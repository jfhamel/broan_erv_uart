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
		value = BroanFanMode::Recirculate; // Max par défaut, ajusté ci-dessous si recirculation_speed est déjà réglée
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

	// Applique tout de suite la vitesse déjà affichée dans HA plutôt que de
	// laisser l'ERV sur une cible potentiellement périmée d'une session
	// précédente. m_eSpeedFamily étant déjà à jour ci-dessus (synchrone), les
	// garde-fous de setFanSpeed()/setRecirculationSpeed() laissent passer ces
	// appels normalement.
#ifdef USE_NUMBER
	if( mode == "exchange" && fan_speed_number_ )
		setFanSpeed( fan_speed_number_->state );
	else if( mode == "recirculation" && recirculation_speed_number_ )
		setRecirculationSpeed( recirculation_speed_number_->state );
#endif
}

void BroanComponent::setFanSpeed( float input )
{
	// Continuous exchange (Manual/0x0B): confirmed by capture. The physical wall
	// controller never offers this, but the ERV honors any CFM target you write
	// into 06:22/08:22 while sitting in that mode.
	if( m_eSpeedFamily == BroanFanMode::Manual )
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

	ESP_LOGW("broan","setFanSpeed() only applies in 'exchange' mode. Use recirculation_speed for recirculation.");
}

void BroanComponent::setRecirculationSpeed( float percent )
{
	// Découplé du changement de mode: n'agit sur le bus que si on est déjà en
	// recirculation (comme setFanSpeed() pour l'échange). Sinon, seule la valeur
	// HA (publish_state, fait par RecirculationSpeedNumber::control()) est mise à
	// jour - rien n'est envoyé à l'ERV, et le mode ne change pas.
	// Note: on vérifie m_eSpeedFamily (mis à jour de façon synchrone dans
	// setFanMode()) plutôt que m_vecFields[FanMode] directement, qui lui ne se
	// met à jour qu'après relecture du bus - sinon, appeler cette fonction juste
	// après un changement de mode verrait encore l'ancienne valeur.
	if( m_eSpeedFamily != BroanFanMode::Recirculate )
	{
		ESP_LOGD("broan_control", "recirculation_speed changed while not in recirculation - not sent to the ERV");
		return;
	}

	// CONFIRMED BY CAPTURE: writing a custom CFM target while in recirculation has
	// no effect on real airflow - the ERV stays pinned wherever it was regardless
	// of the value sent. Recirculation genuinely only has these three fixed steps.
	// This takes a 0-100% input (matching the number's 3 fixed stops: 0/50/100)
	// and maps it to the nearest of the three.
	uint8_t value = BroanFanMode::Recirculate; // max

	if( percent < 33.f )
		value = BroanFanMode::RecirculateMin;
	else if( percent < 67.f )
		value = BroanFanMode::RecirculateMed;

	std::vector<BroanField_t> vecFields;
	vecFields.push_back( m_vecFields[FanMode].copyForUpdate( value ) );
	m_vecFields[FanMode].markDirty();
	writeRegisters( vecFields );
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

	// Mémorise pour la rediffusion périodique (voir runTasks()) et repousse le
	// prochain envoi automatique puisqu'on vient d'en faire un.
	m_flLastHumidity = humidity;
	m_bHaveHumidity = true;
	m_unLastEnvironmentBroadcast = millis();

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

	m_flLastTemperature = temperature;
	m_bHaveTemperature = true;
	m_unLastEnvironmentBroadcast = millis();

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

	setTurboDuration( (uint32_t)( flMinutes * 60.f ) );
}

}  // namespace broan
}  // namespace esphome
