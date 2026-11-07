#include "env3_sensor.h"

#include "config.h"

Env3Sensor::Env3Sensor(TwoWire& wire, uint8_t sda, uint8_t scl) : Sensor("ENV III"), wire_(wire), sda_(sda), scl_(scl) {
  addMetric({"temperature_c", "気温", "°C", "*C", 1, COLOR_TEMPERATURE});
  addMetric({"humidity_pct", "湿度", "%", "%", 1, COLOR_HUMIDITY});
  addMetric({"pressure_hpa", "気圧", "hPa", "hPa", 1, COLOR_PRESSURE});
}

bool Env3Sensor::begin() {
  bool shtOk = sht3x_.begin(&wire_, SHT3X_I2C_ADDR, sda_, scl_, 400000U);
  bool qmpOk = qmp_.begin(&wire_, QMP6988_SLAVE_ADDRESS_L, sda_, scl_, 400000U);
  return shtOk && qmpOk;
}

bool Env3Sensor::read() {
  if (!sht3x_.update()) return false;
  qmp_.update();
  float hPa = qmp_.pressure / 100.0f;
  if (hPa < 300 || hPa > 1100) return false;  // 起動直後など、変換が終わっていない値は捨てる
  metrics()[0].value = sht3x_.cTemp;
  metrics()[1].value = sht3x_.humidity;
  metrics()[2].value = hPa;
  return true;
}
