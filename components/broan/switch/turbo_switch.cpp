#include "turbo_switch.h"

namespace esphome {
namespace broan {

void TurboSwitch::write_state(bool state) {
	this->publish_state(state);
	this->parent_->setTurbo(state);
}

}  // namespace broan
}  // namespace esphome
