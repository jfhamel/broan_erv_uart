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
        cv.Optional(CONF_VENTILATION_STATE): text_sensor.text_sensor_schema(
            icon=ICON_FAN,
        ),
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
