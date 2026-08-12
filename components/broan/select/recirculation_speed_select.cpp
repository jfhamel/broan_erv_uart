#include "recirculation_speed_select.h"

namespace esphome {
namespace broan {

void RecirculationSpeedSelect::control(const std::string &value)
{
	this->publish_state( value );
	this->parent_->setRecirculationSpeed( value );
}

}  // namespace broan
}  // namespace esphome
