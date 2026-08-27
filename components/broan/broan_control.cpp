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

// Sends new filter life to ERV in three steps (to mimic wall controller)
// 1. Write new filter life with FilterLifeStage (09:30)
// 2. Write FilterReset=1 + filter life in (08:30)
// 3. Write new filter life again, alone
// Not sur why new filter life needs to be repeated three times
// and if all this is necessary.
void BroanComponent::setFilterLife( uint32_t days )
{
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

// Public bridge for filter reset
void BroanComponent::applyFilterLifeReset()
{
	float months = 3.f;

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

	// A single write frame containing TurboDuration followed by FanMode=Turbo.
	vecFields.push_back( m_vecFields[TurboDuration].copyForUpdate( seconds ) );
	vecFields.push_back( m_vecFields[FanMode].copyForUpdate( (uint8_t)BroanFanMode::Turbo ) );

	m_vecFields[TurboDuration].markDirty();
	m_vecFields[FanMode].markDirty();

	writeRegisters( vecFields );
}

void BroanComponent::setTurbo( bool enable )
{
	if( enable )
	{
		float flMinutes = 60.f; // fallback duration if the number is not configured in YAML

#ifdef USE_NUMBER
		if( turbo_duration_number_ )
			flMinutes = turbo_duration_number_->state;
#endif

		if( flMinutes <= 0.f )
		{
			ESP_LOGW("broan_control", "Cannot start turbo: duration is 0 minutes. Set turbo_duration first.");
#ifdef USE_SWITCH
			// Switch off if duration = 0
			if( turbo_switch_ )
				turbo_switch_->publish_state( false );
#endif
			return;
		}
		setTurboDuration( (uint32_t)( flMinutes * 60.f ) );
	}
	else
	{
		// Reads back base mode in 02:20
		// and writes it in fan mode on 00:20
		uint8_t baseMode = m_vecFields[BaseMode].m_value.m_chValue;

		ESP_LOGI("broan_control", "Cancel override: returning to base mode %02X", baseMode);

		std::vector<BroanField_t> vecFields;
		vecFields.push_back( m_vecFields[FanMode].copyForUpdate( baseMode ) );
		m_vecFields[FanMode].markDirty();
		writeRegisters( vecFields );
	}
}

// This function allows the user to select between:
// a. ESP32 controls the ERV through HA. The wall controller must therefore be disconnected.
// b. Wall controller controls the ERV. The ESP32 listen the traffic on the bus but stays quiet.
// On/off of wall controller can easily be controlled with a GPIO/relay on the board.
void BroanComponent::setListenOnly( bool enable )
{
	ESP_LOGI("broan_control", "Set listen only: %s", enable ? "ON" : "OFF");
	m_bListenOnly = enable;

#ifdef USE_SENSOR
	// In listenOnly mode, fan speeds are not published by the ERV
	// so we show unavialable.
	if( enable )
	{
		if( supply_cfm_sensor_ ) supply_cfm_sensor_->publish_state(NAN);
		if( exhaust_cfm_sensor_ ) exhaust_cfm_sensor_->publish_state(NAN);
		if( supply_rpm_sensor_ ) supply_rpm_sensor_->publish_state(NAN);
		if( exhaust_rpm_sensor_ ) exhaust_rpm_sensor_->publish_state(NAN);
	}
	// When returning control to ERV, immediately update humidity and temperature
	// with HA sensor values rather than wait 20.3 seconds.
	else
	{
		if( m_bHaveHumidity )
			setCurrentHumidity( m_flLastHumidity );
		if( m_bHaveTemperature )
			setCurrentTemperature( m_flLastTemperature );
	}
#endif
}

}  // namespace broan
}  // namespace esphome
