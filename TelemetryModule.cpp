#include "TelemetryModule.h"

#if USE_BH1750_SENSOR
#include <BH1750.h>
#endif

namespace
{
    // UART2 + MPPT protocol constants.
    static constexpr uint8_t UART2_TX_PIN = 17;
    static constexpr uint8_t UART2_RX_PIN = 16;
    static constexpr uint32_t UART2_BAUDRATE = 9600;
    static constexpr uint8_t MPPT_MODBUS_ADDRESS = 0xFF;
    static constexpr uint8_t MPPT_MODBUS_FUNC_READ_HOLDING = 0x03;
    static constexpr uint16_t MPPT_REG_BATTERY_SOC = 0x0100;
    static constexpr uint16_t MPPT_REG_BATTERY_VOLTAGE = 0x0101;
    static constexpr uint16_t MPPT_REG_CHARGE_CURRENT = 0x0102;
    static constexpr uint16_t MPPT_REG_PANEL_VOLTAGE = 0x0107;
    static constexpr uint16_t MPPT_REG_PANEL_CURRENT = 0x0108;
    static constexpr uint16_t MPPT_REG_PANEL_CHARGING_POWER = 0x0109;
    static constexpr uint32_t MPPT_POLL_INTERVAL_MS = 1000u;
    static constexpr uint32_t MPPT_RESPONSE_TIMEOUT_MS = 200u;

    // ADC + ACS725 constants for 0-20A unidirectional variants.
    static constexpr int ACS725_SENS_CUR_LOAD1_PIN = 36;
    static constexpr int ACS725_SENS_CUR_LOAD2_PIN = 39;
    static constexpr float ADC_REF_VOLTAGE = 3.3f;
    static constexpr float ESP32_ADC_RESOLUTION = 4095.0f;
    static constexpr uint8_t ACS725_SAMPLES_PER_READ = 8u;
    static constexpr float ACS725_LOAD1_SENSITIVITY_V_PER_A = 0.132f;
    static constexpr float ACS725_LOAD2_SENSITIVITY_V_PER_A = 0.132f;
    static constexpr float ACS725_LOAD1_ZERO_CURRENT_VOLTAGE = 0.33f;
    static constexpr float ACS725_LOAD2_ZERO_CURRENT_VOLTAGE = 0.33f;
    static constexpr float ACS725_LOAD1_SIGN = 1.0f;
    static constexpr float ACS725_LOAD2_SIGN = 1.0f;
    static constexpr float ACS725_LOAD1_OFFSET_A = 0.0f;
    static constexpr float ACS725_LOAD2_OFFSET_A = 0.0f;
    static constexpr uint32_t LOAD_CURRENT_SAMPLE_INTERVAL_MS = 100u;
    static constexpr uint8_t LOAD_CURRENT_SWMA_WINDOW_SAMPLES = 10u;

    struct SlidingWindowAverage
    {
        float Samples[LOAD_CURRENT_SWMA_WINDOW_SAMPLES];
        uint8_t NextIndex;
        uint8_t Count;
        float Sum;
    };

    static SlidingWindowAverage Load1Filter = {{0.0f}, 0u, 0u, 0.0f};
    static SlidingWindowAverage Load2Filter = {{0.0f}, 0u, 0u, 0.0f};

    static void ResetSlidingWindow(SlidingWindowAverage& filter)
    {
        for(uint8_t i = 0u; i < LOAD_CURRENT_SWMA_WINDOW_SAMPLES; i++)
        {
            filter.Samples[i] = 0.0f;
        }

        filter.NextIndex = 0u;
        filter.Count = 0u;
        filter.Sum = 0.0f;
    }

    static float PushSlidingWindowSample(SlidingWindowAverage& filter, float sample)
    {
        if(filter.Count < LOAD_CURRENT_SWMA_WINDOW_SAMPLES)
        {
            filter.Count++;
        }
        else
        {
            filter.Sum -= filter.Samples[filter.NextIndex];
        }

        filter.Samples[filter.NextIndex] = sample;
        filter.Sum += sample;
        filter.NextIndex = (uint8_t)((filter.NextIndex + 1u) % LOAD_CURRENT_SWMA_WINDOW_SAMPLES);

        if(filter.Count == 0u)
        {
            return sample;
        }

        return filter.Sum / (float)filter.Count;
    }

    static float ReadAdcVoltage(int pin)
    {
        // Prefer calibrated millivolt conversion when available.
        uint32_t mvSum = 0u;
        uint8_t validCount = 0u;
        for(uint8_t i = 0u; i < ACS725_SAMPLES_PER_READ; i++)
        {
            uint32_t mv = (uint32_t)analogReadMilliVolts(pin);
            if(mv > 0u)
            {
                mvSum += mv;
                validCount++;
            }
        }

        if(validCount > 0u)
        {
            float mvAverage = (float)mvSum / (float)validCount;
            return mvAverage / 1000.0f;
        }

        // Fallback path if mV calibration is unavailable at runtime.
        uint32_t adcSum = 0u;
        for(uint8_t i = 0u; i < ACS725_SAMPLES_PER_READ; i++)
        {
            adcSum += (uint32_t)analogRead(pin);
        }

        float adcAverage = (float)adcSum / (float)ACS725_SAMPLES_PER_READ;
        return (adcAverage / ESP32_ADC_RESOLUTION) * ADC_REF_VOLTAGE;
    }

    static float GetACS725_Current(int pin,
                                   float zeroCurrentVoltage,
                                   float sensitivityVPerA,
                                   float sign,
                                   float offsetA)
    {
        if(sensitivityVPerA <= 0.0f)
        {
            return 0.0f;
        }

        float voltage = ReadAdcVoltage(pin);
        float current = ((voltage - zeroCurrentVoltage) / sensitivityVPerA);

        current = (current * sign) + offsetA;
        if(current < 0.0f)
        {
            current = 0.0f;
        }

        return current;
    }

    static uint16_t ReadAdcAverageRaw(int pin)
    {
        uint32_t adcSum = 0u;
        for(uint8_t i = 0u; i < ACS725_SAMPLES_PER_READ; i++)
        {
            adcSum += (uint32_t)analogRead(pin);
        }

        return (uint16_t)(adcSum / (uint32_t)ACS725_SAMPLES_PER_READ);
    }
}

extern float Load1CurrentA;
extern float Load2CurrentA;
extern float AmbientLightLux;
extern float PanelVoltage;
extern float ChargingCurrent;
extern float BatterySoc;
extern float BatteryVoltage;
extern float PanelCurrent;
extern float PanelChargingPower;
extern bool MpptPollHasRun;
extern bool MpptLinkHealthy;
extern uint8_t MpptLastReadErrorCode;
extern uint8_t MpptLinkStatusCode;

#if USE_BH1750_SENSOR
extern BH1750 LightMeter;
#endif

void InitUart2()
{
    Serial2.begin(UART2_BAUDRATE, SERIAL_8N1, UART2_RX_PIN, UART2_TX_PIN);
    Serial2.setTimeout(MPPT_RESPONSE_TIMEOUT_MS);

  #ifdef USE_DEBUG_APP
    Serial.printf("[UART2] Started at %u baud, 8N1 (RX=%u, TX=%u)\n",
                  (unsigned)UART2_BAUDRATE,
                  (unsigned)UART2_RX_PIN,
                  (unsigned)UART2_TX_PIN);
  #endif
}

void InitAdcInputs()
{
    analogReadResolution(12);
    analogSetAttenuation(ADC_11db);
    analogSetPinAttenuation(ACS725_SENS_CUR_LOAD1_PIN, ADC_11db);
    analogSetPinAttenuation(ACS725_SENS_CUR_LOAD2_PIN, ADC_11db);

    pinMode(ACS725_SENS_CUR_LOAD1_PIN, INPUT);
    pinMode(ACS725_SENS_CUR_LOAD2_PIN, INPUT);

    ResetSlidingWindow(Load1Filter);
    ResetSlidingWindow(Load2Filter);
}

void UpdateLoadCurrentTelemetry(uint32_t nowMs)
{
    static uint32_t lastSampleMs = 0u;
    static bool initialized = false;

    if(!initialized)
    {
        initialized = true;
    }
    else if((nowMs - lastSampleMs) < LOAD_CURRENT_SAMPLE_INTERVAL_MS)
    {
        return;
    }

    lastSampleMs = nowMs;

    float load1Raw = ReadLoad1CurrentA();
    float load2Raw = ReadLoad2CurrentA();

    Load1CurrentA = PushSlidingWindowSample(Load1Filter, load1Raw);
    Load2CurrentA = PushSlidingWindowSample(Load2Filter, load2Raw);
}

uint16_t ComputeModbusCrc16(const uint8_t* pData, size_t lengthBytes)
{
    uint16_t crc = 0xFFFFu;

    for(size_t i = 0; i < lengthBytes; i++)
    {
        crc ^= pData[i];

        for(uint8_t bit = 0; bit < 8; bit++)
        {
            if((crc & 0x0001u) != 0)
            {
                crc >>= 1;
                crc ^= 0xA001u;
            }
            else
            {
                crc >>= 1;
            }
        }
    }

    return crc;
}

bool ReadMpptHoldingRegister(uint16_t registerAddress, uint16_t& outValue)
{
    uint8_t request[8];
    request[0] = MPPT_MODBUS_ADDRESS;
    request[1] = MPPT_MODBUS_FUNC_READ_HOLDING;
    request[2] = (uint8_t)(registerAddress >> 8);
    request[3] = (uint8_t)(registerAddress & 0xFFu);
    request[4] = 0x00;
    request[5] = 0x01;

    uint16_t requestCrc = ComputeModbusCrc16(request, 6);
    request[6] = (uint8_t)(requestCrc & 0xFFu);
    request[7] = (uint8_t)(requestCrc >> 8);

    while(Serial2.available() > 0)
    {
        Serial2.read();
    }

    Serial2.write(request, sizeof(request));
    Serial2.flush();

    uint32_t startMs = millis();
    while(Serial2.available() < 7)
    {
        if((millis() - startMs) > MPPT_RESPONSE_TIMEOUT_MS)
        {
            MpptLastReadErrorCode = 1;
            return false;
        }

        delay(1);
    }

    uint8_t response[7];
    size_t readCount = Serial2.readBytes(response, sizeof(response));
    if(readCount != sizeof(response))
    {
        MpptLastReadErrorCode = 1;
        return false;
    }

    if(response[0] != MPPT_MODBUS_ADDRESS)
    {
        MpptLastReadErrorCode = 3;
        return false;
    }

    if(response[1] != MPPT_MODBUS_FUNC_READ_HOLDING)
    {
        MpptLastReadErrorCode = 3;
        return false;
    }

    if(response[2] != 0x02)
    {
        MpptLastReadErrorCode = 3;
        return false;
    }

    uint16_t receivedCrc = (uint16_t)response[5] | ((uint16_t)response[6] << 8);
    uint16_t computedCrc = ComputeModbusCrc16(response, 5);
    if(receivedCrc != computedCrc)
    {
        MpptLastReadErrorCode = 2;
        return false;
    }

    outValue = ((uint16_t)response[3] << 8) | (uint16_t)response[4];
    MpptLastReadErrorCode = 0;
    return true;
}

void UpdateMpptTelemetry(uint32_t nowMs)
{
    static uint32_t lastPollMs = 0;
    if((nowMs - lastPollMs) < MPPT_POLL_INTERVAL_MS)
    {
        return;
    }

    lastPollMs = nowMs;

    uint16_t rawValue = 0;
    bool allReadsOk = true;
    uint8_t cycleErrorCode = 0;

    if(ReadMpptHoldingRegister(MPPT_REG_BATTERY_SOC, rawValue))
    {
        BatterySoc = (float)rawValue;
    }
    else
    {
        allReadsOk = false;
        if(cycleErrorCode == 0) { cycleErrorCode = MpptLastReadErrorCode; }
    }

    if(ReadMpptHoldingRegister(MPPT_REG_BATTERY_VOLTAGE, rawValue))
    {
        BatteryVoltage = (float)rawValue * 0.1f;
    }
    else
    {
        allReadsOk = false;
        if(cycleErrorCode == 0) { cycleErrorCode = MpptLastReadErrorCode; }
    }

    if(ReadMpptHoldingRegister(MPPT_REG_CHARGE_CURRENT, rawValue))
    {
        ChargingCurrent = (float)rawValue * 0.01f;
    }
    else
    {
        allReadsOk = false;
        if(cycleErrorCode == 0) { cycleErrorCode = MpptLastReadErrorCode; }
    }

    if(ReadMpptHoldingRegister(MPPT_REG_PANEL_VOLTAGE, rawValue))
    {
        PanelVoltage = (float)rawValue * 0.1f;
    }
    else
    {
        allReadsOk = false;
        if(cycleErrorCode == 0) { cycleErrorCode = MpptLastReadErrorCode; }
    }

    if(ReadMpptHoldingRegister(MPPT_REG_PANEL_CURRENT, rawValue))
    {
        PanelCurrent = (float)rawValue * 0.01f;
    }
    else
    {
        allReadsOk = false;
        if(cycleErrorCode == 0) { cycleErrorCode = MpptLastReadErrorCode; }
    }

    if(ReadMpptHoldingRegister(MPPT_REG_PANEL_CHARGING_POWER, rawValue))
    {
        PanelChargingPower = (float)rawValue;
    }
    else
    {
        allReadsOk = false;
        if(cycleErrorCode == 0) { cycleErrorCode = MpptLastReadErrorCode; }
    }

    MpptPollHasRun = true;
    MpptLinkHealthy = allReadsOk;
    if(allReadsOk)
    {
        MpptLinkStatusCode = 1;
    }
    else if(cycleErrorCode == 1)
    {
        MpptLinkStatusCode = 2;
    }
    else if(cycleErrorCode == 2)
    {
        MpptLinkStatusCode = 3;
    }
    else
    {
        MpptLinkStatusCode = 4;
    }
}

float ReadLoad1CurrentA()
{
    return GetACS725_Current(ACS725_SENS_CUR_LOAD1_PIN,
                             ACS725_LOAD1_ZERO_CURRENT_VOLTAGE,
                             ACS725_LOAD1_SENSITIVITY_V_PER_A,
                             ACS725_LOAD1_SIGN,
                             ACS725_LOAD1_OFFSET_A);
}

float ReadLoad2CurrentA()
{
    return GetACS725_Current(ACS725_SENS_CUR_LOAD2_PIN,
                             ACS725_LOAD2_ZERO_CURRENT_VOLTAGE,
                             ACS725_LOAD2_SENSITIVITY_V_PER_A,
                             ACS725_LOAD2_SIGN,
                             ACS725_LOAD2_OFFSET_A);
}

float ReadAmbientLightLux()
{
#if !USE_BH1750_SENSOR
    return AmbientLightLux;
#else
    float lux = LightMeter.readLightLevel();

    if(lux < 0.0f)
    {
        return AmbientLightLux;
    }

    return lux;
#endif
}

float GetLoad1CurrentA()
{
    return Load1CurrentA;
}

float GetLoad2CurrentA()
{
    return Load2CurrentA;
}

uint16_t GetLoad1AdcRaw()
{
    return ReadAdcAverageRaw(ACS725_SENS_CUR_LOAD1_PIN);
}

uint16_t GetLoad2AdcRaw()
{
    return ReadAdcAverageRaw(ACS725_SENS_CUR_LOAD2_PIN);
}

float GetAmbientLightLux()
{
    return AmbientLightLux;
}

float GetPanelVoltage()
{
    return PanelVoltage;
}

float GetChargingCurrent()
{
    return ChargingCurrent;
}

float GetBatterySoc()
{
    return BatterySoc;
}

float GetBatteryVoltage()
{
    return BatteryVoltage;
}

float GetPanelCurrent()
{
    return PanelCurrent;
}

float GetPanelChargingPower()
{
    return PanelChargingPower;
}

String GetMpptLinkStatusSummary()
{
    if(!MpptPollHasRun)
    {
        return "WAITING";
    }

    if(MpptLinkStatusCode == 1)
    {
        return "OK";
    }

    if(MpptLinkStatusCode == 2)
    {
        return "TIMEOUT";
    }

    if(MpptLinkStatusCode == 3)
    {
        return "CRC";
    }

    return "ERROR";
}