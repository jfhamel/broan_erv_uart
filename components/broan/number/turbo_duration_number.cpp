#include "turbo_duration_number.h"

namespace esphome {
namespace broan {

void TurboDurationNumber::control(float value)
{
	this->publish_state(value);
	if( this->parent_->m_vecFields[BroanField::FanMode].m_value.m_chValue == BroanFanMode::Turbo )
		this->parent_->setTurboDuration( (uint32_t)(value * 60.f) );
}

}  // namespace broan
}  // namespace esphome
