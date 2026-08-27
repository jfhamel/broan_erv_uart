#include "intermittent_period_number.h"

namespace esphome {
namespace broan {

void IntermittentPeriodNumber::control(float value)
{
	this->publish_state(value);
	this->parent_->setIntermittentPeriod( (uint32_t)(value * 60.f) );
}

}  // namespace broan
}  // namespace esphome
