import esphome.codegen as cg
from esphome.components import button
import esphome.config_validation as cv
from esphome.const import (
    ENTITY_CATEGORY_CONFIG,
    ICON_RESTART,
)

from .. import CONF_BROAN_ID, BroanComponent, broan_ns

FilterResetButton = broan_ns.class_("FilterResetButton", button.Button)
CancelOverrideButton = broan_ns.class_("CancelOverrideButton", button.Button)

CONF_FILTER_RESET = "filter_reset"
CONF_CANCEL_OVERRIDE = "cancel_override"

CONFIG_SCHEMA = {
    cv.GenerateID(CONF_BROAN_ID): cv.use_id(BroanComponent),
    cv.Optional(CONF_FILTER_RESET): button.button_schema(
        FilterResetButton,
        entity_category=ENTITY_CATEGORY_CONFIG,
        icon=ICON_RESTART,
    ),
    # Unlike the fan_mode select, a button always fires on every press regardless
    # of what's currently displayed - reliable even if fan_mode's shown state is
    # stale. Returns to whatever base mode (02:20) the ERV was actually running
    # before Turbo/Absence/Humidity/Ovr took over.
    cv.Optional(CONF_CANCEL_OVERRIDE): button.button_schema(
        CancelOverrideButton,
        entity_category=ENTITY_CATEGORY_CONFIG,
        icon="mdi:cancel",
    ),
}


async def to_code(config):
    broan_component = await cg.get_variable(config[CONF_BROAN_ID])
    if filter_reset_config := config.get(CONF_FILTER_RESET):
        b = await button.new_button(filter_reset_config)
        await cg.register_parented(b, config[CONF_BROAN_ID])
        cg.add(broan_component.set_filter_reset_button(b))

    if cancel_override_config := config.get(CONF_CANCEL_OVERRIDE):
        b = await button.new_button(cancel_override_config)
        await cg.register_parented(b, config[CONF_BROAN_ID])
        cg.add(broan_component.set_cancel_override_button(b))
