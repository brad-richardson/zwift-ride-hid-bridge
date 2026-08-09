# SPDX-License-Identifier: GPL-3.0-only

from esphome import codegen as cg
from esphome import config_validation as cv
from esphome.components import ble_client
from esphome.const import CONF_ID

DEPENDENCIES = ["esp32", "ble_client"]

CONF_ANALOG_LEVERS = "analog_levers"
CONF_DEBUG_CAPTURE = "debug_capture"
CONF_EXPOSE_RAW = "expose_raw"
CONF_HID_NAME = "hid_name"
CONF_PRESS_THRESHOLD = "press_threshold"
CONF_PROFILE = "profile"
CONF_RELEASE_THRESHOLD = "release_threshold"

PROFILES = ("delta_emulator", "diagnostic_all_inputs")

zwift_ride_hid_ns = cg.esphome_ns.namespace("zwift_ride_hid")
ZwiftRideHid = zwift_ride_hid_ns.class_(
    "ZwiftRideHid", cg.Component, ble_client.BLEClientNode
)

ANALOG_LEVER_SCHEMA = cv.Schema(
    {
        cv.Optional(CONF_PRESS_THRESHOLD, default=35): cv.int_range(min=1, max=100),
        cv.Optional(CONF_RELEASE_THRESHOLD, default=20): cv.int_range(min=0, max=99),
        cv.Optional(CONF_EXPOSE_RAW, default=False): cv.boolean,
    }
)


def _validate_thresholds(config):
    analog = config[CONF_ANALOG_LEVERS]
    if analog[CONF_RELEASE_THRESHOLD] >= analog[CONF_PRESS_THRESHOLD]:
        raise cv.Invalid("release_threshold must be lower than press_threshold")
    return config


CONFIG_SCHEMA = cv.All(
    cv.Schema(
        {
            cv.GenerateID(): cv.declare_id(ZwiftRideHid),
            cv.Optional(CONF_HID_NAME, default="Zwift Ride KB"): cv.string_strict,
            cv.Optional(CONF_PROFILE, default="delta_emulator"): cv.one_of(
                *PROFILES, lower=True
            ),
            cv.Optional(CONF_ANALOG_LEVERS, default={}): ANALOG_LEVER_SCHEMA,
            cv.Optional(CONF_DEBUG_CAPTURE, default=False): cv.boolean,
        }
    )
    .extend(cv.COMPONENT_SCHEMA)
    .extend(ble_client.BLE_CLIENT_SCHEMA),
    _validate_thresholds,
)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    await ble_client.register_ble_node(var, config)

    analog = config[CONF_ANALOG_LEVERS]
    cg.add(var.set_hid_name(config[CONF_HID_NAME]))
    cg.add(var.set_profile(config[CONF_PROFILE]))
    cg.add(var.set_press_threshold(analog[CONF_PRESS_THRESHOLD]))
    cg.add(var.set_release_threshold(analog[CONF_RELEASE_THRESHOLD]))
    cg.add(var.set_expose_raw(analog[CONF_EXPOSE_RAW]))
    cg.add(var.set_debug_capture(config[CONF_DEBUG_CAPTURE]))
