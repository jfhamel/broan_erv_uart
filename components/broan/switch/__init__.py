import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import switch
from esphome.const import (
    CONF_ID,
    ENTITY_CATEGORY_CONFIG,
    ICON_WATER,
    ICON_FAN,
)

from .. import CONF_BROAN_ID, BroanComponent, broan_ns

HumidityControlSwitch = broan_ns.class_("HumidityControlSwitch", switch.Switch)
TurboSwitch = broan_ns.class_("TurboSwitch", switch.Switch)

CONF_HUMIDITY_CONTROL = "humidity_control"
CONF_TURBO = "turbo"

CONFIG_SCHEMA = {
    cv.GenerateID(CONF_BROAN_ID): cv.use_id(BroanComponent),
    cv.Optional(CONF_HUMIDITY_CONTROL): switch.switch_schema(
        HumidityControlSwitch,
        entity_category=ENTITY_CATEGORY_CONFIG,
        icon=ICON_WATER,
    ),
    # Turning this on starts Turbo using whatever duration is currently set on the
    # turbo_duration number. Turns itself back off once Turbo ends (timer expiry or
    # any other mode change) - it reflects real device state, it isn't just a toggle.
    # Turning it off manually cancels Turbo early.
    cv.Optional(CONF_TURBO): switch.switch_schema(
        TurboSwitch,
        entity_category=ENTITY_CATEGORY_CONFIG,
        icon=ICON_FAN,
    ),
}


async def to_code(config):
    broan_component = await cg.get_variable(config[CONF_BROAN_ID])
    if humidity_control_config := config.get(CONF_HUMIDITY_CONTROL):
        s = await switch.new_switch(humidity_control_config)
        await cg.register_parented(s, config[CONF_BROAN_ID])
        cg.add(broan_component.set_humidity_control_switch(s))

    if turbo_config := config.get(CONF_TURBO):
        s = await switch.new_switch(turbo_config)
        await cg.register_parented(s, config[CONF_BROAN_ID])
        cg.add(broan_component.set_turbo_switch(s))
