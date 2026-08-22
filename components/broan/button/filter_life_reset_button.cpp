#include "filter_life_reset_button.h"

namespace esphome {
namespace broan {

void FilterLifeResetButton::press_action()
{
	this->parent_->applyFilterLifeReset();
}

}  // namespace broan
}  // namespace esphome
