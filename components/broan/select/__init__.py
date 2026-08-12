import esphome.codegen as cg
from esphome.components import select
import esphome.config_validation as cv
from esphome.const import (
    ENTITY_CATEGORY_CONFIG,
    ICON_GAUGE,
    ICON_TIMER,
    ICON_FAN,
)

from .. import CONF_BROAN_ID, BroanComponent, broan_ns

FanModeSelect = broan_ns.class_("FanModeSelect", select.Select)
TurboDurationSelect = broan_ns.class_("TurboDurationSelect", select.Select)
RecirculationSpeedSelect = broan_ns.class_("RecirculationSpeedSelect", select.Select)

CONF_FAN_MODE = 'fan_mode'
CONF_TURBO_DURATION = 'turbo_duration'
CONF_RECIRCULATION_SPEED = 'recirculation_speed'

CONFIG_SCHEMA = {
    cv.GenerateID(CONF_BROAN_ID): cv.use_id(BroanComponent),

    cv.Optional(CONF_FAN_MODE): select.select_schema(
        FanModeSelect,
        entity_category=ENTITY_CATEGORY_CONFIG,
        icon=ICON_GAUGE,
    ),

    # Selecting a duration here is what activates Turbo - the real wall controller
    # always sends the duration and the mode together in one message, so there's no
    # separate "turbo, unspecified duration" state to expose. Pick "off" (or any other
    # fan_mode) to leave Turbo before the timer runs out.
    cv.Optional(CONF_TURBO_DURATION): select.select_schema(
        TurboDurationSelect,
        entity_category=ENTITY_CATEGORY_CONFIG,
        icon=ICON_TIMER,
    ),

    # Recirculation only has three fixed steps - confirmed by capture that writing a
    # custom CFM target (like fan_speed does for exchange) has no effect on real
    # airflow here. Picking a value activates recirculation at that step directly.
    cv.Optional(CONF_RECIRCULATION_SPEED): select.select_schema(
        RecirculationSpeedSelect,
        entity_category=ENTITY_CATEGORY_CONFIG,
        icon=ICON_FAN,
    ),
}


async def to_code(config):
    broan_component = await cg.get_variable(config[CONF_BROAN_ID])
    if fan_mode_config := config.get(CONF_FAN_MODE):
        s = await select.new_select(
            fan_mode_config,
            options=[
                "off",
                "smart",
                "intermittent",
                "exchange",
                "recirculation",
                "absence",
            ],
        )
        await cg.register_parented(s, config[CONF_BROAN_ID])
        cg.add(broan_component.set_fan_mode_select(s))

    if turbo_duration_config := config.get(CONF_TURBO_DURATION):
        s = await select.new_select(
            turbo_duration_config,
            options=[
                "1h",
                "2h",
                "4h",
            ],
        )
        await cg.register_parented(s, config[CONF_BROAN_ID])
        cg.add(broan_component.set_turbo_duration_select(s))

    if recirculation_speed_config := config.get(CONF_RECIRCULATION_SPEED):
        s = await select.new_select(
            recirculation_speed_config,
            options=[
                "min",
                "med",
                "max",
            ],
        )
        await cg.register_parented(s, config[CONF_BROAN_ID])
        cg.add(broan_component.set_recirculation_speed_select(s))
