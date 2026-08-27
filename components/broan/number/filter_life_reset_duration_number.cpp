#include "filter_life_reset_duration_number.h"

namespace esphome {
namespace broan {

void FilterLifeResetDurationNumber::control(float value) {
	this->publish_state(value);
}

}  // namespace broan
}  // namespace esphome
