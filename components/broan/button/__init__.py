import esphome.codegen as cg
from esphome.components import button
import esphome.config_validation as cv
from esphome.const import (
    ENTITY_CATEGORY_CONFIG,
    ICON_AIR_FILTER,
)

from .. import CONF_BROAN_ID, BroanComponent, broan_ns

FilterLifeResetButton = broan_ns.class_("FilterLifeResetButton", button.Button)

CONF_FILTER_LIFE_RESET = "filter_life_reset"

CONFIG_SCHEMA = {
    cv.GenerateID(CONF_BROAN_ID): cv.use_id(BroanComponent),
	cv.Optional(CONF_FILTER_LIFE_RESET): button.button_schema(
        FilterLifeResetButton,
        entity_category=ENTITY_CATEGORY_CONFIG,
        icon=ICON_AIR_FILTER,
    ),
}


async def to_code(config):
    broan_component = await cg.get_variable(config[CONF_BROAN_ID])
    if filter_life_reset_config := config.get(CONF_FILTER_LIFE_RESET):
        b = await button.new_button(filter_life_reset_config)
        await cg.register_parented(b, config[CONF_BROAN_ID])
        cg.add(broan_component.set_filter_life_reset_button(b))
