import esphome.codegen as cg
from esphome.components import select
import esphome.config_validation as cv
from esphome.const import (
    ENTITY_CATEGORY_CONFIG,
    ICON_GAUGE,
)

from .. import CONF_BROAN_ID, BroanComponent, broan_ns

CommandedFanModeSelect = broan_ns.class_("CommandedFanModeSelect", select.Select)

CONF_COMMANDED_FAN_MODE = 'commanded_fan_mode'

CONFIG_SCHEMA = {
    cv.GenerateID(CONF_BROAN_ID): cv.use_id(BroanComponent),

	cv.Optional(CONF_COMMANDED_FAN_MODE): select.select_schema(
        CommandedFanModeSelect,
        entity_category=ENTITY_CATEGORY_CONFIG,
        icon=ICON_GAUGE,
    ),
}


async def to_code(config):
    broan_component = await cg.get_variable(config[CONF_BROAN_ID])
    if commanded_fan_mode_config := config.get(CONF_COMMANDED_FAN_MODE):
        s = await select.new_select(
            commanded_fan_mode_config,
            options=[
                "smart",
                "intermittent",
                "exchange_min",
                "exchange_med",
                "exchange_max",
                "exchange_adjustable",
                "recirculation_min",
                "recirculation_med",
                "recirculation_max",
                "absence",
                "off",
                "auto",
            ],
        )
        await cg.register_parented(s, config[CONF_BROAN_ID])
        cg.add(broan_component.set_commanded_fan_mode_select(s))
