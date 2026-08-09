#!/usr/bin/env python3
"""Stream the device's serial-equivalent log through Device Builder.

Device Builder holds the OTA log connection, so this is the way to watch the
bridge without unplugging it. It uses the same environment as tools/dashboard.py:

    ESPHOME_DASHBOARD_URL          ws://<host>:6052/ws           (required)
    ESPHOME_DASHBOARD_CREDENTIALS  file with username\\npassword  (required)
    ESPHOME_DASHBOARD_CONFIG       dashboard configuration name  (required)

Usage:
    tools/dashboard_logs.py [seconds] [regex]

With no regex the default filter keeps bridge lifecycle lines and drops the
idle 0x23 input frames, which otherwise arrive continuously.
"""

from __future__ import annotations

import base64
import json
import os
from pathlib import Path
import re
import sys
import time

from websockets.sync.client import connect

# Ride/HID lifecycle, scanner policy, and the idle disconnect path.
DEFAULT_FILTER = re.compile(
    r"Ride Left|Ride idle|idle timeout|woke|zwift_ride|HID|Scanner State:|"
    r"Continuous Scanning:|Stopping scan|Bridge"
)
# The controllers notify continuously; an all-ones mask is the idle frame.
IDLE_INPUT_FRAME = "raw=23 08 FF FF FF FF 0F"


def environment(name: str) -> str:
    value = os.environ.get(name)
    if value is None:
        raise SystemExit(f"{name} must be set; see the module docstring")
    return value


def main() -> None:
    seconds = float(sys.argv[1]) if len(sys.argv) > 1 else 120.0
    pattern = re.compile(sys.argv[2]) if len(sys.argv) > 2 else DEFAULT_FILTER

    credentials = (
        Path(environment("ESPHOME_DASHBOARD_CREDENTIALS")).read_text().splitlines()
    )
    authorization = base64.b64encode(
        f"{credentials[0]}:{credentials[1]}".encode()
    ).decode()

    with connect(
        environment("ESPHOME_DASHBOARD_URL"),
        additional_headers={"Authorization": f"Basic {authorization}"},
        open_timeout=10,
    ) as websocket:
        json.loads(websocket.recv(timeout=10))
        websocket.send(
            json.dumps(
                {
                    "command": "auth/login",
                    "message_id": "1",
                    "args": {
                        "username": credentials[0],
                        "password": credentials[1],
                    },
                }
            )
        )
        if "result" not in json.loads(websocket.recv(timeout=10)):
            raise SystemExit("dashboard authentication failed")

        websocket.send(
            json.dumps(
                {
                    "command": "devices/logs",
                    "message_id": "2",
                    "args": {
                        "configuration": environment("ESPHOME_DASHBOARD_CONFIG"),
                        "port": "OTA",
                        "no_states": True,
                    },
                }
            )
        )

        deadline = time.monotonic() + seconds
        while time.monotonic() < deadline:
            try:
                message = json.loads(
                    websocket.recv(timeout=max(0.1, deadline - time.monotonic()))
                )
            except TimeoutError:
                break
            if message.get("message_id") != "2":
                continue
            if message.get("event") == "output":
                data = message.get("data", "")
                if IDLE_INPUT_FRAME in data:
                    continue
                if pattern.search(data):
                    print(data, end="", flush=True)
            elif message.get("event") == "result":
                print(json.dumps(message.get("data", {})), flush=True)
                break
            elif "error_code" in message:
                raise SystemExit(
                    f"{message['error_code']}: {message.get('details', '')}"
                )


if __name__ == "__main__":
    main()
