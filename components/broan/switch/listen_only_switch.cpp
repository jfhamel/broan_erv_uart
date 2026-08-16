#include "listen_only_switch.h"

namespace esphome {
namespace broan {

void ListenOnlySwitch::write_state(bool state) {
	this->publish_state(state);
	this->parent_->setListenOnly(state);
}

}  // namespace broan
}  // namespace esphome
