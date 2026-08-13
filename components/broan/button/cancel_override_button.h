#pragma once

#include "esphome/components/button/button.h"
#include "../broan.h"

class BroanComponent;

namespace esphome {
namespace broan {

class CancelOverrideButton : public button::Button, public Parented<BroanComponent> {
 public:
  CancelOverrideButton() = default;

 protected:
  void press_action() override;
};

}  // namespace broan
}  // namespace esphome
