#include "intermittent_period_number.h"

namespace esphome {
namespace broan {

void IntermittentPeriodNumber::control(float value)
{
	this->publish_state(value);
	// Slider is in minutes (10-50, step 5); the wire register is in seconds.
	this->parent_->setIntermittentPeriod( (uint32_t)(value * 60.f) );
}

}  // namespace broan
}  // namespace esphome
