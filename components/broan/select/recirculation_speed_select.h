#pragma once

#include "esphome/components/select/select.h"
#include "../broan.h"

class BroanComponent;

namespace esphome {
namespace broan {

class RecirculationSpeedSelect : public select::Select, public Parented<BroanComponent> {
 public:
  RecirculationSpeedSelect() = default;

 protected:
  void control(const std::string &value) override;
};

}  // namespace broan
}  // namespace esphome
