#!/usr/bin/env python3
"""Read the bridge's diagnostic entities over the encrypted ESPHome API.

`snapshot` prints one reading and exits; `watch` streams changes, which is the
practical way to observe the idle disconnect, the sleep/wake reconnect, and
controller loss without a Home Assistant instance.

Configuration comes from the environment so no site-specific address or key
path enters this repository:

    ZWIFT_BRIDGE_HOST          device address or hostname   (required)
    ZWIFT_BRIDGE_NAME          expected esphome name        (required)
    ZWIFT_BRIDGE_API_KEY_FILE  file holding the API key     (required)
    ZWIFT_BRIDGE_PORT          API port                     (default 6053)

Usage:
    tools/bridge_state.py snapshot
    tools/bridge_state.py watch [seconds]
"""

from __future__ import annotations

import asyncio
import json
import os
from pathlib import Path
import sys
import time

from aioesphomeapi import APIClient

# Everything the bridge publishes about its own health. Wi-Fi and uptime are
# included because a reconnect that follows a reboot means something different
# from one that follows a controller wake.
DIAGNOSTIC_ENTITIES = {
    "Ride Controller Connected",
    "HID Host Ready",
    "Bridge Ready",
    "Bridge State",
    "Ride Reconnect Count",
    "Ride Idle Disconnect Count",
    "Ride Setup Timeout Count",
    "Ride Haptic Timeout Count",
    "Invalid Ride Frame Count",
    "HID Report Count",
    "Left Lever Raw",
    "Right Lever Raw",
    "Uptime",
}


def environment(name: str, default: str | None = None) -> str:
    value = os.environ.get(name, default)
    if value is None:
        raise SystemExit(f"{name} must be set; see the module docstring")
    return value


def build_client() -> APIClient:
    return APIClient(
        environment("ZWIFT_BRIDGE_HOST"),
        int(environment("ZWIFT_BRIDGE_PORT", "6053")),
        noise_psk=Path(environment("ZWIFT_BRIDGE_API_KEY_FILE")).read_text().strip(),
        expected_name=environment("ZWIFT_BRIDGE_NAME"),
        client_info="zwift-ride-hid-bridge validation",
    )


async def snapshot() -> None:
    client = build_client()
    await client.connect(login=True)
    try:
        device = await client.device_info()
        entities, _ = await client.list_entities_services()
        entity_by_key = {entity.key: entity for entity in entities}
        states: dict[int, object] = {}
        first_state = asyncio.Event()

        def on_state(state) -> None:
            states[state.key] = state
            first_state.set()

        client.subscribe_states(on_state)
        await asyncio.wait_for(first_state.wait(), timeout=5)
        # Entities publish independently, so allow the slower diagnostics to
        # arrive before taking the reading.
        await asyncio.sleep(2)

        readings = [
            {
                "name": entity_by_key[key].name,
                "state": getattr(state, "state", None),
                "missing_state": getattr(state, "missing_state", False),
            }
            for key, state in states.items()
            if key in entity_by_key
            and entity_by_key[key].name in DIAGNOSTIC_ENTITIES
        ]
        readings.sort(key=lambda item: item["name"])
        print(
            json.dumps(
                {
                    "device": device.name,
                    "esphome_version": device.esphome_version,
                    "entities": readings,
                },
                indent=2,
            )
        )
    finally:
        await client.disconnect()


async def watch(seconds: float) -> None:
    client = build_client()
    await client.connect(login=True)
    try:
        entities, _ = await client.list_entities_services()
        entity_by_key = {entity.key: entity for entity in entities}
        previous: dict[str, str] = {}

        def on_state(state) -> None:
            entity = entity_by_key.get(state.key)
            if entity is None or entity.name not in DIAGNOSTIC_ENTITIES:
                return
            value = getattr(state, "state", None)
            # ESPHome republishes unchanged states; only transitions matter here.
            comparable = repr(value)
            if previous.get(entity.name) == comparable:
                return
            previous[entity.name] = comparable
            print(
                json.dumps(
                    {
                        "time": time.strftime("%H:%M:%S"),
                        "name": entity.name,
                        "state": value,
                    },
                    allow_nan=True,
                ),
                flush=True,
            )

        client.subscribe_states(on_state)
        await asyncio.sleep(seconds)
    finally:
        await client.disconnect()


def main() -> None:
    operation = sys.argv[1] if len(sys.argv) > 1 else "snapshot"
    if operation == "snapshot":
        asyncio.run(snapshot())
        return
    if operation == "watch":
        asyncio.run(watch(float(sys.argv[2]) if len(sys.argv) > 2 else 180.0))
        return
    raise SystemExit(__doc__)


if __name__ == "__main__":
    main()
