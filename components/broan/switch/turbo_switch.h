#pragma once

#include "esphome/components/switch/switch.h"
#include "../broan.h"

class BroanComponent;

namespace esphome {
namespace broan {

class TurboSwitch : public switch_::Switch, public Parented<BroanComponent> {
 public:
  TurboSwitch() = default;

 protected:
  void write_state(bool state) override;
};

}  // namespace broan
}  // namespace esphome
