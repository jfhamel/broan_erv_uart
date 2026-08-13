#include "cancel_override_button.h"

namespace esphome {
namespace broan {

void CancelOverrideButton::press_action()
{
	this->parent_->cancelOverride();
}

}  // namespace broan
}  // namespace esphome
