#ifndef TELEMETRY_MODULE_H
#define TELEMETRY_MODULE_H

#include <Arduino.h>

#ifndef USE_BH1750_SENSOR
#define USE_BH1750_SENSOR 1
#endif

void InitUart2();
void InitAdcInputs();
void UpdateLoadCurrentTelemetry(uint32_t nowMs);

uint16_t ComputeModbusCrc16(const uint8_t* pData, size_t lengthBytes);
bool ReadMpptHoldingRegister(uint16_t registerAddress, uint16_t& outValue);
void UpdateMpptTelemetry(uint32_t nowMs);

float ReadLoad1CurrentA();
float ReadLoad2CurrentA();
float ReadAmbientLightLux();

float GetLoad1CurrentA();
float GetLoad2CurrentA();
uint16_t GetLoad1AdcRaw();
uint16_t GetLoad2AdcRaw();
float GetAmbientLightLux();
float GetPanelVoltage();
float GetChargingCurrent();
float GetBatterySoc();
float GetBatteryVoltage();
float GetPanelCurrent();
float GetPanelChargingPower();
String GetMpptLinkStatusSummary();

#endif // TELEMETRY_MODULE_H