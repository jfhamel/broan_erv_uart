#include "broan.h"

namespace esphome {
namespace broan {

void BroanComponent::setFanMode( std::string mode )
{
	uint8_t value = 0x01;

	// Top-level modes exposed to Home Assistant. "off"/"smart"/"intermittent"/"absence"
	// are simple, single-register writes. Turbo is deliberately NOT selectable here: the
	// real wall controller always writes TurboDuration + FanMode together in one frame
	// (see setTurboDuration()), and we've never captured a plain "turbo, no duration"
	// write, so we don't emulate one.
	//
	// Exchange and recirculation both expose their min/med/max steps directly rather
	// than a single mode + a speed number: confirmed by capture that ExchangeMin/Max
	// (0x09/0x0A) don't accept an adjustable speed at all (same as recirculation), only
	// ExchangeMedManual (0x0B) does - and even then, only when "exchange_adjustable" was
	// explicitly picked (see m_bAdjustableSpeed below), not for the plain "exchange_med".
	bool bAdjustable = false;

	if( mode == "smart" )
		value = BroanFanMode::Smart;
	else if( mode == "intermittent" )
		value = BroanFanMode::Intermittent;
	else if( mode == "exchange_min" )
		value = BroanFanMode::ExchangeMin;
	else if( mode == "exchange_max" )
		value = BroanFanMode::ExchangeMax;
	else if( mode == "exchange_med" )
		value = BroanFanMode::ExchangeMedManual;
	else if( mode == "exchange_adjustable" )
	{
		value = BroanFanMode::ExchangeMedManual;
		bAdjustable = true;
	}
	else if( mode == "recirculation_min" )
		value = BroanFanMode::RecirculateMin;
	else if( mode == "recirculation_med" )
		value = BroanFanMode::RecirculateMed;
	else if( mode == "recirculation_max" )
		value = BroanFanMode::Recirculate;
	else if( mode == "absence" )
		value = BroanFanMode::Away;
	else
		value = BroanFanMode::Off;

	m_bAdjustableSpeed = bAdjustable;

	ESP_LOGI("broan_control", "Set fan mode: %s (%02X)", mode.c_str(), value);

	std::vector<BroanField_t> vecFields;
	vecFields.push_back( m_vecFields[FanMode].copyForUpdate( value ) );

	m_vecFields[FanMode].markDirty();

	writeRegisters( vecFields );

	// "exchange_adjustable" applies the speed already shown in HA right away, rather
	// than leaving the ERV on a possibly stale target from a previous session.
	// m_bAdjustableSpeed is already up to date above (synchronous), so setFanSpeed()'s
	// guard lets this call through normally.
#ifdef USE_NUMBER
	if( bAdjustable && fan_speed_number_ )
		setFanSpeed( fan_speed_number_->state );
#endif
}

void BroanComponent::setFanSpeed( float input )
{
	// Only applies when "exchange_adjustable" was explicitly selected (see setFanMode()).
	// ExchangeMin/Max and the plain "exchange_med" all confirmed by capture to ignore a
	// custom CFM target - same as recirculation, which never had adjustable speed either
	// and is now exposed as three direct steps instead of a number.
	if( !m_bAdjustableSpeed )
	{
		ESP_LOGW("broan","setFanSpeed() only applies when 'exchange_adjustable' is selected");
		return;
	}

	float flMin = m_vecFields[CFMIn_Min].m_value.m_flValue;
	float flMax = m_vecFields[CFMIn_Max].m_value.m_flValue;
	if( flMin == 0 || flMax == 0 )
	{
		ESP_LOGE("broan","Failed to set fan speed: Invalid min/max state");
		return;
	}
	float value = remap( input, 0.f, 100.f, flMin, flMax );

	ESP_LOGI("broan_control", "Set fan speed (exchange_adjustable): %.0f%% -> %.1f CFM", input, value);

	std::vector<BroanField_t> vecFields;

	vecFields.push_back( m_vecFields[CFMIn_Medium].copyForUpdate( value ) );
	vecFields.push_back( m_vecFields[CFMOut_Medium].copyForUpdate( value ) );

	m_vecFields[CFMIn_Medium].markDirty();
	m_vecFields[CFMOut_Medium].markDirty();

	writeRegisters( vecFields );
}


void BroanComponent::setFanSpeedCFM( BroanFanMode mode, BroanCFMMode direction, float flTargetCFM )
{
	std::vector<BroanField_t> vecFields;

	ESP_LOGI("broan_control", "Set fan speed CFM limit: mode %02X, direction %02X, %.1f CFM", mode, direction, flTargetCFM);

	switch( mode )
	{
		case BroanFanMode::ExchangeMax:
		{
			if( ( direction & BroanCFMMode::Input ) != 0 )
				vecFields.push_back( m_vecFields[CFMIn_Max].copyForUpdate( flTargetCFM ) );
			if( ( direction & BroanCFMMode::Output ) != 0 )
				vecFields.push_back( m_vecFields[CFMOut_Max].copyForUpdate( flTargetCFM ) );
		}
		break;

		case BroanFanMode::ExchangeMin:
		{
			if( ( direction & BroanCFMMode::Input ) != 0 )
				vecFields.push_back( m_vecFields[CFMIn_Max].copyForUpdate( flTargetCFM ) );
			if( ( direction & BroanCFMMode::Output ) != 0 )
				vecFields.push_back( m_vecFields[CFMOut_Max].copyForUpdate( flTargetCFM ) );
		}
		break;


		default:
			ESP_LOGW("broan","Unhandled: Setting fan speed limits for  mode %02X", mode );

	}

	writeRegisters( vecFields );
}

void BroanComponent::resetFilter()
{
	std::vector<BroanField_t> vecFields;

	uint32_t unNewFilterLife = FILTER_LIFE_MAX;
	uint8_t unFilterReset = 1;

	ESP_LOGI("broan_control", "Reset filter life to %u s", unNewFilterLife);

	vecFields.push_back( m_vecFields[FilterLife].copyForUpdate( unNewFilterLife ) );
	vecFields.push_back( m_vecFields[FilterReset].copyForUpdate( unFilterReset ) );

	m_vecFields[FilterReset].markDirty();
	m_vecFields[FilterLife].markDirty();

	writeRegisters( vecFields );
}

void BroanComponent::setFilterLife( uint32_t days )
{
	// Same mechanism as resetFilter() (fixed 3-month/FILTER_LIFE_MAX reset), just
	// with an arbitrary day count instead - e.g. for a filter with a different
	// rated lifespan than Broan's own default, or to set a specific remaining
	// value rather than a full reset.
	std::vector<BroanField_t> vecFields;

	uint32_t unNewFilterLife = days * 24u * 60u * 60u;
	uint8_t unFilterReset = 1;

	ESP_LOGI("broan_control", "Set filter life to %u days (%u s)", days, unNewFilterLife);

	vecFields.push_back( m_vecFields[FilterLife].copyForUpdate( unNewFilterLife ) );
	vecFields.push_back( m_vecFields[FilterReset].copyForUpdate( unFilterReset ) );

	m_vecFields[FilterReset].markDirty();
	m_vecFields[FilterLife].markDirty();

	writeRegisters( vecFields );
}

void BroanComponent::setHumidityControl( bool enable ) {
	std::vector<BroanField_t> vecFields;

	uint8_t value = 0;

	if (enable) {
		value = 0x01;
	}

	ESP_LOGI("broan_control", "Set humidity control: %s", enable ? "ON" : "OFF");

	vecFields.push_back( m_vecFields[HumidityControl].copyForUpdate( value ) );

	m_vecFields[HumidityControl].markDirty();

	writeRegisters( vecFields );
}

void BroanComponent::setHumiditySetpoint( float humidity ) {
	std::vector<BroanField_t> vecFields;

	ESP_LOGI("broan_control", "Set humidity setpoint: %0.1f%%", humidity);

	vecFields.push_back( m_vecFields[TargetHumidityA].copyForUpdate( humidity ) );
	vecFields.push_back( m_vecFields[TargetHumidityB].copyForUpdate( humidity ) );

	m_vecFields[TargetHumidityA].markDirty();
	m_vecFields[TargetHumidityB].markDirty();

	writeRegisters( vecFields );
}

void BroanComponent::setCurrentHumidity( float humidity ) {
	// Remembered regardless of mode, so the freshest HA-sourced value is ready
	// to restore the moment we're back in command mode (see setListenOnly()).
	m_flLastHumidity = humidity;
	m_bHaveHumidity = true;

	// While listening in on a real wall controller, it's the source of truth
	// right now - writing our own HA source sensor's value here (to the wire
	// or even just the local indoor_humidity display) would fight with what
	// it's actually broadcasting. Confirmed by capture: without this guard,
	// indoor_humidity visibly spikes to our value the instant the HA sensor
	// changes, then gets corrected back down moments later once the wall
	// controller's own broadcast is overheard again (see the
	// ControllerHumidity case in parseBroanFields()).
	if( m_bListenOnly )
		return;

	std::vector<BroanField_t> vecFields;

	ESP_LOGI("broan_control", "Set current humidity: %0.1f%%", humidity);

	vecFields.push_back( m_vecFields[ControllerHumidity].copyForUpdate( humidity ) );
	m_vecFields[ControllerHumidity].markDirty();

	writeRegisters( vecFields );

	// Pushes the next automatic re-broadcast back out, since we just did one now.
	m_unLastEnvironmentBroadcast = millis();

	// We already have the value in hand - no need to wait for a read-back that will
	// never come (this register is write-only on the wire).
#ifdef USE_SENSOR
	if( indoor_humidity_sensor_ )
		indoor_humidity_sensor_->publish_state( humidity );
#endif
}

void BroanComponent::setCurrentTemperature( float temperature ) {
	m_flLastTemperature = temperature;
	m_bHaveTemperature = true;

	// Same reasoning as setCurrentHumidity() above.
	if( m_bListenOnly )
		return;

	std::vector<BroanField_t> vecFields;

	ESP_LOGI("broan_control", "Set current temperature: %0.1f C", temperature);

	vecFields.push_back( m_vecFields[ControllerTemperature].copyForUpdate( temperature ) );
	m_vecFields[ControllerTemperature].markDirty();

	writeRegisters( vecFields );

	m_unLastEnvironmentBroadcast = millis();

#ifdef USE_SENSOR
	if( indoor_temperature_sensor_ )
		indoor_temperature_sensor_->publish_state( temperature );
#endif
}

void BroanComponent::setIntermittentPeriod( uint32_t period ) {
	std::vector<BroanField_t> vecFields;

	ESP_LOGI("broan_control", "Set int period: %u s", period);

	vecFields.push_back( m_vecFields[IntModeDuration].copyForUpdate( period ) );
	m_vecFields[IntModeDuration].markDirty();

	writeRegisters( vecFields );
}

void BroanComponent::setTurboDuration( uint32_t seconds ) {
	std::vector<BroanField_t> vecFields;

	ESP_LOGI("broan_control", "Set turbo duration: %u s", seconds);

	// Matches the exact wire format captured from the wall controller for 1h/2h/4h:
	// a single write frame containing TurboDuration followed by FanMode=Turbo.
	vecFields.push_back( m_vecFields[TurboDuration].copyForUpdate( seconds ) );
	vecFields.push_back( m_vecFields[FanMode].copyForUpdate( (uint8_t)BroanFanMode::Turbo ) );

	m_vecFields[TurboDuration].markDirty();
	m_vecFields[FanMode].markDirty();

	writeRegisters( vecFields );
}

void BroanComponent::cancelOverride()
{
	// Reads back whatever base mode (02:20) the ERV is actually running right now
	// and writes it straight to FanMode (00:20) - exits Turbo/Absence/Humidity/Ovr
	// unconditionally, regardless of what fan_mode's displayed state currently
	// shows in HA. A select only fires when its displayed value changes; a button
	// always fires on every press, so this is the reliable way to back out of an
	// override when the dropdown is showing something stale.
	uint8_t baseMode = m_vecFields[BaseMode].m_value.m_chValue;

	ESP_LOGI("broan_control", "Cancel override: returning to base mode %02X", baseMode);

	std::vector<BroanField_t> vecFields;
	vecFields.push_back( m_vecFields[FanMode].copyForUpdate( baseMode ) );
	m_vecFields[FanMode].markDirty();
	writeRegisters( vecFields );
}

void BroanComponent::startTurbo()
{
	// Reads the desired duration directly from the turbo_duration number (minutes)
	// - no separate "pending duration" state needed, its ->state IS the value to use.
	float flMinutes = 60.f; // fallback if the number isn't configured in YAML

#ifdef USE_NUMBER
	if( turbo_duration_number_ )
		flMinutes = turbo_duration_number_->state;
#endif

	if( flMinutes <= 0.f )
	{
		ESP_LOGW("broan_control", "Cannot start turbo: duration is 0 minutes. Set turbo_duration first.");
#ifdef USE_SWITCH
		// Flip the switch back off since we're not actually starting anything -
		// otherwise it would show "on" for a turbo that never started.
		if( turbo_switch_ )
			turbo_switch_->publish_state( false );
#endif
		return;
	}

	// setTurboDuration() already logs the resulting action - no separate log here
	// to avoid a duplicate message right above it.
	setTurboDuration( (uint32_t)( flMinutes * 60.f ) );
}

void BroanComponent::setListenOnly( bool enable )
{
	ESP_LOGI("broan_control", "Set listen only: %s", enable ? "ON" : "OFF");
	m_bListenOnly = enable;

	// Confirmed by capture (2026-08-14): the physical wall controller's own
	// polling cycle never requests CFM/RPM at all (03:10/04:10/05:10/06:10) -
	// a completely different, much larger set of registers is used for whatever
	// its own display shows instead. While listening in on it, we'll never see
	// these updated, so mark them unavailable right away rather than leaving
	// stale values displayed indefinitely.
#ifdef USE_SENSOR
	if( enable )
	{
		if( supply_cfm_sensor_ ) supply_cfm_sensor_->publish_state(NAN);
		if( exhaust_cfm_sensor_ ) exhaust_cfm_sensor_->publish_state(NAN);
		if( supply_rpm_sensor_ ) supply_rpm_sensor_->publish_state(NAN);
		if( exhaust_rpm_sensor_ ) exhaust_rpm_sensor_->publish_state(NAN);
	}
	else
	{
		// Returning to ESP32 control: the ERV may still be holding whatever value
		// the wall controller last wrote to it (04:50/05:50), and indoor_temperature/
		// indoor_humidity in HA may still be showing it too - reclaim both right
		// away with the last genuine value we got from HA, rather than waiting for
		// the HA source sensor to happen to change again or for the next periodic
		// broadcast (up to ENVIRONMENT_BROADCAST_RATE later). Calls the full
		// setCurrentHumidity()/setCurrentTemperature() (not just publish_state())
		// so this actually writes fresh values to the ERV too, not just the local
		// HA-facing display - matters for Smart mode, which uses these internally.
		// m_flLastHumidity/m_flLastTemperature only ever reflect real HA-sourced
		// values, never the overheard wall controller ones - see the
		// ControllerHumidity/ControllerTemperature cases in parseBroanFields(),
		// which publish directly without touching these.
		if( m_bHaveHumidity )
			setCurrentHumidity( m_flLastHumidity );
		if( m_bHaveTemperature )
			setCurrentTemperature( m_flLastTemperature );
	}
#endif
}

}  // namespace broan
}  // namespace esphome
