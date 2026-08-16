#include "broan.h"

namespace esphome {
namespace broan { // Change 'broan' to match your component name


void BroanComponent::setup()
{
	//uart::UARTDevice::setup();
	Component::setup();
	//esp_log_level_set("broan", ESP_LOG_DEBUG);

	m_vecHeader.reserve(5);

	for( int i=0; i<BroanField::MAX_FIELDS; i++ )
		m_vecFields[i].markDirty();

	// Avoids a false bus-timeout trigger on the very first boot, before we've
	// even had a chance to exchange anything with the ERV.
	m_unLastValidResponse = millis();

  	if(flow_control_pin_)
    	this->flow_control_pin_->setup();
}


void BroanComponent::loop()
{
	while ( true )
	{
		if( !readHeader() ) break;
		bool bRead = readMessage();
		if( !bRead ) break;
	}

	replyIfAllowed();

	runTasks();
}

void BroanComponent::dump_config()
{
	ESP_LOGCONFIG("broan", "Broan:");
	if(flow_control_pin_)
	{
		char buffer[255];
		this->flow_control_pin_->dump_summary( buffer, 255 );
		ESP_LOGCONFIG("broan", "Flow Control Pin: %s", buffer );
	}
}

float BroanComponent::get_setup_priority() const
{
  // After UART bus
  return setup_priority::BUS - 1.0f;
}

bool BroanComponent::readHeader()
{
	if( m_bHaveHeader )
	{
		//ESP_LOGD("broan", "Recycling header (good)");
		return true;
	}

	if( available() < 5 )
		return false;

	for (uint8_t i = 0; i < 5; i++) {
		m_vecHeader[i] = read();
		if( i == 0 && m_vecHeader[i] != 0x01 )
		{
			ESP_LOGW("broan", "Alignment: Unexpected %02X in position %i", m_vecHeader[i], i);
			return false;
		}

		if( i == 3 && m_vecHeader[i] != 0x01 )
		{
			ESP_LOGW("broan", "Alignment: Unexpected %02X in position %i", m_vecHeader[i], i);
			return false;
		}
	}

	uint8_t head = m_vecHeader[0];
	if ( m_vecHeader[1] > 32 || m_vecHeader[2] > 32 )
	{
		ESP_LOGW("broan", "Alignment: Unexpected %02X %02X %02X %02X %02X",
			m_vecHeader[0], m_vecHeader[1], m_vecHeader[2], m_vecHeader[3], m_vecHeader[4]);
		return false;
	}

	m_bHaveHeader = true;

	return true;
}

void BroanComponent::writeRegisters( const std::vector<BroanField_t> &values )
{
	std::vector<uint8_t> message;

	message.push_back(0x40); // Write

	for( BroanField_t value : values )
	{
		message.push_back( value.m_nOpcodeHigh );
		message.push_back( value.m_nOpcodeLow );
		uint8_t len = value.m_nType == BroanFieldType::Byte ? 0x01 : 0x04;
		message.push_back( len );
		for( int i=0; i<len; i++ )
			message.push_back( value.m_value.m_rgBytes[i] );
	}

	queueMessage( message );
}

bool BroanComponent::readMessage()
{
	uint8_t target = m_vecHeader[1];
	uint8_t sender = m_vecHeader[2];
	int len = m_vecHeader[4];

	if( !m_bHaveHeader )
		return false;

	if( available() < len + 2 )
	{
		//ESP_LOGD("broan", "Waiting for rest of packet to show up in buffer (Want %i have %i)", len + 2, available() );
		return false;
	}

	m_bHaveHeader = false;

	std::vector<uint8_t> message(len);

	for (uint8_t i = 0; i < len; i++)
	{
		if (!available())
		{
			ESP_LOGE("broan", "Exhausted ring buffer somehow");
			return false;
		}

		message[i] = read();
	}

	uint8_t checksum = read();
	uint8_t expected_checksum = calculateChecksum(sender, target, message);
	if (checksum != expected_checksum)
	{
		ESP_LOGE("broan", "Checksum mismatch: got %02X, expected %02X", checksum, expected_checksum);
		return false;
	}

	uint8_t footer = read();
	if (footer != 0x04)
	{
		ESP_LOGE("broan", "Missing 0x04 footer, incomplete read??");
		return false;
	}

	handleMessage(sender, target, message);

	return true;
}

void esp_log_vector_hex(const char* tag, const std::vector<uint8_t>& message) {
    if (message.empty()) {
        ESP_LOGW(tag, "Message vector is empty");
        return;
    }
    std::string hex_string;
    for (size_t i = 0; i < message.size(); ++i) {
        char buf[4];
        snprintf(buf, sizeof(buf), "%02X ", message[i]);
        hex_string += buf;
        // Optional: newline every 16 bytes for readability
        if ((i + 1) % 16 == 0) {
            ESP_LOGW(tag, "%s", hex_string.c_str());
            hex_string.clear();
        }
    }
    if (!hex_string.empty()) {
        ESP_LOGW(tag, "%s", hex_string.c_str());
    }
}

void BroanComponent::handleMessage(uint8_t sender, uint8_t target, const std::vector<uint8_t>& message)
{
	if( target == m_nServerAddress )
	{
		if( message[0] == 0x03 )
			m_bWaitForRemote = false;
	}
#ifndef LISTEN_ONLY
	if (target != m_nClientAddress) return;
#endif

	int m_nType = message[0];
	switch (m_nType)
	{
		case 0x02:
		{
			// Respond to ping
			std::vector<uint8_t> reply = {0x03};
			reply.insert(reply.end(), message.begin() + 1, message.end());

			send(reply);

			ESP_LOGD("broan","0x02 Ping");
			m_bERVReady = true;
			break;
		}
		case 0x04:
		{
			// Flow control
			m_nLastHadControl = millis();
			m_bHaveControl = true;
			m_bExpectingReply = false;
			// ERV won't re-ping us if we drop, so just assume if we're getting flow
			// control messages it's ready for us to start feeding it data.
			m_bERVReady = true;

			// Ack that we have control. We'll send any queued messages then release with 0x04
			send({ 0x05 });
			//ESP_LOGD("broan","Got flow control");
			break;
		}
		case 0x05:
			// ERV has confirmed it has control, no-op
			break;

		case 0x41:
		{
			// A genuine response from the ERV - it's still there.
			m_unLastValidResponse = millis();
			m_bBusTimedOut = false;

			// set register ACK, mark all fields dirty
			for( int i=1; i<message.size(); i+=2)
			{
				BroanField_t *pField = lookupField(message[i], message[i+1]);
				if( !pField )
				{
					ESP_LOGW("broan", "Got write response for unknown field %02X %02X", message[i], message[i+1]);
					continue;
				}
				pField->markDirty();
			}
			m_bExpectingReply = false;


			break;
		}
		case 0x21:
		{
			// A genuine response from the ERV - it's still there.
			m_unLastValidResponse = millis();
			m_bBusTimedOut = false;

			// Request register response
			parseBroanFields(message);
			m_bExpectingReply = false;

			break;
		}
#ifdef LISTEN_ONLY
		case 0x20:
			break;
#endif
		default:
		{
			// Log unhandled m_nType
			ESP_LOGW("broan", "Unhandled m_nType %02X", m_nType);
			esp_log_vector_hex("broan", message );
			break;
		}
	}
}

void BroanComponent::replyIfAllowed()
{
	uint32_t time = millis();
	if( m_nLastHadControl + CONTROL_TIMEOUT < time )
	{
		ESP_LOGW("broan","ERV has not yielded control in over %ims, communication has likely failed. Please restart the device.", CONTROL_TIMEOUT);
		m_bERVReady = false;
		m_nLastHadControl = time;
	}

	if( !m_bHaveControl || m_bExpectingReply )
		return;

	if( m_vecSendQueue.size() > 0 )
	{
		send( m_vecSendQueue.front() );
		m_vecSendQueue.pop_front();
		m_bExpectingReply = true;
		m_bHaveSentMessage = true;
		return;
	}

	if( m_bHaveControl && !m_bExpectingReply && m_vecSendQueue.size() == 0 )
	{
		// Release control.
		send( { 0x04 } );
		m_bHaveControl = false;
		m_bERVReady = true;
		m_bHaveSentMessage = false;
		return;
	}

}

void BroanComponent::queueMessage(std::vector<uint8_t>& message)
{
	if( m_vecSendQueue.size() > 20 )
	{
		ESP_LOGW("broan","Dropping queued message: Stack is full. (Tried to queue %02X)",message[0]);
		return;
	}
	m_vecSendQueue.push_back(message);
}


// Used for the fan_mode select's displayed state (register 00:20 / commanded mode).
// Keeps full granularity since this reflects exactly what was written to the ERV.
// Used for the fan_mode select's displayed state (register 00:20 / commanded mode).
// Note that ExchangeMedManual (0x0B) is ambiguous on the wire alone: "exchange_med"
// and "exchange_adjustable" both write this same byte, only m_bAdjustableSpeed (our
// own side, not recoverable from a bus read) tells them apart.
std::string BroanComponent::fanModeToString( uint8_t value )
{
	switch( value )
	{
		case BroanFanMode::Ovr: return "ovr";
		case BroanFanMode::Intermittent: return "intermittent";
		case BroanFanMode::ExchangeMin: return "exchange_min";
		case BroanFanMode::ExchangeMax: return "exchange_max";
		case BroanFanMode::ExchangeMedManual: return m_bAdjustableSpeed ? "exchange_adjustable" : "exchange_med";
		case BroanFanMode::Turbo: return "turbo";
		case BroanFanMode::Humidity: return "humidity";
		case BroanFanMode::Away: return "absence";
		case BroanFanMode::Smart: return "smart";
		case BroanFanMode::RecirculateMin: return "recirculation_min";
		case BroanFanMode::RecirculateMed: return "recirculation_med";
		case BroanFanMode::Recirculate: return "recirculation_max";
		default: return "off";
	}
}

// Used for the current_mode status text sensor. Full table confirmed by
// capture on 2026-08-13/14, tested across all modes/tiers (internal Smart,
// Turbo, Deshumidistat, Ovr, Absence, Intermittent - exchange and rest phases,
// Recirc Min/Med/Max):
//   00 = off                  04 = exchange (functionally identical to 01,
//   01 = exchange                  difference not understood at the hardware level)
//   02 = deshumidistat        05 = override (Ovr / bathroom boost)
//   03 = turbo                06 = recirculation min
//                             07 = recirculation max
//                             08 = recirculation medium
// current_mode deliberately simplifies 01/04 -> "exchange" and 06/07/08 ->
// "recirculation": it only reflects what the ERV is doing, not at which tier.
// No longer depends on FanMode (00:20) - 07:20 already encodes "off" (00) directly.
std::string BroanComponent::ventilationStateToString( uint8_t ventilationState )
{
	switch( ventilationState )
	{
		case 0x00: return "off";
		case 0x01:
		case 0x04: return "exchange";
		case 0x02: return "deshumidistat";
		case 0x03: return "turbo";
		case 0x05: return "override";
		case 0x06:
		case 0x07:
		case 0x08: return "recirculation";
		default: return "unknown";
	}
}

void BroanComponent::parseBroanFields(const std::vector<uint8_t>& message)
{
    size_t i = 1;
	bool bPublish = false;

    while (i < message.size())
    {
        uint8_t nOpcodeHigh = message[i++];
        uint8_t nOpcodeLow  = message[i++];
		size_t len = message[i++];
		uint32_t nDataPos = i;

		i += len;

		uint32_t unField = lookupFieldIndex(nOpcodeHigh, nOpcodeLow);
		if( unField == INVALID_FIELD )
			continue;

		BroanField_t *pField = &m_vecFields[unField];
		if( !pField )
		{
			handleUnknownField(nOpcodeHigh, nOpcodeLow, len, nDataPos, message);
			continue;
		}

		uint32_t oldVal = pField->m_value.m_nValue;
		for (size_t b = 0; b < len; ++b)
			pField->m_value.m_rgBytes[b] = static_cast<char>(message[nDataPos+b]);
	
		if( oldVal == pField->m_value.m_nValue )
			continue;

		// Keeps m_bAdjustableSpeed in sync when the ERV reverts to a different
		// FanMode on its own (Turbo timer expiring, Absence schedule, etc) - if
		// FanMode isn't ExchangeMedManual (0x0B) anymore, adjustable speed can't
		// apply regardless. If it IS 0x0B, leave the flag alone: that byte is
		// ambiguous on the wire (both "exchange_med" and "exchange_adjustable"
		// write it), so only our own last explicit selection (set synchronously in
		// setFanMode()) can tell them apart - a bus read can't recover that.
		// Kept outside the ifdefs below so it still works even if the
		// select/number platforms aren't used.
		if( unField == BroanField::FanMode )
		{
			uint8_t val = pField->m_value.m_chValue;
			if( val != BroanFanMode::ExchangeMedManual )
				m_bAdjustableSpeed = false;
		}

		switch(unField)
		{
			case BroanField::FanMode:
			{
				uint8_t val = pField->m_value.m_chValue;

#ifdef USE_SELECT
				// fan_mode's declared options don't include turbo/humidity/ovr -
				// those are exposed separately (turbo switch, override_state text
				// sensor). Publishing one of them here would be rejected by
				// select::publish_state() as an invalid option (logged as an
				// error) since it validates against the declared option list.
				// Simplest fix: only publish values that are actually selectable.
				if( fan_mode_select_ &&
				    val != BroanFanMode::Turbo &&
				    val != BroanFanMode::Humidity &&
				    val != BroanFanMode::Ovr )
					fan_mode_select_->publish_state( fanModeToString( val ) );
#endif

#ifdef USE_SWITCH
				// The turbo switch reflects reality: on while Turbo is the active
				// FanMode, off otherwise - including when the ERV reverts on its
				// own once the timer runs out.
				if( turbo_switch_ )
					turbo_switch_->publish_state( val == BroanFanMode::Turbo );
#endif

#ifdef USE_TEXT_SENSOR
				if( override_state_text_sensor_ )
				{
					std::string state = "none";
					if( val == BroanFanMode::Turbo ) state = "turbo";
					else if( val == BroanFanMode::Away ) state = "absence";
					else if( val == BroanFanMode::Humidity ) state = "humidity";
					else if( val == BroanFanMode::Ovr ) state = "ovr";
					override_state_text_sensor_->publish_state( state );
				}
#endif

#ifdef USE_SENSOR
				if( turbo_remaining_sensor_ )
					turbo_remaining_sensor_->publish_state( val == BroanFanMode::Turbo ? m_vecFields[TurboRemaining].m_value.m_nValue / 60.f : 0.f );

				if( override_remaining_sensor_ )
					override_remaining_sensor_->publish_state( val == BroanFanMode::Ovr ? m_vecFields[OvrRemaining].m_value.m_nValue / 60.f : 0.f );
#endif
			}
			break;
			case BroanField::VentilationState:
			{
				uint8_t ventState = pField->m_value.m_chValue;

#ifdef USE_TEXT_SENSOR
				if( current_mode_text_sensor_ )
					current_mode_text_sensor_->publish_state( ventilationStateToString( ventState ) );
#endif

#ifdef USE_SENSOR
				// Immediately re-publishes the outdoor temperature (see the
				// TemperatureIn case below) right at the moment the state changes,
				// rather than waiting up to 10s for the next read cycle of 01:E0
				// to reflect the switch.
				if( temperature_sensor_ )
				{
					if( ventState == BroanFanMode::Recirculate )
						temperature_sensor_->publish_state(NAN);
					else
						temperature_sensor_->publish_state( m_vecFields[TemperatureIn].m_value.m_flValue );
				}
#endif
			}
			break;
#ifdef USE_SENSOR
			case BroanField::Wattage:
				if( !power_sensor_ )
					continue;

				power_sensor_->publish_state(pField->m_value.m_flValue);
			break;

			case BroanField::FilterLife:
				if( !filter_life_sensor_ )
					continue;

				// Seconds -> Days
				filter_life_sensor_->publish_state(pField->m_value.m_nValue / ( 60 * 60 * 24 ) );
			break;

			case BroanField::TemperatureIn:
				if( !temperature_sensor_ )
					continue;

				// Recirculation: no outdoor air is admitted, so this sensor only
				// measures recycled/indoor air - misleading if shown as
				// "outdoor temperature". Publishes NAN (= unavailable in HA)
				// instead of this unrepresentative value.
				if( m_vecFields[VentilationState].m_value.m_chValue == BroanFanMode::Recirculate )
					temperature_sensor_->publish_state(NAN);
				else
					temperature_sensor_->publish_state(pField->m_value.m_flValue);
			break;

			case BroanField::SupplyCFM:
				if( !supply_cfm_sensor_ )
					continue;

				supply_cfm_sensor_->publish_state(pField->m_value.m_flValue);
			break;

			case BroanField::ExhaustCFM:
				if( !exhaust_cfm_sensor_ )
					continue;

				exhaust_cfm_sensor_->publish_state(pField->m_value.m_flValue);
			break;

			case BroanField::SupplyRPM:
				if( !supply_rpm_sensor_ )
					continue;

				supply_rpm_sensor_->publish_state(pField->m_value.m_flValue);
			break;

			case BroanField::ExhaustRPM:
				if( !exhaust_rpm_sensor_ )
					continue;

				exhaust_rpm_sensor_->publish_state(pField->m_value.m_flValue);
			break;
				
			case BroanField::TemperatureOut:
			{
				// @todo: We should stop querying NaN fields...
				if( !temperature_out_sensor_ || std::isnan( pField->m_value.m_flValue ) )
					continue;

				temperature_out_sensor_->publish_state(pField->m_value.m_flValue);		
			}
			break;

			case BroanField::TurboRemaining:
			{
				if( !turbo_remaining_sensor_ )
					continue;

				// Only publish if Turbo is the currently active override - avoids
				// pushing a stale/irrelevant countdown if something else is active.
				if( m_vecFields[FanMode].m_value.m_chValue == BroanFanMode::Turbo )
					turbo_remaining_sensor_->publish_state( pField->m_value.m_nValue / 60.f );
			}
			break;

			case BroanField::OvrRemaining:
			{
				if( !override_remaining_sensor_ )
					continue;

				if( m_vecFields[FanMode].m_value.m_chValue == BroanFanMode::Ovr )
					override_remaining_sensor_->publish_state( pField->m_value.m_nValue / 60.f );
			}
			break;

#endif	
#ifdef USE_NUMBER
			case BroanField::TargetHumidityA:
				if( !humidity_setpoint_number_ )
					continue;
				humidity_setpoint_number_->publish_state(pField->m_value.m_flValue);
			break;

			// @todo: We don't support unbalanced values here currently....
			case BroanField::CFMIn_Medium:
			{
				if( !fan_speed_number_ )
					continue;

				float flMin = m_vecFields[CFMIn_Min].m_value.m_flValue;
				float flMax = m_vecFields[CFMIn_Max].m_value.m_flValue;
				float flAdjusted = remap( pField->m_value.m_flValue, flMin, flMax, 0.f, 100.f );
				fan_speed_number_->publish_state(flAdjusted);
			}
			break;

			case BroanField::IntModeDuration:
				if( !intermittent_period_number_ )
					continue;

				// The register is in seconds on the wire, the number entity in minutes
				// (see IntermittentPeriodNumber::control() for the reverse
				// conversion at write time) - without this, the raw value (e.g. 600
				// for 10 minutes) would display as-is as if it were already in
				// minutes, visible on first boot before any interaction.
				intermittent_period_number_->publish_state(pField->m_value.m_nValue / 60.f);
			break;
#endif
#ifdef USE_SWITCH
			case BroanField::HumidityControl:
				if( !humidity_control_switch_ )
						continue;
				
				humidity_control_switch_->publish_state(pField->m_value.m_chValue==1);
			break;
	#endif
		}

		switch( pField->m_nType )
		{
			case BroanFieldType::Byte:
				ESP_LOGVV("broan","%02X%02X is now Byte %02X", nOpcodeHigh, nOpcodeLow, pField->m_value.m_chValue );
				break;
			case BroanFieldType::Int:
				ESP_LOGVV("broan","%02X%02X is now Int %i", nOpcodeHigh, nOpcodeLow, pField->m_value.m_nValue );
				break;
			case BroanFieldType::Float:
				ESP_LOGVV("broan","%02X%02X is now Float %f", nOpcodeHigh, nOpcodeLow, pField->m_value.m_flValue );
				break;
			case BroanFieldType::Void:
				ESP_LOGVV("broan","%02X%02X is not set", nOpcodeHigh, nOpcodeLow );
				break;
		}
    }

}

void BroanComponent::handleUnknownField(uint32_t nOpcodeHigh, uint32_t nOpcodeLow, uint8_t len, uint32_t i, const std::vector<uint8_t>& message )
{
#ifdef SCAN_UNKNOWN
	uint16_t kv = ( nOpcodeHigh << 8 ) | nOpcodeLow;
	if( m_vecFieldData.contains( kv ) )
	{
		BroanField_t copy = m_vecFieldData[ kv ];

		for (size_t b = 0; b < len && b < 4; ++b)
			m_vecFieldData[kv].m_value.m_rgBytes[b] = static_cast<char>(message[i+b]);

		if( m_vecFieldData[kv].m_value.m_nValue != copy.m_value.m_nValue )
		{


			if( len == 4)
				ESP_LOGVV("broan","%02X%02X field is unmapped. Value: %f / %i -->  %f / %i", nOpcodeHigh, nOpcodeLow,
					copy.m_value.m_flValue, copy.m_value.m_nValue,
					m_vecFieldData[kv].m_value.m_flValue, m_vecFieldData[kv].m_value.m_nValue ) ;
			else if (len == 1)
				ESP_LOGVV("broan","%02X%02X field is unmapped. Value: %f / %i -->  %f / %i", nOpcodeHigh, nOpcodeLow,
					copy.m_value.m_flValue, copy.m_value.m_nValue,
					m_vecFieldData[kv].m_value.m_flValue, m_vecFieldData[kv].m_value.m_nValue ) ;
		}
	}
	else
#endif
	{
		BroanField_t newField;
		newField.m_nOpcodeHigh = nOpcodeHigh;
		newField.m_nOpcodeLow = nOpcodeLow;
		newField.m_nType = len == 4 ? BroanFieldType::Float : BroanFieldType::Byte;

		for (size_t b = 0; b < len && b < 4; ++b)
			newField.m_value.m_rgBytes[b] = static_cast<char>(message[i+b]);


		if( len == 4)
			ESP_LOGVV("broan","%02X%02X field is unmapped. Value: %f / %i", nOpcodeHigh, nOpcodeLow, newField.m_value.m_flValue, newField.m_value.m_nValue );
		else if( len == 1 )
			ESP_LOGVV("broan","%02X%02X field is unmapped. Value: %i", nOpcodeHigh, nOpcodeLow, newField.m_value.m_chValue);
		else
			ESP_LOGVV("broan","%02X%02X has unhandled field length %i: %s", nOpcodeHigh, nOpcodeLow, len, format_hex_pretty(&message[i], len).c_str() );
#ifdef SCAN_UNKNOWN
		m_vecFieldData[kv] = newField;
#endif

	}

}

void BroanComponent::send(const std::vector<uint8_t>& vecMessage)
{
#ifndef LISTEN_ONLY
 	if(flow_control_pin_)
    	flow_control_pin_->digital_write(true);

	uint8_t header = 0x01;
	uint8_t alignment = 0x01;
	uint8_t footer = 0x04;
	write(header);
	write(m_nServerAddress);
	write(m_nClientAddress);
	write(alignment);
	write((uint8_t)vecMessage.size());
	for (auto b : vecMessage) write(b);
	write(calculateChecksum(m_nClientAddress, m_nServerAddress, vecMessage));
	write(footer);

	flush();

 	if(flow_control_pin_)
    	flow_control_pin_->digital_write(false);
#endif
}

uint8_t BroanComponent::calculateChecksum(uint8_t sender, uint8_t receiver, const std::vector<uint8_t>& message)
{
	uint8_t total = 0x01 + sender + receiver + 0x01 + message.size();
	for (uint8_t b : message) total += b;
	return 0xFF & (0 - (total - 1));
}

BroanField_t* BroanComponent::lookupField( uint8_t opcodeHigh, uint8_t opcodeLow )
{
	uint32_t unField = lookupFieldIndex( opcodeHigh, opcodeLow );
	if( unField != INVALID_FIELD )
	{
		return &m_vecFields[unField];
	}

	return nullptr;
}

uint32_t BroanComponent::lookupFieldIndex( uint8_t opcodeHigh, uint8_t opcodeLow )
{
	for( int i=0; i<BroanField::MAX_FIELDS; i++ )
	{
		BroanField_t *pField = &m_vecFields[i];
		if( pField->m_nOpcodeHigh == opcodeHigh && pField->m_nOpcodeLow == opcodeLow )
			return i;
	}

	return INVALID_FIELD;
}

void BroanComponent::runTasks()
{
	uint32_t time = millis();

	if( m_bERVReady )
	{
		//ESP_LOGD("broan", "Reading values" );

		std::vector<unsigned char> vecRequest;

		int nCount = 0;
		for( int i=0; i<BroanField::MAX_FIELDS && nCount < MAX_REQUEST_SIZE; i++ )
		{
			if( m_vecFields[i].m_unPollRate == UPDATE_RATE_NEVER || time - m_vecFields[i].m_unLastUpdate < m_vecFields[i].m_unPollRate )
				continue;

			nCount++;
			m_vecFields[i].m_unLastUpdate = time;

			if( vecRequest.size() == 0 )
				vecRequest.push_back(0x20);

			vecRequest.push_back( m_vecFields[i].m_nOpcodeHigh );
			vecRequest.push_back( m_vecFields[i].m_nOpcodeLow );
		}

		if( vecRequest.size() > 0 )
		{
			queueMessage(vecRequest);
		}
	}


	if( time - m_unLastHeartbeat > HEARTBEAT_RATE )
	{
		m_unLastHeartbeat = time;
		std::vector<unsigned char> vecRequest;
		vecRequest.push_back(0x40);
		vecRequest.push_back(0x00);
		vecRequest.push_back(0x50);
		vecRequest.push_back(0x00);

		queueMessage(vecRequest);
	}

	// Re-broadcasts humidity/temperature every ~20.3s even if the value hasn't
	// changed, as the physical wall controller does (confirmed by capture).
	// setCurrentHumidity()/setCurrentTemperature() already push this timer back
	// whenever a genuine update arrives - this only ensures the ERV keeps
	// getting a regular signal even if the HA source sensor doesn't change for a while.
	if( ( m_bHaveHumidity || m_bHaveTemperature ) && time - m_unLastEnvironmentBroadcast > ENVIRONMENT_BROADCAST_RATE )
	{
		m_unLastEnvironmentBroadcast = time;

		std::vector<BroanField_t> vecFields;

		if( m_bHaveHumidity )
		{
			vecFields.push_back( m_vecFields[ControllerHumidity].copyForUpdate( m_flLastHumidity ) );
			m_vecFields[ControllerHumidity].markDirty();
		}

		if( m_bHaveTemperature )
		{
			vecFields.push_back( m_vecFields[ControllerTemperature].copyForUpdate( m_flLastTemperature ) );
			m_vecFields[ControllerTemperature].markDirty();
		}

		writeRegisters( vecFields );
	}

	// No valid response from the ERV since BUS_TIMEOUT - publishes NAN on
	// numeric sensors instead of leaving the last known values displayed
	// indefinitely. m_bBusTimedOut avoids repeating this every loop iteration
	// while the situation remains unresolved.
	if( !m_bBusTimedOut && time - m_unLastValidResponse > BUS_TIMEOUT )
	{
		m_bBusTimedOut = true;
		ESP_LOGW("broan", "No valid response from ERV in %u ms - marking sensors unavailable", BUS_TIMEOUT);
		publishBusDisconnected();
	}

#ifdef SCAN_UNKNOWN

	if( m_nNextScan == 0 )
		m_nNextScan	= time + 15000;

	if( m_bERVReady && time > m_nNextScan )
	{
		m_nNextScan = time + 100;

		std::vector<unsigned char> vecRequest;
		vecRequest.push_back(0x20);

		for( int i=0; i<15;i++)
		{
			vecRequest.push_back(m_nFieldCursor);
			vecRequest.push_back(m_nGroupCursor);


			if( m_nFieldCursor == 0xFF)
			{

				switch( m_nGroupCursor )
				{
					case 0x20: m_nGroupCursor = 0x21; break;
					case 0x21: m_nGroupCursor = 0x22; break;
					case 0x22: m_nGroupCursor = 0x30; break;
					case 0x30: m_nGroupCursor = 0x40; break;
					case 0x40: m_nGroupCursor = 0x50; break;
					case 0x50: m_nGroupCursor = 0x60; break;
					case 0x60: m_nGroupCursor = 0xE0; break;
					case 0xE0: m_nGroupCursor = 0x20; break;
					//case 0xF0: m_nGroupCursor = 0x20; break;
				}
				//ESP_LOGD("broan","Brute force: Group is now %02X ", m_nGroupCursor );
			}
			m_nFieldCursor++;
		}

		queueMessage(vecRequest);

	}
#endif
}

void BroanComponent::publishBusDisconnected()
{
	// Only the sensors whose value genuinely comes from the ERV over the bus -
	// indoor_temperature/indoor_humidity are NOT affected: we publish those
	// ourselves from setCurrentTemperature()/setCurrentHumidity(), their source
	// is an external HA sensor, not the ERV - they stay valid even if the bus
	// to the ERV is down.
#ifdef USE_SENSOR
	if( power_sensor_ ) power_sensor_->publish_state(NAN);
	if( temperature_sensor_ ) temperature_sensor_->publish_state(NAN);
	if( temperature_out_sensor_ ) temperature_out_sensor_->publish_state(NAN);
	if( filter_life_sensor_ ) filter_life_sensor_->publish_state(NAN);
	if( supply_cfm_sensor_ ) supply_cfm_sensor_->publish_state(NAN);
	if( exhaust_cfm_sensor_ ) exhaust_cfm_sensor_->publish_state(NAN);
	if( supply_rpm_sensor_ ) supply_rpm_sensor_->publish_state(NAN);
	if( exhaust_rpm_sensor_ ) exhaust_rpm_sensor_->publish_state(NAN);
	if( turbo_remaining_sensor_ ) turbo_remaining_sensor_->publish_state(NAN);
	if( override_remaining_sensor_ ) override_remaining_sensor_->publish_state(NAN);
#endif

	// current_mode has no NAN equivalent (TextSensor::set_has_state(false) alone
	// wouldn't notify HA in real time - a genuine publish_state() call is needed).
	// "unknown" acts as a sentinel value: not HA's native "unavailable" badge,
	// but an explicit state indicating the data is no longer fresh.
#ifdef USE_TEXT_SENSOR
	if( current_mode_text_sensor_ ) current_mode_text_sensor_->publish_state("unknown");
#endif
}

}  // namespace broan
}  // namespace esphome
