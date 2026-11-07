#pragma once

#include <M5UnitENV.h>
#include <Wire.h>

#include "sensor.h"

// M5Stack Unit ENV III (SHT30: 気温・湿度 / QMP6988: 気圧)
class Env3Sensor : public Sensor {
 public:
  Env3Sensor(TwoWire& wire, uint8_t sda, uint8_t scl);

 protected:
  bool begin() override;
  bool read() override;

 private:
  TwoWire& wire_;
  uint8_t sda_;
  uint8_t scl_;
  SHT3X sht3x_;
  QMP6988 qmp_;
};
