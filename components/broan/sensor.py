import esphome.codegen as cg
from esphome.components import sensor
import esphome.config_validation as cv
from esphome.const import (
    CONF_POWER,
    CONF_TEMPERATURE,
    DEVICE_CLASS_POWER ,
    ENTITY_CATEGORY_DIAGNOSTIC,
    DEVICE_CLASS_TEMPERATURE,
    DEVICE_CLASS_HUMIDITY,
    ICON_POWER,
    ICON_THERMOMETER,
    ICON_AIR_FILTER,
    ICON_FAN,
    ICON_WATER,
    ICON_TIMER,
    UNIT_WATT,
    UNIT_CELSIUS,
    UNIT_PERCENT,
)

CONF_FILTER_LIFE = "filter_life"
CONF_TEMPERATURE_OUT = "temperature_out"
CONF_SUPPLY_CFM = "supply_fan_cfm"
CONF_EXHAUST_CFM = "exhaust_fan_cfm"
CONF_SUPPLY_RPM = "supply_fan_rpm"
CONF_EXHAUST_RPM = "exhaust_fan_rpm"
CONF_INDOOR_TEMPERATURE = "indoor_temperature"
CONF_INDOOR_HUMIDITY = "indoor_humidity"
CONF_TURBO_REMAINING = "turbo_remaining"
CONF_OVERRIDE_REMAINING = "override_remaining"

UNIT_CFM = "CFM"
UNIT_RPM = "RPM"
UNIT_DAY = "d"
UNIT_MINUTE = "min"

from . import CONF_BROAN_ID, BroanComponent

DEPENDENCIES = ["broan"]

CONFIG_SCHEMA = cv.Schema(
    {
        cv.GenerateID(CONF_BROAN_ID): cv.use_id(BroanComponent),
        cv.Optional(CONF_POWER): sensor.sensor_schema(
            device_class=DEVICE_CLASS_POWER,
            icon=ICON_POWER,
            unit_of_measurement=UNIT_WATT,
        ),
        cv.Optional(CONF_TEMPERATURE): sensor.sensor_schema(
            device_class=DEVICE_CLASS_TEMPERATURE,
            icon=ICON_THERMOMETER,
            unit_of_measurement=UNIT_CELSIUS,
        ),
        cv.Optional(CONF_TEMPERATURE_OUT): sensor.sensor_schema(
            device_class=DEVICE_CLASS_TEMPERATURE,
            icon=ICON_THERMOMETER,
            unit_of_measurement=UNIT_CELSIUS,
        ),
        # Indoor temperature/humidity. The ERV expects these on the bus, but once the
        # ESP32 is the sole controller there's no physical wall controller left to
        # supply them - you provide them yourself (a Home Assistant sensor, a probe
        # wired to the ESP32, etc) via setCurrentTemperature()/setCurrentHumidity().
        # These sensors simply republish whatever value you fed in, so it's visible/
        # graphable in HA without a second source of truth.
        cv.Optional(CONF_INDOOR_TEMPERATURE): sensor.sensor_schema(
            device_class=DEVICE_CLASS_TEMPERATURE,
            icon=ICON_THERMOMETER,
            unit_of_measurement=UNIT_CELSIUS,
        ),
        cv.Optional(CONF_INDOOR_HUMIDITY): sensor.sensor_schema(
            device_class=DEVICE_CLASS_HUMIDITY,
            icon=ICON_WATER,
            unit_of_measurement=UNIT_PERCENT,
        ),
        cv.Optional(CONF_FILTER_LIFE): sensor.sensor_schema(
            icon=ICON_AIR_FILTER,
            unit_of_measurement=UNIT_DAY,
        ),
        cv.Optional(CONF_SUPPLY_CFM): sensor.sensor_schema(
            icon=ICON_FAN,
            unit_of_measurement=UNIT_CFM,
        ),
        cv.Optional(CONF_EXHAUST_CFM): sensor.sensor_schema(
            icon=ICON_FAN,
            unit_of_measurement=UNIT_CFM,
        ),
        cv.Optional(CONF_SUPPLY_RPM): sensor.sensor_schema(
            icon=ICON_FAN,
            unit_of_measurement=UNIT_RPM,
        ),
        cv.Optional(CONF_EXHAUST_RPM): sensor.sensor_schema(
            icon=ICON_FAN,
            unit_of_measurement=UNIT_RPM,
        ),
        # Minutes remaining on the current Turbo boost. Reads 0 when Turbo isn't active.
        cv.Optional(CONF_TURBO_REMAINING): sensor.sensor_schema(
            icon=ICON_TIMER,
            unit_of_measurement=UNIT_MINUTE,
        ),
        # Minutes remaining on the current Ovr (bathroom boost) timer. Reads 0 when
        # Ovr isn't active.
        cv.Optional(CONF_OVERRIDE_REMAINING): sensor.sensor_schema(
            icon=ICON_TIMER,
            unit_of_measurement=UNIT_MINUTE,
        ),
    }
)

async def to_code(config):
    broan_component = await cg.get_variable(config[CONF_BROAN_ID])
    if power_config := config.get(CONF_POWER):
        sens = await sensor.new_sensor(power_config)
        cg.add(broan_component.set_power_sensor(sens))

    if temperature_config := config.get(CONF_TEMPERATURE):
        sens = await sensor.new_sensor(temperature_config)
        cg.add(broan_component.set_temperature_sensor(sens))

    if temperature_out_config := config.get(CONF_TEMPERATURE_OUT):
        sens = await sensor.new_sensor(temperature_out_config)
        cg.add(broan_component.set_temperature_out_sensor(sens))

    if indoor_temperature_config := config.get(CONF_INDOOR_TEMPERATURE):
        sens = await sensor.new_sensor(indoor_temperature_config)
        cg.add(broan_component.set_indoor_temperature_sensor(sens))

    if indoor_humidity_config := config.get(CONF_INDOOR_HUMIDITY):
        sens = await sensor.new_sensor(indoor_humidity_config)
        cg.add(broan_component.set_indoor_humidity_sensor(sens))

    if filter_life_config := config.get(CONF_FILTER_LIFE):
        sens = await sensor.new_sensor(filter_life_config)
        cg.add(broan_component.set_filter_life_sensor(sens))

    if supply_cfm_config := config.get(CONF_SUPPLY_CFM):
        sens = await sensor.new_sensor(supply_cfm_config)
        cg.add(broan_component.set_supply_cfm_sensor(sens))

    if exhaust_cfm_config := config.get(CONF_EXHAUST_CFM):
        sens = await sensor.new_sensor(exhaust_cfm_config)
        cg.add(broan_component.set_exhaust_cfm_sensor(sens))

    if supply_rpm_config := config.get(CONF_SUPPLY_RPM):
        sens = await sensor.new_sensor(supply_rpm_config)
        cg.add(broan_component.set_supply_rpm_sensor(sens))

    if exhaust_rpm_config := config.get(CONF_EXHAUST_RPM):
        sens = await sensor.new_sensor(exhaust_rpm_config)
        cg.add(broan_component.set_exhaust_rpm_sensor(sens))

    if turbo_remaining_config := config.get(CONF_TURBO_REMAINING):
        sens = await sensor.new_sensor(turbo_remaining_config)
        cg.add(broan_component.set_turbo_remaining_sensor(sens))

    if override_remaining_config := config.get(CONF_OVERRIDE_REMAINING):
        sens = await sensor.new_sensor(override_remaining_config)
        cg.add(broan_component.set_override_remaining_sensor(sens))
