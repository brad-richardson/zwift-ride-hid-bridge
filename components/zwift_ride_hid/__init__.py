# SPDX-License-Identifier: GPL-3.0-only

from esphome import automation
from esphome import codegen as cg
from esphome import config_validation as cv
from esphome import pins
from esphome.components import (
    binary_sensor,
    ble_client,
    esp32_ble,
    esp32_ble_tracker,
    ota,
    sensor,
    text_sensor,
)
from esphome.components.esp32_ble import BTLoggers
from esphome.const import (
    CONF_ID,
    DEVICE_CLASS_CONNECTIVITY,
    ENTITY_CATEGORY_DIAGNOSTIC,
    ICON_COUNTER,
    STATE_CLASS_MEASUREMENT,
    UNIT_SECOND,
)

DEPENDENCIES = ["esp32", "ble_client", "esp32_ble"]
AUTO_LOAD = ["binary_sensor", "sensor", "text_sensor"]

CONF_ADVERTISEMENT_AGE = "advertisement_age"
CONF_ADVERTISING_RATE = "advertising_rate"
CONF_ANALOG_LEVERS = "analog_levers"
CONF_BUTTON_FEEDBACK = "button_feedback"
CONF_CONNECT_CONFIRMATION = "connect_confirmation"
CONF_DEBUG_ADVERTISEMENTS = "debug_advertisements"
CONF_DEBUG_CAPTURE = "debug_capture"
CONF_DIAGNOSTICS = "diagnostics"
CONF_DISCONNECT_AFTER = "disconnect_after"
CONF_EXPOSE_RAW = "expose_raw"
CONF_HAPTIC_TIMEOUT_COUNT = "haptic_timeout_count"
CONF_HAPTICS = "haptics"
CONF_HID_CONNECTED = "hid_connected"
CONF_HID_NAME = "hid_name"
CONF_HID_REPORT_COUNT = "hid_report_count"
CONF_IDLE_DISCONNECT_COUNT = "idle_disconnect_count"
CONF_IDLE_TIMEOUT = "idle_timeout"
CONF_INVALID_FRAME_COUNT = "invalid_frame_count"
CONF_LEFT_LEVER = "left_lever"
CONF_MAX_SUPPRESSION = "max_suppression"
CONF_PRESS_THRESHOLD = "press_threshold"
CONF_PROFILE = "profile"
CONF_READY = "ready"
CONF_RECONNECT_COUNT = "reconnect_count"
CONF_RELEASE_HID = "release_hid"
CONF_RELEASE_THRESHOLD = "release_threshold"
CONF_RIDE_ADVERTISING = "ride_advertising"
CONF_RIDE_CONNECTED = "ride_connected"
CONF_RIGHT_LEVER = "right_lever"
CONF_SETUP_TIMEOUT_COUNT = "setup_timeout_count"
CONF_SLEEP_CONFIRMATION = "sleep_confirmation"
CONF_SLOW_RATE = "slow_rate"
CONF_STATE = "state"
CONF_STATUS_LED = "status_led"
CONF_RATE_SAMPLES = "rate_samples"
CONF_WAKE_RATE = "wake_rate"

# Elapsed times use rollover-safe unsigned subtraction, which needs every
# interval to stay well below 2^31 ms. A day is a generous practical ceiling.
MAX_INTERVAL_MS = 24 * 60 * 60 * 1000

PROFILES = ("delta_emulator", "diagnostic_all_inputs")

zwift_ride_hid_ns = cg.esphome_ns.namespace("zwift_ride_hid")
ZwiftRideHid = zwift_ride_hid_ns.class_(
    "ZwiftRideHid", cg.Component, ble_client.BLEClientNode
)
ReconnectAction = zwift_ride_hid_ns.class_("ReconnectAction", automation.Action)

ANALOG_LEVER_SCHEMA = cv.Schema(
    {
        cv.Optional(CONF_PRESS_THRESHOLD, default=35): cv.int_range(min=1, max=100),
        cv.Optional(CONF_RELEASE_THRESHOLD, default=20): cv.int_range(min=0, max=99),
        cv.Optional(CONF_EXPOSE_RAW, default=False): cv.boolean,
    }
)

HAPTICS_SCHEMA = cv.Schema(
    {
        cv.Optional(CONF_CONNECT_CONFIRMATION, default=True): cv.boolean,
        cv.Optional(CONF_BUTTON_FEEDBACK, default=False): cv.boolean,
    }
)


def _interval(minimum_ms, zero_disables=False):
    """A millisecond time period bounded so elapsed times stay rollover-safe."""

    def validate(value):
        period = cv.positive_time_period_milliseconds(value)
        milliseconds = period.total_milliseconds
        if zero_disables and milliseconds == 0:
            return period
        if milliseconds < minimum_ms:
            raise cv.Invalid(
                f"must be at least {minimum_ms} ms"
                + (" or 0 to disable" if zero_disables else "")
            )
        if milliseconds > MAX_INTERVAL_MS:
            raise cv.Invalid(f"must not exceed {MAX_INTERVAL_MS} ms")
        return period

    return validate


IDLE_TIMEOUT_SCHEMA = cv.Schema(
    {
        # Zero holds the Ride link open indefinitely, which is the original
        # behavior and flattens the controllers when the bridge is unattended.
        cv.Optional(CONF_DISCONNECT_AFTER, default="15min"): _interval(
            60 * 1000, zero_disables=True
        ),
        cv.Optional(CONF_SLEEP_CONFIRMATION, default="30s"): _interval(5 * 1000),
        # Zero removes the safety net: a controller that never stops
        # advertising would then keep the bridge offline until a reboot.
        cv.Optional(CONF_MAX_SUPPRESSION, default="60min"): _interval(
            60 * 1000, zero_disables=True
        ),
        # A connected keyboard makes iPadOS hide its on-screen keyboard, so the
        # bonded host is released for the duration of a long idle period.
        cv.Optional(CONF_RELEASE_HID, default=True): cv.boolean,
        # Ride Left does not stop advertising when released; it drops from a
        # ~196 ms rate to a ~640 ms one before sleeping. Those two rates are
        # the only usable discriminator, so reconnection is decided by the
        # measured mean gap crossing these thresholds, with hysteresis.
        cv.Optional(CONF_SLOW_RATE, default="500ms"): _interval(50),
        cv.Optional(CONF_WAKE_RATE, default="350ms"): _interval(50),
        cv.Optional(CONF_RATE_SAMPLES, default=8): cv.int_range(min=3, max=12),
    }
)

DIAGNOSTICS_SCHEMA = cv.Schema(
    {
        cv.Optional(CONF_RIDE_CONNECTED): binary_sensor.binary_sensor_schema(
            device_class=DEVICE_CLASS_CONNECTIVITY,
            entity_category=ENTITY_CATEGORY_DIAGNOSTIC,
        ),
        cv.Optional(CONF_HID_CONNECTED): binary_sensor.binary_sensor_schema(
            device_class=DEVICE_CLASS_CONNECTIVITY,
            entity_category=ENTITY_CATEGORY_DIAGNOSTIC,
        ),
        cv.Optional(CONF_READY): binary_sensor.binary_sensor_schema(
            device_class=DEVICE_CLASS_CONNECTIVITY,
            entity_category=ENTITY_CATEGORY_DIAGNOSTIC,
        ),
        # True while Ride Left is present: connected, or seen advertising
        # recently. False means it has gone quiet.
        cv.Optional(CONF_RIDE_ADVERTISING): binary_sensor.binary_sensor_schema(
            device_class=DEVICE_CLASS_CONNECTIVITY,
            entity_category=ENTITY_CATEGORY_DIAGNOSTIC,
        ),
        cv.Optional(CONF_ADVERTISEMENT_AGE): sensor.sensor_schema(
            unit_of_measurement=UNIT_SECOND,
            accuracy_decimals=1,
            state_class=STATE_CLASS_MEASUREMENT,
            entity_category=ENTITY_CATEGORY_DIAGNOSTIC,
        ),
        # The mean gap the re-arm thresholds are compared against. Roughly
        # 196 ms while the controller is awake and 640 ms while winding down.
        cv.Optional(CONF_ADVERTISING_RATE): sensor.sensor_schema(
            unit_of_measurement="ms",
            accuracy_decimals=0,
            state_class=STATE_CLASS_MEASUREMENT,
            entity_category=ENTITY_CATEGORY_DIAGNOSTIC,
        ),
        cv.Optional(CONF_STATE): text_sensor.text_sensor_schema(
            entity_category=ENTITY_CATEGORY_DIAGNOSTIC,
        ),
        cv.Optional(CONF_RECONNECT_COUNT): sensor.sensor_schema(
            icon=ICON_COUNTER,
            accuracy_decimals=0,
            state_class=STATE_CLASS_MEASUREMENT,
            entity_category=ENTITY_CATEGORY_DIAGNOSTIC,
        ),
        cv.Optional(CONF_INVALID_FRAME_COUNT): sensor.sensor_schema(
            icon=ICON_COUNTER,
            accuracy_decimals=0,
            state_class=STATE_CLASS_MEASUREMENT,
            entity_category=ENTITY_CATEGORY_DIAGNOSTIC,
        ),
        cv.Optional(CONF_HID_REPORT_COUNT): sensor.sensor_schema(
            icon=ICON_COUNTER,
            accuracy_decimals=0,
            state_class=STATE_CLASS_MEASUREMENT,
            entity_category=ENTITY_CATEGORY_DIAGNOSTIC,
        ),
        cv.Optional(CONF_IDLE_DISCONNECT_COUNT): sensor.sensor_schema(
            icon=ICON_COUNTER,
            accuracy_decimals=0,
            state_class=STATE_CLASS_MEASUREMENT,
            entity_category=ENTITY_CATEGORY_DIAGNOSTIC,
        ),
        cv.Optional(CONF_SETUP_TIMEOUT_COUNT): sensor.sensor_schema(
            icon=ICON_COUNTER,
            accuracy_decimals=0,
            state_class=STATE_CLASS_MEASUREMENT,
            entity_category=ENTITY_CATEGORY_DIAGNOSTIC,
        ),
        cv.Optional(CONF_HAPTIC_TIMEOUT_COUNT): sensor.sensor_schema(
            icon=ICON_COUNTER,
            accuracy_decimals=0,
            state_class=STATE_CLASS_MEASUREMENT,
            entity_category=ENTITY_CATEGORY_DIAGNOSTIC,
        ),
        cv.Optional(CONF_LEFT_LEVER): sensor.sensor_schema(
            accuracy_decimals=0,
            entity_category=ENTITY_CATEGORY_DIAGNOSTIC,
        ),
        cv.Optional(CONF_RIGHT_LEVER): sensor.sensor_schema(
            accuracy_decimals=0,
            entity_category=ENTITY_CATEGORY_DIAGNOSTIC,
        ),
    }
)


def _validate_thresholds(config):
    analog = config[CONF_ANALOG_LEVERS]
    if analog[CONF_RELEASE_THRESHOLD] >= analog[CONF_PRESS_THRESHOLD]:
        raise cv.Invalid("release_threshold must be lower than press_threshold")
    return config


def _validate_idle_timeout(config):
    idle = config[CONF_IDLE_TIMEOUT]
    if idle[CONF_WAKE_RATE].total_milliseconds >= idle[CONF_SLOW_RATE].total_milliseconds:
        raise cv.Invalid(
            "wake_rate must be faster than slow_rate; without a gap between "
            "them the detector oscillates instead of latching"
        )
    suppression_ms = idle[CONF_MAX_SUPPRESSION].total_milliseconds
    if suppression_ms == 0:
        return config
    if suppression_ms <= idle[CONF_SLEEP_CONFIRMATION].total_milliseconds:
        raise cv.Invalid(
            "max_suppression must be longer than sleep_confirmation, otherwise "
            "the bridge reconnects before it can tell that the controllers slept"
        )
    return config


def _validate_hid_name(value):
    value = cv.string_strict(value)
    byte_length = len(value.encode("utf-8"))
    if not 1 <= byte_length <= 20:
        raise cv.Invalid("hid_name must contain 1..20 UTF-8 bytes")
    return value


CONFIG_SCHEMA = cv.All(
    cv.Schema(
        {
            cv.GenerateID(): cv.declare_id(ZwiftRideHid),
            cv.GenerateID(esp32_ble.CONF_BLE_ID): cv.use_id(esp32_ble.ESP32BLE),
            cv.Optional(CONF_HID_NAME, default="Zwift Ride KB"): _validate_hid_name,
            cv.Optional(CONF_PROFILE, default="delta_emulator"): cv.one_of(
                *PROFILES, lower=True
            ),
            cv.Optional(CONF_ANALOG_LEVERS, default={}): ANALOG_LEVER_SCHEMA,
            cv.Optional(CONF_HAPTICS, default={}): HAPTICS_SCHEMA,
            cv.Optional(CONF_IDLE_TIMEOUT, default={}): IDLE_TIMEOUT_SCHEMA,
            cv.Optional(CONF_STATUS_LED): pins.gpio_output_pin_schema,
            cv.Optional(CONF_DIAGNOSTICS, default={}): DIAGNOSTICS_SCHEMA,
            cv.Optional(CONF_DEBUG_CAPTURE, default=False): cv.boolean,
            # Logs every matching Ride advertisement with its manufacturer
            # payload, flags, and interval. Needed to establish the real
            # advertising cadence before sleep_confirmation can be tightened.
            cv.Optional(CONF_DEBUG_ADVERTISEMENTS, default=False): cv.boolean,
        }
    )
    .extend(cv.COMPONENT_SCHEMA)
    .extend(ble_client.BLE_CLIENT_SCHEMA)
    .extend(esp32_ble_tracker.ESP_BLE_DEVICE_SCHEMA),
    _validate_thresholds,
    _validate_idle_timeout,
    cv.only_with_framework("esp-idf"),
)

# One connection is Ride Left (reserved by ble_client) and one is the HID host.
FINAL_VALIDATE_SCHEMA = esp32_ble.consume_connection_slots(1, "zwift_ride_hid")


@automation.register_action(
    "zwift_ride_hid.reconnect",
    ReconnectAction,
    cv.Schema({cv.GenerateID(): cv.use_id(ZwiftRideHid)}),
)
async def reconnect_action_to_code(config, action_id, template_arg, args):
    var = cg.new_Pvariable(action_id, template_arg)
    await cg.register_parented(var, config[CONF_ID])
    return var


async def to_code(config):
    esp32_ble.register_bt_logger(BTLoggers.GATT, BTLoggers.SMP, BTLoggers.HID)
    cg.add_define("USE_ESP32_BLE_SERVER")
    cg.add_define("USE_ESP32_BLE_ADVERTISING")
    cg.add_define("USE_ESP32_BLE_UUID")
    ota.request_ota_state_listeners()

    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    await ble_client.register_ble_node(var, config)
    await esp32_ble_tracker.register_ble_device(var, config)
    tracker = await cg.get_variable(config[esp32_ble_tracker.CONF_ESP32_BLE_ID])
    cg.add(var.set_ble_tracker(tracker))

    # BLEClientNode already receives GAP events via its BLEClient parent. Registering
    # it again with the process-wide broker would deliver every GAP event twice.
    parent = await cg.get_variable(config[esp32_ble.CONF_BLE_ID])
    esp32_ble.register_gatts_event_handler(parent, var)
    esp32_ble.register_ble_status_event_handler(parent, var)
    cg.add(var.set_ble_parent(parent))

    analog = config[CONF_ANALOG_LEVERS]
    haptics = config[CONF_HAPTICS]
    cg.add(parent.set_name(config[CONF_HID_NAME]))
    cg.add(parent.advertising_set_appearance(0x03C1))  # HID keyboard
    cg.add(var.set_hid_name(config[CONF_HID_NAME]))
    cg.add(var.set_profile(config[CONF_PROFILE]))
    cg.add(var.set_press_threshold(analog[CONF_PRESS_THRESHOLD]))
    cg.add(var.set_release_threshold(analog[CONF_RELEASE_THRESHOLD]))
    cg.add(var.set_expose_raw(analog[CONF_EXPOSE_RAW]))
    cg.add(var.set_connect_haptic(haptics[CONF_CONNECT_CONFIRMATION]))
    cg.add(var.set_button_haptic(haptics[CONF_BUTTON_FEEDBACK]))
    cg.add(var.set_debug_capture(config[CONF_DEBUG_CAPTURE]))
    cg.add(var.set_debug_advertisements(config[CONF_DEBUG_ADVERTISEMENTS]))

    idle = config[CONF_IDLE_TIMEOUT]
    cg.add(
        var.set_idle_timeout(
            idle[CONF_DISCONNECT_AFTER].total_milliseconds,
            idle[CONF_SLEEP_CONFIRMATION].total_milliseconds,
            idle[CONF_MAX_SUPPRESSION].total_milliseconds,
            idle[CONF_SLOW_RATE].total_milliseconds,
            idle[CONF_WAKE_RATE].total_milliseconds,
            idle[CONF_RATE_SAMPLES],
        )
    )
    cg.add(var.set_release_hid_when_idle(idle[CONF_RELEASE_HID]))

    if CONF_STATUS_LED in config:
        pin = await cg.gpio_pin_expression(config[CONF_STATUS_LED])
        cg.add(var.set_status_led(pin))

    diagnostics = config[CONF_DIAGNOSTICS]
    diagnostic_setters = {
        CONF_RIDE_CONNECTED: (binary_sensor.new_binary_sensor, "set_ride_connected_sensor"),
        CONF_HID_CONNECTED: (binary_sensor.new_binary_sensor, "set_hid_connected_sensor"),
        CONF_READY: (binary_sensor.new_binary_sensor, "set_ready_sensor"),
        CONF_RIDE_ADVERTISING: (
            binary_sensor.new_binary_sensor,
            "set_ride_advertising_sensor",
        ),
        CONF_ADVERTISEMENT_AGE: (sensor.new_sensor, "set_advertisement_age_sensor"),
        CONF_ADVERTISING_RATE: (sensor.new_sensor, "set_advertising_rate_sensor"),
        CONF_STATE: (text_sensor.new_text_sensor, "set_state_text_sensor"),
        CONF_RECONNECT_COUNT: (sensor.new_sensor, "set_reconnect_count_sensor"),
        CONF_INVALID_FRAME_COUNT: (sensor.new_sensor, "set_invalid_frame_count_sensor"),
        CONF_HID_REPORT_COUNT: (sensor.new_sensor, "set_hid_report_count_sensor"),
        CONF_IDLE_DISCONNECT_COUNT: (
            sensor.new_sensor,
            "set_idle_disconnect_count_sensor",
        ),
        CONF_SETUP_TIMEOUT_COUNT: (sensor.new_sensor, "set_setup_timeout_count_sensor"),
        CONF_HAPTIC_TIMEOUT_COUNT: (
            sensor.new_sensor,
            "set_haptic_timeout_count_sensor",
        ),
        CONF_LEFT_LEVER: (sensor.new_sensor, "set_left_lever_sensor"),
        CONF_RIGHT_LEVER: (sensor.new_sensor, "set_right_lever_sensor"),
    }
    for key, (factory, setter) in diagnostic_setters.items():
        if key in diagnostics:
            entity = await factory(diagnostics[key])
            cg.add(getattr(var, setter)(entity))
