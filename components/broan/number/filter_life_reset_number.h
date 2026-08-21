#pragma once

#include "esphome/components/number/number.h"
#include "../broan.h"

class BroanComponent;

namespace esphome {
namespace broan {

class FilterLifeResetNumber : public number::Number, public Parented<BroanComponent> {
 public:
  FilterLifeResetNumber() = default;

 protected:
  void control(float value) override;
};

}  // namespace broan
}  // namespace esphome
