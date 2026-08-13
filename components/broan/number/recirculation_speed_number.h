#pragma once

#include "esphome/components/number/number.h"
#include "../broan.h"

class BroanComponent;

namespace esphome {
namespace broan {

class RecirculationSpeedNumber : public number::Number, public Parented<BroanComponent> {
 public:
  RecirculationSpeedNumber() = default;

 protected:
  void control(float value) override;
};

}  // namespace broan
}  // namespace esphome
