#include "turbo_duration_select.h"

namespace esphome {
namespace broan {

void TurboDurationSelect::control(const std::string &value)
{
	// Matches exactly the three durations the physical wall controller offers
	// (confirmed by capture: 1h/2h/4h - NOT 1h/2h/3h).
	uint32_t seconds = TURBO_DURATION_1H;

	if( value == "2h" )
		seconds = TURBO_DURATION_2H;
	else if( value == "4h" )
		seconds = TURBO_DURATION_4H;

	this->publish_state( value );
	this->parent_->setTurboDuration( seconds );
}

}  // namespace broan
}  // namespace esphome
