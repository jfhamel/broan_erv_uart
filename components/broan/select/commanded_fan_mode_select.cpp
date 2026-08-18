#include "commanded_fan_mode_select.h"

namespace esphome {
namespace broan {

void CommandedFanModeSelect::control(const std::string &value)
{
	this->publish_state(value);
	this->parent_->setFanMode( value );
}

}  // namespace broan
}  // namespace esphome