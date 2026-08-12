#include "broan.h"

namespace esphome {
namespace broan {

void BroanComponent::setFanMode( std::string mode )
{
	uint8_t value = 0x01;

	// Top-level modes exposed to Home Assistant. "off"/"smart"/"exchange"/"recirculation"/"absence"
	// are simple, single-register writes. Turbo is deliberately NOT selectable here: the real wall
	// controller always writes TurboDuration + FanMode together in one frame (see setTurboDuration()),
	// and we've never captured a plain "turbo, no duration" write, so we don't emulate one.
	if( mode == "smart" )
		value = BroanFanMode::Smart;
	else if( mode == "intermittent" )
		value = BroanFanMode::Intermittent;
	else if( mode == "exchange" )
	{
		value = BroanFanMode::Manual;
		m_eSpeedFamily = BroanFanMode::Manual;
	}
	else if( mode == "recirculation" )
	{
		// Defaults to Max; use the fan_speed number to pick a level.
		value = BroanFanMode::Recirculate;
		m_eSpeedFamily = BroanFanMode::Recirculate;
	}
	else if( mode == "absence" )
		value = BroanFanMode::Away;
	else
		value = BroanFanMode::Off;


	std::vector<BroanField_t> vecFields;
	vecFields.push_back( m_vecFields[FanMode].copyForUpdate( value ) );

	m_vecFields[FanMode].markDirty();

	writeRegisters( vecFields );

}

void BroanComponent::setFanSpeed( float input )
{
	// Continuous exchange (Manual/0x0B) is confirmed by capture: the physical wall
	// controller never offers this, but the ERV honors any CFM target you write
	// into 06:22/08:22 while sitting in that mode.
	//
	// Recirculation (Recirculate/RecirculateMin/RecirculateMed) uses the exact same
	// mechanism here, on the assumption that 06:22/08:22 is a general "current speed
	// target" the ERV applies whenever you're in an adjustable tier, not something
	// tied specifically to continuous exchange. This has NOT been confirmed by
	// capture - verify with the supply/exhaust CFM sensors that real airflow
	// actually follows the slider while in recirculation. If it turns out the ERV
	// ignores this and stays pinned to the nearest fixed step, this will need to
	// go back to writing FanMode directly to one of the three discrete values.
	if( m_eSpeedFamily == BroanFanMode::Manual ||
	    m_eSpeedFamily == BroanFanMode::Recirculate ||
	    m_eSpeedFamily == BroanFanMode::RecirculateMin ||
	    m_eSpeedFamily == BroanFanMode::RecirculateMed )
	{
		float flMin = m_vecFields[CFMIn_Min].m_value.m_flValue;
		float flMax = m_vecFields[CFMIn_Max].m_value.m_flValue;
		if( flMin == 0 || flMax == 0 )
		{
			ESP_LOGE("broan","Failed to set fan speed: Invalid min/max state");
			return;
		}
		float value = remap( input, 0.f, 100.f, flMin, flMax );

		std::vector<BroanField_t> vecFields;

		vecFields.push_back( m_vecFields[CFMIn_Medium].copyForUpdate( value ) );
		vecFields.push_back( m_vecFields[CFMOut_Medium].copyForUpdate( value ) );

		m_vecFields[CFMIn_Medium].markDirty();
		m_vecFields[CFMOut_Medium].markDirty();

		writeRegisters( vecFields );
		return;
	}

	ESP_LOGW("broan","setFanSpeed() only applies in 'exchange' or 'recirculation' modes");
}


void BroanComponent::setFanSpeedCFM( BroanFanMode mode, BroanCFMMode direction, float flTargetCFM )
{
	std::vector<BroanField_t> vecFields;


	switch( mode )
	{
		case BroanFanMode::Max:
		{
			if( ( direction & BroanCFMMode::Input ) != 0 )
				vecFields.push_back( m_vecFields[CFMIn_Max].copyForUpdate( flTargetCFM ) );
			if( ( direction & BroanCFMMode::Output ) != 0 )
				vecFields.push_back( m_vecFields[CFMOut_Max].copyForUpdate( flTargetCFM ) );
		}
		break;

		case BroanFanMode::Min:
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
	uint8_t unFilterReset = 0;

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

	vecFields.push_back( m_vecFields[HumidityControl].copyForUpdate( value ) );

	m_vecFields[HumidityControl].markDirty();

	writeRegisters( vecFields );
}

void BroanComponent::setHumiditySetpoint( float humidity ) {
	std::vector<BroanField_t> vecFields;

	vecFields.push_back( m_vecFields[TargetHumidityA].copyForUpdate( humidity ) );
	vecFields.push_back( m_vecFields[TargetHumidityB].copyForUpdate( humidity ) );

	m_vecFields[TargetHumidityA].markDirty();
	m_vecFields[TargetHumidityB].markDirty();

	writeRegisters( vecFields );
}

void BroanComponent::setCurrentHumidity( float humidity ) {
	std::vector<BroanField_t> vecFields;
  
	ESP_LOGI("broan_control", "Set current humidity: %0.1f%%", humidity);

	vecFields.push_back( m_vecFields[ControllerHumidity].copyForUpdate( humidity ) );
	m_vecFields[ControllerHumidity].markDirty();

	writeRegisters( vecFields );

	// We already have the value in hand - no need to wait for a read-back that will
	// never come (this register is write-only on the wire).
#ifdef USE_SENSOR
	if( indoor_humidity_sensor_ )
		indoor_humidity_sensor_->publish_state( humidity );
#endif
}

void BroanComponent::setCurrentTemperature( float temperature ) {
	std::vector<BroanField_t> vecFields;

	ESP_LOGI("broan_control", "Set current temperature: %0.1f C", temperature);

	vecFields.push_back( m_vecFields[ControllerTemperature].copyForUpdate( temperature ) );
	m_vecFields[ControllerTemperature].markDirty();

	writeRegisters( vecFields );

#ifdef USE_SENSOR
	if( indoor_temperature_sensor_ )
		indoor_temperature_sensor_->publish_state( temperature );
#endif
}

void BroanComponent::setIntermittentPeriod( uint32_t period ) {
	std::vector<BroanField_t> vecFields;

	// S -> MS
	//period *= 1000;
  
	ESP_LOGI("broan_control", "Set int period: %i", period);

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

}  // namespace broan
}  // namespace esphome
