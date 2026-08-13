#include "turbo_switch.h"

namespace esphome {
namespace broan {

void TurboSwitch::write_state(bool state) {
	this->publish_state(state);
	if( state )
		this->parent_->startTurbo();
	else
		this->parent_->cancelOverride();
}

}  // namespace broan
}  // namespace esphome
