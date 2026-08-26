#include "broan.h"

namespace esphome {
namespace broan {

void BroanComponent::setFanMode( std::string mode )
{
	uint8_t value = 0x01;

	// Basic selectable modes to mimic the wall controller options for HA.
	// Turbo and humiditstat are modes that apply over these modes with switched in HA.
	// Override is settable by the physical swicth, no interface here.
	// exchange_adjustable does not exist on the wall controller but
	// left avilable in HA as an interesting feature.
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
		value = BroanFanMode::RecirculateMax;
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

	// If exchange_adjustable is selected, fan speed is adjusted.
#ifdef USE_NUMBER
	if( bAdjustable && fan_speed_number_ )
		setFanSpeed( fan_speed_number_->state );
#endif
}

void BroanComponent::setFanSpeed( float input )
{
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

void BroanComponent::setFilterLife( uint32_t days )
{
	// Two-message sequence confirmed by capture (2026-08-19), matching exactly
	// what the wall controller does: stage the desired value in FilterLifeStage
	// (09:30) FIRST, THEN trigger FilterReset (01:30=1) combined with FilterLife
	// (08:30) in a second message. A single combined write (what an earlier
	// version of this did) was accepted on the wire but silently had no effect -
	// the ERV appears to need the staged value present before it'll apply
	// anything other than its own default.
	uint32_t unNewFilterLife = days * 24u * 60u * 60u;

	ESP_LOGI("broan_control", "Set filter life to %u days (%u s)", days, unNewFilterLife);

	std::vector<BroanField_t> vecStage;
	vecStage.push_back( m_vecFields[FilterLifeStage].copyForUpdate( unNewFilterLife ) );
	m_vecFields[FilterLifeStage].markDirty();
	writeRegisters( vecStage );

	std::vector<BroanField_t> vecApply;
	vecApply.push_back( m_vecFields[FilterReset].copyForUpdate( (uint8_t)1 ) );
	vecApply.push_back( m_vecFields[FilterLife].copyForUpdate( unNewFilterLife ) );
	m_vecFields[FilterReset].markDirty();
	m_vecFields[FilterLife].markDirty();
	writeRegisters( vecApply );
}

void BroanComponent::applyFilterLifeReset()
{
	// Public bridge for FilterLifeResetButton::press_action(): the pointer
	// itself (filter_life_reset_duration_number_) is protected (see SUB_NUMBER
	// in broan.h), so an external entity class can't read ->state directly -
	// this method does it from inside BroanComponent, which has access.
	float months = 3.f; // fallback if the number isn't configured in YAML

#ifdef USE_NUMBER
	if( filter_life_reset_duration_number_ )
		months = filter_life_reset_duration_number_->state;
#endif

	setFilterLife( (uint32_t)( months * 30.f ) );
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
	
	// Remember last humidity to republish every 20.3 secondes.
	m_flLastHumidity = humidity;
	m_bHaveHumidity = true;

	// When listenOnly, wall controller already sends humidity to ERV. Nothing else to do.
	if( m_bListenOnly )
		return;

	std::vector<BroanField_t> vecFields;

	ESP_LOGI("broan_control", "Set current humidity: %0.1f%%", humidity);

	vecFields.push_back( m_vecFields[ControllerHumidity].copyForUpdate( humidity ) );
	m_vecFields[ControllerHumidity].markDirty();

	writeRegisters( vecFields );

	// Remember last humidity update time.
	m_unLastEnvironmentBroadcast = millis();

#ifdef USE_SENSOR
	if( indoor_humidity_sensor_ )
		indoor_humidity_sensor_->publish_state( humidity );
#endif
}

void BroanComponent::setCurrentTemperature( float temperature ) {

	// Remember last temperature to republish every 20.3 secondes.
	m_flLastTemperature = temperature;
	m_bHaveTemperature = true;

	// When listenOnly, wall controller already sends temperature to ERV. Nothing else to do.
	if( m_bListenOnly )
		return;

	std::vector<BroanField_t> vecFields;

	ESP_LOGI("broan_control", "Set current temperature: %0.1f C", temperature);

	vecFields.push_back( m_vecFields[ControllerTemperature].copyForUpdate( temperature ) );
	m_vecFields[ControllerTemperature].markDirty();

	writeRegisters( vecFields );

	// Remember last temprature update time.
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
