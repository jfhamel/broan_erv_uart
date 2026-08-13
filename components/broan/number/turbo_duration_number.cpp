#include "turbo_duration_number.h"

namespace esphome {
namespace broan {

void TurboDurationNumber::control(float value)
{
	this->publish_state(value);

	// If Turbo is already running, adjusting the slider updates the live timer too
	// (matches how the wall controller works: picking a new duration mid-Turbo just
	// resets the countdown - confirmed for 1h/2h/4h by capture). If Turbo isn't
	// running, this just stores the value for the next time the turbo switch is
	// turned on - startTurbo() reads ->state directly, no separate write here.
	if( this->parent_->m_vecFields[BroanField::FanMode].m_value.m_chValue == BroanFanMode::Turbo )
		this->parent_->setTurboDuration( (uint32_t)(value * 60.f) );
}

}  // namespace broan
}  // namespace esphome
