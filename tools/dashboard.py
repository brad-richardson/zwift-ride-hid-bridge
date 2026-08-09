#!/usr/bin/env python3
"""Drive an ESPHome Device Builder instance over its dashboard WebSocket.

The deployed configuration is derived from devices/zwift-ride-hid-bridge.yaml
rather than maintained as a second copy, so the reviewed reference YAML stays
the single source of truth. Only the documented deployment differences are
substituted: the device name, the Wi-Fi secret names, and the immutable Git
source pin.

Configuration comes from the environment so no site-specific address, device
name, or credential ever enters this repository:

    ESPHOME_DASHBOARD_URL          ws://<host>:6052/ws           (required)
    ESPHOME_DASHBOARD_CREDENTIALS  file with username\\npassword  (required)
    ESPHOME_DASHBOARD_CONFIG       dashboard configuration name  (required)
    ESPHOME_DEVICE_NAME            deployed esphome name         (optional)
    ESPHOME_FRIENDLY_NAME          deployed friendly name        (optional)
    ESPHOME_WIFI_SSID_SECRET       Wi-Fi SSID secret name        (default ssid)
    ESPHOME_WIFI_PASSWORD_SECRET   Wi-Fi password secret name    (default password)

Usage:
    tools/dashboard.py render <full-git-sha>
    tools/dashboard.py deploy <full-git-sha>     # validate + save
    tools/dashboard.py compile
    tools/dashboard.py install                   # OTA
    tools/dashboard.py jobs
    tools/dashboard.py status
"""

from __future__ import annotations

import asyncio
import base64
import json
import os
from pathlib import Path
import re
import sys

from websockets.asyncio.client import connect

REPOSITORY = Path(__file__).resolve().parent.parent
REFERENCE_YAML = REPOSITORY / "devices" / "zwift-ride-hid-bridge.yaml"
GIT_URL = "https://github.com/brad-richardson/zwift-ride-hid-bridge.git"

LOCAL_COMPONENT_BLOCK = """external_components:
  - source:
      type: local
      path: ../components
    components: [zwift_ride_hid]
"""


def environment(name: str, default: str | None = None) -> str:
    value = os.environ.get(name, default)
    if value is None:
        raise SystemExit(f"{name} must be set; see the module docstring")
    return value


def substitute_once(content: str, old: str, new: str, label: str) -> str:
    """Replace exactly one occurrence, or fail loudly.

    A silent no-op here would ship the wrong device name or an unpinned source,
    so every deployment difference has to be provably applied.
    """
    occurrences = content.count(old)
    if occurrences != 1:
        raise SystemExit(
            f"expected exactly one {label} in the reference YAML, found {occurrences}"
        )
    return content.replace(old, new)


def render(sha: str) -> str:
    if not re.fullmatch(r"[0-9a-f]{40}", sha):
        raise SystemExit(
            "the deployed source must be a full 40-character commit SHA, "
            "never a branch or a movable tag"
        )

    content = REFERENCE_YAML.read_text()
    content = substitute_once(
        content,
        "  name: zwift-ride-hid-bridge\n",
        f"  name: {environment('ESPHOME_DEVICE_NAME', 'zwift-ride-hid-bridge')}\n",
        "device name",
    )
    content = substitute_once(
        content,
        "  friendly_name: Zwift Ride HID Bridge\n",
        f"  friendly_name: {environment('ESPHOME_FRIENDLY_NAME', 'Zwift Ride HID Bridge')}\n",
        "friendly name",
    )
    content = substitute_once(
        content,
        "  ssid: !secret ssid\n",
        f"  ssid: !secret {environment('ESPHOME_WIFI_SSID_SECRET', 'ssid')}\n",
        "Wi-Fi SSID secret",
    )
    content = substitute_once(
        content,
        "  password: !secret password\n",
        f"  password: !secret {environment('ESPHOME_WIFI_PASSWORD_SECRET', 'password')}\n",
        "Wi-Fi password secret",
    )
    content = substitute_once(
        content,
        LOCAL_COMPONENT_BLOCK,
        "external_components:\n"
        "  - source:\n"
        "      type: git\n"
        f"      url: {GIT_URL}\n"
        f"      ref: {sha}\n"
        "      path: components\n"
        "    components: [zwift_ride_hid]\n"
        "    refresh: never\n",
        "local external_components block",
    )
    return content


async def request(websocket, message_id, command, args=None):
    await websocket.send(
        json.dumps(
            {
                "command": command,
                "message_id": str(message_id),
                "args": args or {},
            }
        )
    )
    while True:
        response = json.loads(await websocket.recv())
        if response.get("message_id") == str(message_id):
            return response


def require_success(response, label):
    if response.get("success") is False or "error" in response:
        raise SystemExit(f"{label} failed: {response.get('error', response)}")
    return response.get("result")


async def run(operation: str, argument: str | None) -> None:
    configuration = environment("ESPHOME_DASHBOARD_CONFIG")
    credentials = Path(environment("ESPHOME_DASHBOARD_CREDENTIALS"))
    username, password = credentials.read_text().splitlines()[:2]
    authorization = base64.b64encode(f"{username}:{password}".encode()).decode()

    async with connect(
        environment("ESPHOME_DASHBOARD_URL"),
        additional_headers={"Authorization": f"Basic {authorization}"},
        max_size=None,
    ) as websocket:
        await websocket.recv()
        require_success(
            await request(
                websocket,
                1,
                "auth/login",
                {"username": username, "password": password},
            ),
            "authentication",
        )

        if operation == "deploy":
            if argument is None:
                raise SystemExit("deploy needs a full commit SHA")
            content = render(argument)
            validated = await request(
                websocket,
                2,
                "editor/validate_yaml",
                {"configuration": configuration, "content": content},
            )
            result = require_success(validated, "validation")
            if result not in (None, {}, []):
                print(json.dumps({"validation": result}, indent=2))
            require_success(
                await request(
                    websocket,
                    3,
                    "devices/update_config",
                    {"configuration": configuration, "content": content},
                ),
                "configuration update",
            )
            saved = require_success(
                await request(
                    websocket,
                    4,
                    "devices/get_config",
                    {"configuration": configuration},
                ),
                "configuration readback",
            )
            saved_content = (
                saved.get("content", saved) if isinstance(saved, dict) else saved
            )
            if saved_content != content:
                raise SystemExit("dashboard readback differs from the rendered candidate")
            print(f"saved {configuration} pinned to {argument}")
            return

        if operation in ("compile", "install"):
            args = {"configuration": configuration}
            if operation == "install":
                args |= {"port": "OTA", "force_local": False, "bootloader": False}
            response = await request(websocket, 2, f"firmware/{operation}", args)
            print(json.dumps(require_success(response, f"{operation} request"), indent=2))
            return

        if operation == "jobs":
            jobs = require_success(
                await request(
                    websocket, 2, "firmware/get_jobs", {"configuration": configuration}
                ),
                "job query",
            )
            print(
                json.dumps(
                    [
                        {
                            key: job.get(key)
                            for key in (
                                "job_id",
                                "job_type",
                                "status",
                                "exit_code",
                                "progress",
                                "depends_on",
                                "failure_reason",
                                "error",
                            )
                        }
                        for job in jobs
                    ],
                    indent=2,
                )
            )
            return

        if operation == "status":
            saved = require_success(
                await request(
                    websocket, 2, "devices/get_config", {"configuration": configuration}
                ),
                "configuration readback",
            )
            content = (
                saved.get("content", saved) if isinstance(saved, dict) else saved
            )
            states = require_success(
                await request(websocket, 3, "devices/get_states"), "state query"
            )
            selected = (
                states.get(configuration)
                if isinstance(states, dict)
                else [
                    item
                    for item in states
                    if item.get("configuration") == configuration
                ]
            )
            ref = next(
                (
                    line.split("ref:", 1)[1].strip()
                    for line in content.splitlines()
                    if "ref:" in line
                ),
                None,
            )
            print(
                json.dumps(
                    {"deployed_ref": ref, "dashboard_state": selected}, indent=2
                )
            )
            return

        raise SystemExit(f"unknown operation: {operation}")


def main() -> None:
    if len(sys.argv) < 2:
        raise SystemExit(__doc__)
    operation = sys.argv[1]
    argument = sys.argv[2] if len(sys.argv) > 2 else None

    if operation == "render":
        if argument is None:
            raise SystemExit("render needs a full commit SHA")
        print(render(argument), end="")
        return

    asyncio.run(run(operation, argument))


if __name__ == "__main__":
    main()
