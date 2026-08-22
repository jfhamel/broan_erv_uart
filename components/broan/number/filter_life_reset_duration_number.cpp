#include "filter_life_reset_duration_number.h"

namespace esphome {
namespace broan {

void FilterLifeResetDurationNumber::control(float value) {
	// Pure value holder - just stores what to apply next time the
	// filter_life_reset button is pressed (see
	// BroanComponent::applyFilterLifeReset()). No write to the ERV happens
	// here on its own, matching the wall controller: picking a duration alone
	// doesn't reset anything until the reset action itself is confirmed.
	this->publish_state(value);
}

}  // namespace broan
}  // namespace esphome
