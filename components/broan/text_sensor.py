import esphome.codegen as cg
from esphome.components import text_sensor
import esphome.config_validation as cv
from esphome.const import ICON_FAN

from . import CONF_BROAN_ID, BroanComponent

DEPENDENCIES = ["broan"]

CONF_CURRENT_MODE = "current_mode"

CONFIG_SCHEMA = cv.Schema(
    {
        cv.GenerateID(CONF_BROAN_ID): cv.use_id(BroanComponent),
        # What the ERV is physically doing right now: "off", "exchange",
        # "recirculation", "deshumidistat", "turbo" or "override". Based on 07:20
        # (VentilationState) - see broan-erv-protocole.md for the full table.
        cv.Optional(CONF_CURRENT_MODE): text_sensor.text_sensor_schema(
            icon=ICON_FAN,
        ),
    }
)


async def to_code(config):
    broan_component = await cg.get_variable(config[CONF_BROAN_ID])
    if current_mode_config := config.get(CONF_CURRENT_MODE):
        sens = await text_sensor.new_text_sensor(current_mode_config)
        cg.add(broan_component.set_current_mode_text_sensor(sens))
