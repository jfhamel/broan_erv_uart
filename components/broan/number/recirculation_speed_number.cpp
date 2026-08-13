#include "recirculation_speed_number.h"

namespace esphome {
namespace broan {

void RecirculationSpeedNumber::control(float value)
{
	this->publish_state(value);
	this->parent_->setRecirculationSpeed( value );
}

}  // namespace broan
}  // namespace esphome
