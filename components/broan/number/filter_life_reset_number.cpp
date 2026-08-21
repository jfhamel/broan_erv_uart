#include "filter_life_reset_number.h"

namespace esphome {
namespace broan {

void FilterLifeResetNumber::control(float value) {
	this->publish_state(value);
	this->parent_->setFilterLife( (uint32_t)value );
}

}  // namespace broan
}  // namespace esphome
