import esphome.codegen as cg
from esphome.components import text_sensor
import esphome.config_validation as cv
from esphome.const import ICON_FAN

from . import CONF_BROAN_ID, BroanComponent

DEPENDENCIES = ["broan"]

CONF_VENTILATION_STATE = "ventilation_state"
CONF_BASE_FAN_MODE = "base_fan_mode"

CONFIG_SCHEMA = cv.Schema(
    {
        cv.GenerateID(CONF_BROAN_ID): cv.use_id(BroanComponent),
        # What the ERV is physically doing right now: "off", "exchange",
        # "recirculation", "deshumidistat", "turbo" or "override". Based on 07:20
        # (VentilationState) - see broan-erv-protocole.md for the full table.
        cv.Optional(CONF_VENTILATION_STATE): text_sensor.text_sensor_schema(
            icon=ICON_FAN,
        ),
        # The base mode (02:20) the ERV falls back to once an override (Turbo/
        # Absence/Deshumidistat/Ovr) ends - never itself holds an override value,
        # only "normal" modes (off/smart/intermittent/exchange*/recirculation*).
        # Read-only, purely informational - see commanded_fan_mode (00:20) to
        # control it, and cancelOverride()/the turbo switch for how this is used
        # internally to know what to revert to.
        cv.Optional(CONF_BASE_FAN_MODE): text_sensor.text_sensor_schema(
            icon=ICON_FAN,
        ),
    }
)


async def to_code(config):
    broan_component = await cg.get_variable(config[CONF_BROAN_ID])
    if ventilation_state_config := config.get(CONF_VENTILATION_STATE):
        sens = await text_sensor.new_text_sensor(ventilation_state_config)
        cg.add(broan_component.set_ventilation_state_text_sensor(sens))

    if base_fan_mode_config := config.get(CONF_BASE_FAN_MODE):
        sens = await text_sensor.new_text_sensor(base_fan_mode_config)
        cg.add(broan_component.set_base_fan_mode_text_sensor(sens))
