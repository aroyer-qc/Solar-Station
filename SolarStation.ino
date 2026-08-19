//-------------------------------------------------------------------------------------------------
//
//  File : SolarStation.ino
//
//-------------------------------------------------------------------------------------------------
//
// Copyright(c) 2025 Alain Royer.
// Email: aroyer.qc@gmail.com
//
// Permission is hereby granted, free of charge, to any person obtaining a copy of this software
// and associated documentation files (the "Software"), to deal in the Software without
// restriction, including without limitation the rights to Use, copy, modify, merge, publish,
// distribute, sublicense, and/or sell copies of the Software, and to permit persons to whom the
// Software is furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in all copies or
// substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED,
// INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE
// AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM,
// DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
//
//-------------------------------------------------------------------------------------------------
//
//    Solar Station
//
//        - Tracking panel.
//            * control motor.
//        
//        - Monitor panel voltage and current.
//        - Monitor battery voltage and current flow in or out.
//        - monitor load current.
//        - Control IO according to schedule and or power availability
//        - more to add...
//
//
// Filesystem upload procedure (required to serve local web pages from /data):
// 1) Build and upload firmware from Arduino IDE as usual.
// 2) Open PowerShell in the project root folder.
// 3) Run the helper script with your serial port and partition scheme.
//
// Example commands:
//   .\tools\upload_spiffs.ps1 -Port COMx -Partition default
//   .\tools\upload_spiffs.ps1 -Port COMx -Partition no_ota
//   .\tools\upload_spiffs.ps1 -Port COMx -Partition huge_app
//   .\tools\upload_spiffs.ps1 -Port COMx -Partition min_spiffs
//
// Notes:
// - Use the same partition scheme selected in Arduino IDE (Tools > Partition Scheme).
// - The script packs files from /data into a SPIFFS image and writes it to flash.
// - If pages are blank, upload firmware then rerun this script and reboot the ESP32.
//
//
//-------------------------------------------------------------------------------------------------

//-------------------------------------------------------------------------------------------------
// Include file(s)
//-------------------------------------------------------------------------------------------------

#include <Wire.h>

// #define USE_DEBUG_APP

#ifndef USE_BH1750_SENSOR
#define USE_BH1750_SENSOR            1
#endif

#ifndef USE_IO_EXPANDER
#define USE_IO_EXPANDER              1
#endif

#ifndef USE_M95P32
#define USE_M95P32                   1
#endif

#if USE_BH1750_SENSOR
#include <BH1750.h>
#endif
#include "SolarFixed.h"
#include <math.h>
#include "ConfigModule.h"
#include "WiFiModule.h"
#include "WebServerModule.h"
#include "TelemetryModule.h"
#include "SchedulerModule.h"
#include "TrackingModule.h"
#include "FastAccelStepper.h"
#include "AzimuthController.h"
#include "ElevationController.h"
#include <sys/time.h>

#if USE_M95P32
#include "M95PxxModule.h"
#endif

//-------------------------------------------------------------------------------------------------
// Define(s)
//-------------------------------------------------------------------------------------------------

// Miscellaneous Default

#define DEFAULT_SERIAL_BAUD_RATE        115200

//-------------------------------------------------------------------------------------------------
// Default positions

//-------------------------------------------------------------------------------------------------
// IO definitions

// UART2 (configure pins as needed)
// ESP32 module pins: 27 -> GPIO16, 28 -> GPIO17
#define UART2_TX_PIN                    17
#define UART2_RX_PIN                    16
#define UART2_BAUDRATE                  9600
#define MPPT_MODBUS_ADDRESS             0xFF
#define MPPT_MODBUS_FUNC_READ_HOLDING   0x03

// Read Battery SOC (Value * 1 - Ex: 0x00 0x37 = 55%)
// CMD:	0x100  Bytes Read 2
// Read Battery voltage (Value * 0.1 - Ex: 0x00 0x7A = 12.2V)
// CMD:	0x101  Bytes Read 2
// Read Charging Current to Battery (Value * 0.01 - Ex: 0x01 0x0A = 2.66A)
// CMD:	0x102  Bytes Read 2
// Read Panel voltage (Value * 0.1 - Ex: 0x00 0xC8 = 20.0V)
// CMD:	0x107  Bytes Read 2
// Read Panel current to controller (Value * 0.01 - Ex: 0x01 0x0A = 2.66A)
// CMD:	0x108  Bytes Read 2
// Read Panel Charging power (Value * 1 - Ex: 0x00 0x35 = 55W)
// CMD:	0x109  Bytes Read 2
#define MPPT_REG_BATTERY_SOC            0x0100
#define MPPT_REG_BATTERY_VOLTAGE        0x0101
#define MPPT_REG_CHARGE_CURRENT         0x0102
#define MPPT_REG_PANEL_VOLTAGE          0x0107
#define MPPT_REG_PANEL_CURRENT          0x0108
#define MPPT_REG_PANEL_CHARGING_POWER   0x0109
// UART2 Serial port

// PCA9538PW (MS1/MS2 via IO expander on I2C)
#define PCA9538_I2C_ADDRESS             0x70
#define PCA9538_REG_INPUT               0x00
#define PCA9538_REG_OUTPUT              0x01
#define PCA9538_REG_POLARITY            0x02
#define PCA9538_REG_CONFIG              0x03

// 3x TMC2209 stepper wiring
#define STEPPER1_DIR_PIN                33
#define STEPPER1_STEP_PIN               25
#define STEPPER2_DIR_PIN                26
#define STEPPER2_STEP_PIN               27
#define STEPPER3_DIR_PIN                14
#define STEPPER3_STEP_PIN               15

// Driver diagnostic inputs
#define STEPPER1_DIAG_PIN               34
#define STEPPER2_DIAG_PIN               35
#define STEPPER3_DIAG_PIN               32

// Adjust to LOW if your driver DIAG is active-low
#define STEPPER_DIAG_ACTIVE_STATE       HIGH

// Shared control lines for all 3 drivers
#define STEPPER_PDN_SHARED_PIN           4
#define STEPPER_SPRD_SHARED_PIN          0

// Defaults can be updated later from config/IO expander logic
#define STEPPER_PDN_DEFAULT_STATE        HIGH
#define STEPPER_SPRD_DEFAULT_STATE       LOW

// PCA9538 pin mapping for microstep control (shared in parallel for all 3 steppers)
// MS2 -> IO0, MS1 -> IO1
#define STEPPER_MS2_EXP_PIN              0
#define STEPPER_MS1_EXP_PIN              1
#define STEPPER_EN_EXP_PIN               2

// Shared TMC2209 EN line through PCA9538 IO2.
// Most TMC2209 modules use EN active-low.
#define STEPPER_EN_ACTIVE_STATE          LOW
#define STEPPER_EN_INACTIVE_STATE        HIGH

// PCA9538 status LED output
#define STATUS_LED_EXP_PIN               3
#define STATUS_LED_ON_STATE              LOW
#define STATUS_LED_OFF_STATE             HIGH
#define STATUS_LED_BLINK_INTERVAL_MS     500

// PCA9538 limit switches (wired to GND)
#define LIMIT_SWITCH1_EXP_PIN            4
#define LIMIT_SWITCH2_EXP_PIN            5
#define LIMIT_SWITCH3_EXP_PIN            6
#define LIMIT_SWITCH4_EXP_PIN            7
#define LIMIT_SWITCH_ACTIVE_STATE        LOW

// Default microstep profile on MS pins (can be changed from config later)
#define STEPPER_MS1_DEFAULT_STATE        LOW
#define STEPPER_MS2_DEFAULT_STATE        LOW

// ADC on ADC1
// ACS725 sensors:
// - Load 1 current on ADC1_CH0 (GPIO36)
// - Load 2 current on ADC1_CH3 (GPIO39)
#define ACS725_SENS_CUR_LOAD1_PIN        36
#define ACS725_SENS_CUR_LOAD2_PIN        39

// 3x MOSFET low-side outputs
#define MOSFET_OUTPUT1_PIN               12
#define MOSFET_OUTPUT2_PIN               13
#define MOSFET_OUTPUT3_PIN               2

// ESP32 LEDC PWM configuration for MOSFET outputs
#define MOSFET_PWM_FREQ_HZ               20000
#define MOSFET_PWM_RES_BITS              10

//-------------------------------------------------------------------------------------------------

// Per-sensor calibration:
// - SIGN: use +1.0f for normal polarity, -1.0f to invert current direction
// - OFFSET_A: residual current offset in amps when true current is 0 A

#define ADC_REF_VOLTAGE                  (float(3.3))                // ESP32 ADC reference/supply voltage
#define ESP32_ADC_RESOLUTION             4095                        // 12-bit ADC
#define ACS725_SAMPLES_PER_READ          8u
#define ACS725_LOAD1_SENSITIVITY_V_PER_A (float(0.132))              // ACS725 0-20A unidirectional sensitivity at 3.3V (about 132mV/A)
#define ACS725_LOAD2_SENSITIVITY_V_PER_A (float(0.132))              // ACS725 0-20A unidirectional sensitivity at 3.3V (about 132mV/A)
#define ACS725_LOAD1_ZERO_CURRENT_VOLTAGE (float(0.33))              // Unidirectional quiescent output near 10% of Vcc (adjust by calibration)
#define ACS725_LOAD2_ZERO_CURRENT_VOLTAGE (float(0.33))              // Unidirectional quiescent output near 10% of Vcc (adjust by calibration)

// Per-sensor calibration:
// - SIGN: use +1.0f for normal polarity, -1.0f to invert current direction
// - OFFSET_A: residual current offset in amps when true current is 0 A
#define ACS725_LOAD1_SIGN                (1.0f)
#define ACS725_LOAD2_SIGN                (1.0f)
#define ACS725_LOAD1_OFFSET_A            (0.0f)
#define ACS725_LOAD2_OFFSET_A            (0.0f)
#define ACS725_CLAMP_NEGATIVE_TO_ZERO    1
#define VAR_UNUSED(v)                    ((void)(v))

static constexpr uint32_t OUTPUT_SCHEDULE_UPDATE_MS = 1000u;
static constexpr uint32_t MORNING_RETURN_CHECK_MS = 15000u;
static constexpr uint16_t MORNING_RETURN_DELAY_AFTER_RED_MIN = 30u;
static constexpr uint32_t PANEL_ADJUSTMENT_INTERVAL_MS = 1000u;
static constexpr uint32_t SENSOR_REFRESH_MS = 2000u;
static constexpr uint32_t MPPT_POLL_INTERVAL_MS = 1000u;
static constexpr uint32_t MPPT_RESPONSE_TIMEOUT_MS = 200u;
static constexpr uint32_t SUNSET_RETURN_CHECK_MS = 15000u;
static constexpr uint16_t RETURN_DELAY_AFTER_SUNSET_MIN = 30u;
static constexpr uint32_t ADC_LOG_SAMPLE_INTERVAL_MS = 1000u;
static constexpr uint32_t ADC_LOG_AVG_WINDOW_MS = 2000u;
static constexpr uint32_t LUX_LOG_SAMPLE_INTERVAL_MS = SENSOR_REFRESH_MS;
static constexpr uint32_t LUX_LOG_AVG_WINDOW_MS = 2000u;
static constexpr uint8_t LOG_RETENTION_THRESHOLD_PERCENT = 80u;

struct SensorLogChannelState
{
    uint32_t LastSampleMs;
    uint32_t LastFlushMs;
    float Sum;
    uint16_t Count;
};

struct SensorLogDayEntry
{
    uint8_t Active;
    uint8_t Reserved[3];
    uint32_t DayKey;
    uint32_t Sequence;
    uint16_t Load1Count;
    uint16_t Load2Count;
    uint16_t LuxCount;
    uint16_t Reserved2;
};

struct SensorLogMeta
{
    uint8_t Magic[8];
    uint16_t Version;
    uint16_t Reserved;
    uint32_t NextSequence;
    SensorLogDayEntry Days[13];
};

static constexpr uint32_t M95_LOG_TOTAL_CAPACITY_BYTES = 4u * 1024u * 1024u;
static constexpr uint32_t M95_LOG_META_ADDR = 0x000100u;
static constexpr uint32_t M95_LOG_META_SIZE = 0x0800u;
static constexpr uint32_t M95_LOG_DATA_START_ADDR = 0x001000u;
static constexpr uint16_t ADC_LOG_SAMPLES_PER_DAY = 17280u;
static constexpr uint16_t LUX_LOG_SAMPLES_PER_DAY = 43200u;
static constexpr uint32_t LOAD1_REGION_BYTES = ADC_LOG_SAMPLES_PER_DAY * sizeof(float);
static constexpr uint32_t LOAD2_REGION_BYTES = ADC_LOG_SAMPLES_PER_DAY * sizeof(float);
static constexpr uint32_t LUX_REGION_BYTES = LUX_LOG_SAMPLES_PER_DAY * sizeof(float);
static constexpr uint32_t M95_LOG_DAY_SLOT_BYTES = LOAD1_REGION_BYTES + LOAD2_REGION_BYTES + LUX_REGION_BYTES;
static constexpr uint8_t M95_LOG_DAY_SLOT_COUNT = 13u;
static constexpr uint8_t M95_LOG_MAGIC[8] = {'M','9','5','L','O','G','1','\0'};
static constexpr uint16_t M95_LOG_VERSION = 1u;

//-------------------------------------------------------------------------------------------------
//  Typedef(s)
//-------------------------------------------------------------------------------------------------

enum State_e            // Common Controller Status Bits
{
    CHARGING                    = 0,            // Charging:                Controller is actively charging the battery.
    FLOAT_MODE                  = 1,            // Float Mode:              Battery is in float charging stage.
    BOOST_MODE                  = 2,            // Boost Mode:              Battery is in boost charging stage.
    EQULIZATION_MODE            = 3,            // Equalization Mode:       Equalization charging is active.
    OVERLOAD                    = 4,            // Overload:                Load current exceeds rated capacity (N/U).
    OVER_TEMPERATURE            = 5,            // Over Temperature         Controller temperature exceeds safe limit.
    BATTERY_OVER_VOLTAGE        = 6,            // Battery Over Voltage     Battery voltage too high.
    BATTERY_UNDER_VOLTAGE       = 7,            // Battery Under Voltage    Battery voltage too low.
    PANEL_OVER_VOLTAGE          = 8,            // PV Over Voltage          Solar panel
    PANEL_OVER_CURRENT          = 9,            // PV Over Current          Solar panel
    COMMUNICATION_ERROR         = 10,           // Communication Error      Modbus or RS485 communication fault.
    LOAD_ON                     = 11,           // Load On                  Load output is enabled (not supported).
    LOAD_OFF                    = 12,           // Load Off                 Load output is disabled (not supported).
};

//-------------------------------------------------------------------------------------------------
// Variable(s)
//-------------------------------------------------------------------------------------------------

//-------------------------------------------------------------------------------------------------
// IP with default value
#ifdef USE_WIFI
#endif

uint32_t                TickCounter;
#if USE_BH1750_SENSOR
BH1750                  LightMeter;
#endif


WifiModule              Wifi;
WebServerModule         WebServer;
AzimuthController       azimuthController;
ElevationController     elevationController;

#if USE_M95P32
M95PxxModule            Eeprom;
#endif

float                   Load1CurrentA = 0.0f;
float                   Load2CurrentA = 0.0f;
float                   AmbientLightLux = 0.0f;
float                   PanelVoltage = 0.0f;
float                   ChargingCurrent = 0.0f;
float                   BatterySoc = 0.0f;
float                   BatteryVoltage = 0.0f;
float                   PanelCurrent = 0.0f;
float                   PanelChargingPower = 0.0f;
bool                    MpptPollHasRun = false;
bool                    MpptLinkHealthy = false;
uint8_t                 MpptLastReadErrorCode = 0;
uint8_t                 MpptLinkStatusCode = 0;
SensorLogChannelState   Load1LogState = {0, 0, 0.0f, 0};
SensorLogChannelState   Load2LogState = {0, 0, 0.0f, 0};
SensorLogChannelState   LuxLogState = {0, 0, 0.0f, 0};
int32_t                 CurrentLogDayKey = -1;
String                  CurrentLogDayTag;
SensorLogMeta           M95LogMeta;
bool                    M95LogMetaLoaded = false;
int8_t                  CurrentLogDaySlot = -1;
bool                    StatusLedBlinkState = false;
uint32_t                StatusLedLastToggleMs = 0;
int32_t                 SunsetReturnLastAttemptDateKey = -1;
bool                    SunsetReturnLastAttemptSucceeded = false;
bool                    TrackingTestOverrideActive = false;
float                   TrackingTestOverrideAzimuth = 0.0f;
float                   TrackingTestOverrideElevation = 0.0f;
volatile bool           StepperMicrostepConfigApplyPending = false;
bool                    Stepper1InitOk = false;
bool                    Stepper2InitOk = false;
uint8_t                 TrackingOverrideLastFailureCode = 0u;

FastAccelStepperEngine Engine = FastAccelStepperEngine();
FastAccelStepper*      Stepper1 = nullptr;
FastAccelStepper*      Stepper2 = nullptr;
FastAccelStepper*      Stepper3 = nullptr;

const uint8_t          MosfetPins[3] = {MOSFET_OUTPUT1_PIN, MOSFET_OUTPUT2_PIN, MOSFET_OUTPUT3_PIN};
const uint8_t          StepperDiagPins[3] = {STEPPER1_DIAG_PIN, STEPPER2_DIAG_PIN, STEPPER3_DIAG_PIN};
uint8_t                Pca9538DetectedAddress = PCA9538_I2C_ADDRESS;
bool                   Pca9538AddressDetected = false;

//-------------------------------------------------------------------------------------------------
//  Prototype(s)
//-------------------------------------------------------------------------------------------------
bool IRAM_ATTR TimerHandler(void* Arg);
bool InitSingleStepper(FastAccelStepper*& Stepper, uint8_t StepPin, uint8_t DirPin, const char* Name);
void InitSteppersHardware();
void ApplyMicrostepConfigFromExpander();
void RequestMicrostepConfigApply();
void ProcessPendingMicrostepConfigApply();
bool AreTrackingSteppersReady();
bool ResolveMicrostepPinsFromMode(uint8_t microstepMode, bool& ms1State, bool& ms2State);
bool PCA9538_WriteRegister(uint8_t RegisterAddress, uint8_t Value);
bool PCA9538_ReadRegister(uint8_t RegisterAddress, uint8_t& Value);
bool PCA9538_DetectAddress();
bool PCA9538_ConfigureMicrostepPins();
bool PCA9538_SetPinOutputState(uint8_t Pin, bool HighState);
bool PCA9538_ConfigurePinAsOutput(uint8_t Pin);
bool SetStatusLed(bool IsOn);
bool ToggleStatusLed();
void UpdateStatusLedHeartbeat();
bool ConfigureLimitSwitchInputsOnExpander();
bool IsLimitSwitchActive(uint8_t LimitIndex);
uint8_t ReadLimitSwitchMask();
void SetSystemTimeFromCompileDate();
void InitStepperDiagInputs();
bool IsStepperDiagActive(uint8_t DiagIndex);
uint8_t ReadStepperDiagMask();
void ScanI2cBus();
void InitMosfetOutputs();
bool SetMosfetOutput(uint8_t OutputIndex, bool IsOn);
bool ToggleMosfetOutput(uint8_t OutputIndex);
bool SetMosfetPwmPercent(uint8_t OutputIndex, uint8_t DutyPercent);
uint8_t GetMosfetPwmPercent(uint8_t OutputIndex);
bool IsMosfetOutputOn(uint8_t OutputIndex);
bool IsOutputAutomaticMode(uint8_t OutputIndex);
String GetLimitSwitchSummary();
String GetStepperDiagSummary();
String GetLimitSwitchStateLabel(uint8_t limitIndex);
String GetStepperDiagStateLabel(uint8_t diagIndex);
void InitSensorDataLogging();
void UpdateSensorDataLogging(uint32_t nowMs);
String GetSensorLogsManifestJson();
bool GetSensorLogFileInfo(const String& requestedName, uint32_t& outSizeBytes);
bool ReadSensorLogFileRange(const String& requestedName, uint32_t offsetBytes, uint8_t* pBuffer, size_t lengthBytes);

//-------------------------------------------------------------------------------------------------
// Function(s)
//-------------------------------------------------------------------------------------------------

void setup()
{
    Engine.init();
    Config.Begin();

    Serial.begin(DEFAULT_SERIAL_BAUD_RATE);
    Serial.println();
    Serial.println("Solar Station");
  #ifdef USE_DEBUG_APP
    Serial.println("Initialize server");
  #endif

    InitUart2();
    InitAdcInputs();

    Wire.begin();               // Start I²C
        ScanI2cBus();
  #if USE_BH1750_SENSOR
        if(!LightMeter.begin())
        {
                Serial.println("[BH1750] Sensor init failed (expected address: 0x23 when ADDR is tied to GND).");
        }
  #endif

    InitSensorDataLogging();
    UpdateLoadCurrentTelemetry(millis());
    AmbientLightLux = ReadAmbientLightLux();

  #if USE_M95P32
    Eeprom.Begin();
  #else
   #ifdef USE_DEBUG_APP
    Serial.println("[M95Pxx] Disabled at build time.");
   #endif
  #endif

    InitMosfetOutputs();
    InitStepperDiagInputs();
    InitSteppersHardware();

    azimuthController.init();
    elevationController.init();

    SetSystemTimeFromCompileDate();

#if USE_IO_EXPANDER
    if(!PCA9538_ConfigurePinAsOutput(STATUS_LED_EXP_PIN))
    {
	  #ifdef USE_DEBUG_APP
        Serial.println("Failed to cfg status LED");
	  #endif	
    }
    else
    {
        SetStatusLed(false);
        StatusLedBlinkState = false;
        StatusLedLastToggleMs = millis();
    }

    if(!ConfigureLimitSwitchInputsOnExpander())
    {
	  #ifdef USE_DEBUG_APP
        Serial.println("[LIMIT] Failed to configure limit switches on PCA9538.");
	  #endif	
    }
    else
    {
	  #ifdef USE_DEBUG_APP
        Serial.println("[LIMIT] Limit switches configured on expander IO4..IO7.");
	  #endif	
    }
  #else
   #ifdef USE_DEBUG_APP
    Serial.println("[IOX] IO expander disabled at build time.");
   #endif	
  #endif

    // Initialize networking from Arduino main task to keep lwIP core usage safe on ESP32 core v3.
  #ifdef USE_DEBUG_APP		
    Serial.println("Start WIFI Task");
    Serial.println("[NET] Initializing WiFi and WebServer");
  #endif
    Wifi.Initialize();
  #ifdef USE_DEBUG_APP		
    Serial.println("[NET] WiFi init done");
  #endif
    WebServer.begin();
  #ifdef USE_DEBUG_APP		
    Serial.println("[NET] WebServer started");
  #endif
}

//-------------------------------------------------------------------------------------------------

void SetSystemTimeFromCompileDate()
{
    static const char* monthNames = "JanFebMarAprMayJunJulAugSepOctNovDec";

    char monthText[4] = {__DATE__[0], __DATE__[1], __DATE__[2], '\0'};
    const char* monthPosition = strstr(monthNames, monthText);
    if(monthPosition == nullptr)
    {
        return;
    }

    int month = (int)((monthPosition - monthNames) / 3) + 1;
    int day = atoi(__DATE__ + 4);
    int year = atoi(__DATE__ + 7);
    int hour = atoi(__TIME__);
    int minute = atoi(__TIME__ + 3);
    int second = atoi(__TIME__ + 6);

    auto daysFromCivil = [](int y, unsigned m, unsigned d) -> int32_t
    {
        y -= m <= 2;
        const int era = (y >= 0 ? y : y - 399) / 400;
        const unsigned yoe = (unsigned)(y - era * 400);
        const unsigned doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
        const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
        return era * 146097 + (int32_t)doe - 719468;
    };

    int64_t localEpoch = (int64_t)daysFromCivil(year, (unsigned)month, (unsigned)day) * 86400ll
                       + (int64_t)hour * 3600ll
                       + (int64_t)minute * 60ll
                       + (int64_t)second;

    int32_t utcOffsetSeconds = (int32_t)(GetConfiguredUtcOffsetHours() * 3600.0);
    time_t utcEpoch = (time_t)(localEpoch - utcOffsetSeconds);

    struct timeval tv;
    tv.tv_sec = utcEpoch;
    tv.tv_usec = 0;
    settimeofday(&tv, nullptr);
}

//-------------------------------------------------------------------------------------------------

bool InitSingleStepper(FastAccelStepper*& Stepper, uint8_t StepPin, uint8_t DirPin, const char* Name)
{
    Stepper = Engine.stepperConnectToPin(StepPin);

    if(Stepper == nullptr)
    {
      #ifdef USE_DEBUG_APP	
        Serial.printf("[STEPPER] %s init failed on STEP pin %u\n", Name, StepPin);
      #endif
        return false;
    }

    Stepper->setDirectionPin(DirPin);

      #ifdef USE_DEBUG_APP	
    Serial.printf("[STEPPER] %s ready. STEP=%u DIR=%u\n", Name, StepPin, DirPin);
      #endif
    return true;
}

//-------------------------------------------------------------------------------------------------

void InitSteppersHardware()
{
  #ifdef USE_DEBUG_APP	
    Serial.println("[STEPPER] Initializing 3 steppers...");
  #endif

    // Shared PDN and SPRD lines for all 3 TMC2209 drivers.
    pinMode(STEPPER_PDN_SHARED_PIN, OUTPUT);
    digitalWrite(STEPPER_PDN_SHARED_PIN, STEPPER_PDN_DEFAULT_STATE);

    pinMode(STEPPER_SPRD_SHARED_PIN, OUTPUT);
    digitalWrite(STEPPER_SPRD_SHARED_PIN, STEPPER_SPRD_DEFAULT_STATE);

        Stepper1InitOk = InitSingleStepper(Stepper1, STEPPER1_STEP_PIN, STEPPER1_DIR_PIN, "Stepper1");
        Stepper2InitOk = InitSingleStepper(Stepper2, STEPPER2_STEP_PIN, STEPPER2_DIR_PIN, "Stepper2");
        InitSingleStepper(Stepper3, STEPPER3_STEP_PIN, STEPPER3_DIR_PIN, "Stepper3");

    #ifdef USE_DEBUG_APP
        if(!(Stepper1InitOk && Stepper2InitOk))
        {
                Serial.println("[STEPPER] Tracking axes not fully initialized. Check STEP/DIR pin wiring for Motor1/Motor2.");
        }
    #endif

    ApplyMicrostepConfigFromExpander();
}

//-------------------------------------------------------------------------------------------------

void ApplyMicrostepConfigFromExpander()
{
  #if !USE_IO_EXPANDER
   #ifdef USE_DEBUG_APP	
    Serial.println("[STEPPER] IO expander disabled. Shared MS1/MS2 skipped.");
   #endif
    return;
  #endif

    if(!PCA9538_ConfigureMicrostepPins())
    {
      #ifdef USE_DEBUG_APP	
        Serial.println("[STEPPER] PCA9538 configuration failed. MS pins not applied.");
      #endif
        return;
    }

        bool ms1State = STEPPER_MS1_DEFAULT_STATE;
        bool ms2State = STEPPER_MS2_DEFAULT_STATE;
        uint8_t microstepMode = Config.GetStepperMicrostepMode();
        if(!ResolveMicrostepPinsFromMode(microstepMode, ms1State, ms2State))
        {
            #ifdef USE_DEBUG_APP
                Serial.printf("[STEPPER] Microstep mode %u is not selectable via MS pins, fallback to 8 microsteps.\n", microstepMode);
            #endif
        }

                bool enWriteOk = PCA9538_SetPinOutputState(STEPPER_EN_EXP_PIN, STEPPER_EN_ACTIVE_STATE);
                bool ms2WriteOk = PCA9538_SetPinOutputState(STEPPER_MS2_EXP_PIN, ms2State);
                bool ms1WriteOk = PCA9538_SetPinOutputState(STEPPER_MS1_EXP_PIN, ms1State);

                if(!(enWriteOk && ms1WriteOk && ms2WriteOk))
                {
                    #ifdef USE_DEBUG_APP
                        Serial.println("[STEPPER] Failed to apply EN/MS pins on PCA9538.");
                    #endif
                        return;
                }

  #ifdef USE_DEBUG_APP	
        Serial.printf("[STEPPER] EN/MS applied through PCA9538 (EN=%u, MS1=%u, MS2=%u).\n",
                                    (uint8_t)STEPPER_EN_ACTIVE_STATE,
                                    (uint8_t)ms1State,
                                    (uint8_t)ms2State);
  #endif
}

void RequestMicrostepConfigApply()
{
        StepperMicrostepConfigApplyPending = true;
}

// Keep expander I2C traffic on the Arduino main loop task.
void ProcessPendingMicrostepConfigApply()
{
        if(!StepperMicrostepConfigApplyPending)
        {
                return;
        }

        StepperMicrostepConfigApplyPending = false;
        ApplyMicrostepConfigFromExpander();
}

bool AreTrackingSteppersReady()
{
    return Stepper1InitOk && Stepper2InitOk && (Stepper1 != nullptr) && (Stepper2 != nullptr);
}

bool ResolveMicrostepPinsFromMode(uint8_t microstepMode, bool& ms1State, bool& ms2State)
{
    // TMC2209 standalone pin mapping:
    // MS1=0,MS2=0 => 8  | MS1=1,MS2=1 => 16
    // MS1=1,MS2=0 => 32 | MS1=0,MS2=1 => 64
    switch(microstepMode)
    {
        case 8u:
            ms1State = LOW;
            ms2State = LOW;
            return true;

        case 16u:
            ms1State = HIGH;
            ms2State = HIGH;
            return true;

        case 32u:
            ms1State = HIGH;
            ms2State = LOW;
            return true;

        case 64u:
            ms1State = LOW;
            ms2State = HIGH;
            return true;

        default:
            ms1State = LOW;
            ms2State = LOW;
            return false;
    }
}

//-------------------------------------------------------------------------------------------------

bool PCA9538_WriteRegister(uint8_t RegisterAddress, uint8_t Value)
{
    if(!PCA9538_DetectAddress())
    {
        return false;
    }

    Wire.beginTransmission(Pca9538DetectedAddress);
    Wire.write(RegisterAddress);
    Wire.write(Value);
    return Wire.endTransmission() == 0;
}

//-------------------------------------------------------------------------------------------------

bool PCA9538_DetectAddress()
{
    if(Pca9538AddressDetected)
    {
        return true;
    }

    for(uint8_t address = 0x70; address <= 0x77; address++)
    {
        Wire.beginTransmission(address);
        if(Wire.endTransmission() == 0)
        {
            Pca9538DetectedAddress = address;
            Pca9538AddressDetected = true;
          #ifdef USE_DEBUG_APP
            Serial.printf("[IOX] PCA9538 detected at I2C address 0x%02X.\n", Pca9538DetectedAddress);
          #endif
            return true;
        }
    }

  #ifdef USE_DEBUG_APP
    Serial.println("[IOX] PCA9538 not detected on addresses 0x70..0x77.");
  #endif
    return false;
}

//-------------------------------------------------------------------------------------------------

bool PCA9538_ReadRegister(uint8_t RegisterAddress, uint8_t& Value)
{
    if(!PCA9538_DetectAddress())
    {
        return false;
    }

    Wire.beginTransmission(Pca9538DetectedAddress);
    Wire.write(RegisterAddress);

    if(Wire.endTransmission(false) != 0)
    {
        return false;
    }

    if(Wire.requestFrom((uint8_t)Pca9538DetectedAddress, (uint8_t)1) != 1)
    {
        return false;
    }

    Value = Wire.read();
    return true;
}

//-------------------------------------------------------------------------------------------------

bool PCA9538_ConfigureMicrostepPins()
{
    uint8_t configRegister = 0xFF;

    if(!PCA9538_ReadRegister(PCA9538_REG_CONFIG, configRegister))
    {
      #ifdef USE_DEBUG_APP	
        Serial.println("[STEPPER] PCA9538 not reachable on I2C.");
	  #endif	
        return false;
    }

    configRegister &= ~(1u << STEPPER_MS1_EXP_PIN);
    configRegister &= ~(1u << STEPPER_MS2_EXP_PIN);
    configRegister &= ~(1u << STEPPER_EN_EXP_PIN);

    if(!PCA9538_WriteRegister(PCA9538_REG_CONFIG, configRegister))
    {
        return false;
    }

    return true;
}

//-------------------------------------------------------------------------------------------------

bool PCA9538_SetPinOutputState(uint8_t Pin, bool HighState)
{
    uint8_t outputRegister = 0x00;

    if(Pin > 7)
    {
        return false;
    }

    if(!PCA9538_ReadRegister(PCA9538_REG_OUTPUT, outputRegister))
    {
        return false;
    }

    if(HighState)
    {
        outputRegister |= (1u << Pin);
    }
    else
    {
        outputRegister &= ~(1u << Pin);
    }

    return PCA9538_WriteRegister(PCA9538_REG_OUTPUT, outputRegister);
}

//-------------------------------------------------------------------------------------------------

bool PCA9538_ConfigurePinAsOutput(uint8_t Pin)
{
    uint8_t configRegister = 0xFF;

    if(Pin > 7)
    {
        return false;
    }

    if(!PCA9538_ReadRegister(PCA9538_REG_CONFIG, configRegister))
    {
        return false;
    }

    configRegister &= ~(1u << Pin);
    return PCA9538_WriteRegister(PCA9538_REG_CONFIG, configRegister);
}

//-------------------------------------------------------------------------------------------------

bool SetStatusLed(bool IsOn)
{
  #if !USE_IO_EXPANDER
    VAR_UNUSED(IsOn);
    return false;
  #endif

    return PCA9538_SetPinOutputState(STATUS_LED_EXP_PIN, IsOn ? STATUS_LED_ON_STATE : STATUS_LED_OFF_STATE);
}

//-------------------------------------------------------------------------------------------------

bool ToggleStatusLed()
{
  #if !USE_IO_EXPANDER
    return false;
  #endif

    uint8_t outputRegister = 0x00;

    if(!PCA9538_ReadRegister(PCA9538_REG_OUTPUT, outputRegister))
    {
        return false;
    }

    bool currentState = ((outputRegister >> STATUS_LED_EXP_PIN) & 0x01u) != 0u;
    return SetStatusLed(!currentState);
}

//-------------------------------------------------------------------------------------------------

void UpdateStatusLedHeartbeat()
{
  #if !USE_IO_EXPANDER
    return;
  #endif

    uint32_t now = millis();

    if((now - StatusLedLastToggleMs) >= STATUS_LED_BLINK_INTERVAL_MS)
    {
        StatusLedLastToggleMs = now;
        StatusLedBlinkState = !StatusLedBlinkState;
        SetStatusLed(StatusLedBlinkState);
    }
}

//-------------------------------------------------------------------------------------------------

bool ConfigureLimitSwitchInputsOnExpander()
{
  #if !USE_IO_EXPANDER
    return false;
  #endif

    uint8_t configRegister = 0xFF;

    if(!PCA9538_ReadRegister(PCA9538_REG_CONFIG, configRegister))
    {
        return false;
    }

    // Set IO4..IO7 as inputs
    configRegister |= (1u << LIMIT_SWITCH1_EXP_PIN);
    configRegister |= (1u << LIMIT_SWITCH2_EXP_PIN);
    configRegister |= (1u << LIMIT_SWITCH3_EXP_PIN);
    configRegister |= (1u << LIMIT_SWITCH4_EXP_PIN);

    return PCA9538_WriteRegister(PCA9538_REG_CONFIG, configRegister);
}

//-------------------------------------------------------------------------------------------------

bool IsLimitSwitchActive(uint8_t LimitIndex)
{
  #if !USE_IO_EXPANDER
    VAR_UNUSED(LimitIndex);
    return false;
  #endif

    uint8_t inputRegister = 0xFF;
    uint8_t pin;

    if(LimitIndex > 3)
    {
        return false;
    }

    pin = LIMIT_SWITCH1_EXP_PIN + LimitIndex;

    if(!PCA9538_ReadRegister(PCA9538_REG_INPUT, inputRegister))
    {
        return false;
    }

    bool pinState = ((inputRegister >> pin) & 0x01u) != 0u;

    if(LIMIT_SWITCH_ACTIVE_STATE == LOW)
    {
        return !pinState;
    }

    return pinState;
}

//-------------------------------------------------------------------------------------------------

uint8_t ReadLimitSwitchMask()
{
  #if !USE_IO_EXPANDER
    return 0;
  #endif

    uint8_t mask = 0;

    if(IsLimitSwitchActive(0)) { mask |= 0x01u; }
    if(IsLimitSwitchActive(1)) { mask |= 0x02u; }
    if(IsLimitSwitchActive(2)) { mask |= 0x04u; }
    if(IsLimitSwitchActive(3)) { mask |= 0x08u; }

    return mask;
}

//-------------------------------------------------------------------------------------------------

void InitStepperDiagInputs()
{
    // GPIO34/GPIO35 are input-only; GPIO32 is standard input-capable.
    pinMode(STEPPER1_DIAG_PIN, INPUT);
    pinMode(STEPPER2_DIAG_PIN, INPUT);
    pinMode(STEPPER3_DIAG_PIN, INPUT);

  #ifdef USE_DEBUG_APP	
    Serial.printf("[STEPPER] DIAG inputs ready. D1=%u D2=%u D3=%u\n", STEPPER1_DIAG_PIN, STEPPER2_DIAG_PIN, STEPPER3_DIAG_PIN);
  #endif
}

//-------------------------------------------------------------------------------------------------

void ScanI2cBus()
{
    uint8_t foundCount = 0;

    Serial.println("[I2C] Scanning bus...");

    for(uint8_t address = 1; address < 127; address++)
    {
        Wire.beginTransmission(address);
        uint8_t error = Wire.endTransmission();

        if(error == 0)
        {
            foundCount++;
            Serial.printf("[I2C] Device found at 0x%02X\n", address);
        }
    }

    if(foundCount == 0)
    {
        Serial.println("[I2C] No devices found.");
        return;
    }

    Serial.printf("[I2C] Scan complete. %u device(s) found.\n", foundCount);
}

//-------------------------------------------------------------------------------------------------

bool IsStepperDiagActive(uint8_t DiagIndex)
{
    if(DiagIndex >= 3)
    {
        return false;
    }

    return digitalRead(StepperDiagPins[DiagIndex]) == STEPPER_DIAG_ACTIVE_STATE;
}

//-------------------------------------------------------------------------------------------------

uint8_t ReadStepperDiagMask()
{
    uint8_t mask = 0;

    if(IsStepperDiagActive(0)) { mask |= 0x01u; }
    if(IsStepperDiagActive(1)) { mask |= 0x02u; }
    if(IsStepperDiagActive(2)) { mask |= 0x04u; }

    return mask;
}

//-------------------------------------------------------------------------------------------------

void InitMosfetOutputs()
{
    for(uint8_t i = 0; i < 3; i++)
    {
        ledcAttach(MosfetPins[i], MOSFET_PWM_FREQ_HZ, MOSFET_PWM_RES_BITS);
        ledcWrite(MosfetPins[i], 0); // Default OFF at boot
    }

  #ifdef USE_DEBUG_APP	
    Serial.printf("[MOSFET] Outputs ready on GPIO %u, %u, %u (PWM %u Hz, %u bits).\n",
					MosfetPins[0], MosfetPins[1], MosfetPins[2],
                    MOSFET_PWM_FREQ_HZ, MOSFET_PWM_RES_BITS);
  #endif
}

//-------------------------------------------------------------------------------------------------

bool SetMosfetOutput(uint8_t OutputIndex, bool IsOn)
{
    if(OutputIndex >= 3)
    {
        return false;
    }

    uint16_t duty = IsOn ? ((1u << MOSFET_PWM_RES_BITS) - 1u) : 0u;
    ledcWrite(MosfetPins[OutputIndex], duty);
    return true;
}

//-------------------------------------------------------------------------------------------------

bool ToggleMosfetOutput(uint8_t OutputIndex)
{
    if(OutputIndex >= 3)
    {
        return false;
    }

    uint32_t currentDuty = ledcRead(MosfetPins[OutputIndex]);
    uint16_t dutyOn = (1u << MOSFET_PWM_RES_BITS) - 1u;

    ledcWrite(MosfetPins[OutputIndex], (currentDuty > 0u) ? 0u : dutyOn);
    return true;
}

//-------------------------------------------------------------------------------------------------

bool SetMosfetPwmPercent(uint8_t OutputIndex, uint8_t DutyPercent)
{
    if(OutputIndex >= 3)
    {
        return false;
    }

    if(DutyPercent > 100u)
    {
        DutyPercent = 100u;
    }

    uint16_t dutyMax = (1u << MOSFET_PWM_RES_BITS) - 1u;
    uint16_t duty = (uint16_t)(((uint32_t)DutyPercent * dutyMax) / 100u);

    ledcWrite(MosfetPins[OutputIndex], duty);
    return true;
}

//-------------------------------------------------------------------------------------------------

uint8_t GetMosfetPwmPercent(uint8_t OutputIndex)
{
    if(OutputIndex >= 3)
    {
        return 0;
    }

    uint16_t dutyMax = (1u << MOSFET_PWM_RES_BITS) - 1u;
    uint32_t duty = ledcRead(MosfetPins[OutputIndex]);

    return (uint8_t)((duty * 100u) / dutyMax);
}

//-------------------------------------------------------------------------------------------------

bool IsMosfetOutputOn(uint8_t OutputIndex)
{
    return GetMosfetPwmPercent(OutputIndex) > 0u;
}

//-------------------------------------------------------------------------------------------------

bool IsOutputAutomaticMode(uint8_t OutputIndex)
{
    if(OutputIndex >= 3)
    {
        return false;
    }

    return Config.GetOutputAutomaticMode(OutputIndex);
}

//-------------------------------------------------------------------------------------------------

String GetLimitSwitchSummary()
{
  #if !USE_IO_EXPANDER
    return "Disabled";
  #endif

    uint8_t limits = ReadLimitSwitchMask();
    char buffer[48];
    snprintf(buffer, sizeof(buffer), "L1:%s L2:%s L3:%s L4:%s",
             (limits & 0x01u) ? "ON" : "OFF",
             (limits & 0x02u) ? "ON" : "OFF",
             (limits & 0x04u) ? "ON" : "OFF",
             (limits & 0x08u) ? "ON" : "OFF");
    return String(buffer);
}

//-------------------------------------------------------------------------------------------------

String GetLimitSwitchStateLabel(uint8_t limitIndex)
{
  #if !USE_IO_EXPANDER
    VAR_UNUSED(limitIndex);
    return "DISABLED";
  #endif

    if(limitIndex >= 4)
    {
        return "INVALID";
    }

    return IsLimitSwitchActive(limitIndex) ? "ACTIVE" : "IDLE";
}

//-------------------------------------------------------------------------------------------------

String GetStepperDiagSummary()
{
    uint8_t diag = ReadStepperDiagMask();
    char buffer[40];
    snprintf(buffer, sizeof(buffer), "D1:%s D2:%s D3:%s",
             (diag & 0x01u) ? "FAULT" : "OK",
             (diag & 0x02u) ? "FAULT" : "OK",
             (diag & 0x04u) ? "FAULT" : "OK");
    return String(buffer);
}

//-------------------------------------------------------------------------------------------------

String GetStepperDiagStateLabel(uint8_t diagIndex)
{
    if(diagIndex >= 3)
    {
        return "INVALID";
    }

    return IsStepperDiagActive(diagIndex) ? "FAULT" : "OK";
}

//-------------------------------------------------------------------------------------------------

void loop() // Became Idle task
{
    static uint32_t lastScheduleUpdate = 0;
    static uint32_t lastSunsetReturnCheck = 0;
    static uint32_t lastPanelAdjustment = 0;
    static bool trackingInitialized = false;
    uint32_t now = millis();

    // Initialize tracking with calculated solar position on first valid time (after NTP sync or fallback).
    // This prevents unwanted motor movement at boot-up.
    if(!trackingInitialized && IsSystemTimeValid())
    {
        trackingInitialized = true;
        InitializeTrackingFromSolarPosition();
    }

    UpdateStatusLedHeartbeat();
    ProcessPendingMicrostepConfigApply();

    if(!TrackingTestOverrideActive)
    {
        if((now - lastPanelAdjustment) >= PANEL_ADJUSTMENT_INTERVAL_MS)
        {
            lastPanelAdjustment = now;
            MovePanelToCurrentCalculatedPosition();
        }
    }

    if((now - lastScheduleUpdate) >= OUTPUT_SCHEDULE_UPDATE_MS)
    {
        lastScheduleUpdate = now;
        ApplyOutputSchedules();
    }

    if((now - lastSunsetReturnCheck) >= SUNSET_RETURN_CHECK_MS)
    {
        lastSunsetReturnCheck = now;
        ApplySunsetReturnPreposition();
    }

    UpdateMpptTelemetry(now);
    UpdateLoadCurrentTelemetry(now);
    
    UpdateSensorDataLogging(now);

    Wifi.Loop();
    WebServer.Loop();

    if(WebServer.isRestartRequested())
    {
      #ifdef USE_DEBUG_APP	
        Serial.println(F("\n\t--- Restarting System ---\n"));
	  #endif	
        ESP.restart();
    }
}

//-------------------------------------------------------------------------------------------------

static bool IsSixDigits(const String& value)
{
    if(value.length() != 6)
    {
        return false;
    }

    for(size_t i = 0; i < value.length(); i++)
    {
        if(value[i] < '0' || value[i] > '9')
        {
            return false;
        }
    }

    return true;
}

//-------------------------------------------------------------------------------------------------

static String BuildSensorLogPath(const char* prefix, const String& dayTag)
{
    return String("/") + prefix + "_" + dayTag + ".bin";
}

//-------------------------------------------------------------------------------------------------

static bool ParseSensorLogFileName(const String& rawName, String& typeName, String& dayTag)
{
    String name = rawName;

    if(name.startsWith("/"))
    {
        name = name.substring(1);
    }

    const char* prefixes[] = {"ADC_Load1_", "ADC_Load2_", "LUX_"};
    const char* types[] = {"ADC_LOAD1", "ADC_LOAD2", "LUX"};

    for(size_t i = 0; i < 3; i++)
    {
        String prefix = String(prefixes[i]);
        if(!name.startsWith(prefix) || !name.endsWith(".bin"))
        {
            continue;
        }

        String day = name.substring(prefix.length(), name.length() - 4);
        if(!IsSixDigits(day))
        {
            return false;
        }

        typeName = String(types[i]);
        dayTag = day;
        return true;
    }

    return false;
}

//-------------------------------------------------------------------------------------------------

static bool BuildCurrentLogDay(String& dayTag, int32_t& dayKey)
{
    if(!IsSystemTimeValid())
    {
        return false;
    }

    time_t localNow = GetLocalTimeFromConfigOffset();
    struct tm localTimeInfo;
    if(gmtime_r(&localNow, &localTimeInfo) == nullptr)
    {
        return false;
    }

    int year = (localTimeInfo.tm_year + 1900) % 100;
    int month = localTimeInfo.tm_mon + 1;
    int day = localTimeInfo.tm_mday;

    char buffer[7];
    snprintf(buffer, sizeof(buffer), "%02d%02d%02d", year, month, day);
    dayTag = String(buffer);
    dayKey = (year * 10000) + (month * 100) + day;
    return true;
}

//-------------------------------------------------------------------------------------------------

static String DayKeyToTag(int32_t dayKey)
{
    char buffer[7];
    snprintf(buffer, sizeof(buffer), "%06ld", (long)dayKey);
    return String(buffer);
}

//-------------------------------------------------------------------------------------------------

static uint32_t GetDaySlotBaseAddress(uint8_t slotIndex)
{
    return M95_LOG_DATA_START_ADDR + ((uint32_t)slotIndex * M95_LOG_DAY_SLOT_BYTES);
}

//-------------------------------------------------------------------------------------------------

static bool PersistLogMeta()
{
  #if !USE_M95P32
    return false;
  #else
    if(!Eeprom.IsReady())
    {
        return false;
    }

    return Eeprom.WriteData(M95_LOG_META_ADDR, (const uint8_t*)&M95LogMeta, sizeof(M95LogMeta));
  #endif
}

//-------------------------------------------------------------------------------------------------

static bool LoadOrCreateLogMeta()
{
  #if !USE_M95P32
    return false;
  #else
    if(!Eeprom.IsReady())
    {
        return false;
    }

    if(!Eeprom.ReadData(M95_LOG_META_ADDR, (uint8_t*)&M95LogMeta, sizeof(M95LogMeta)))
    {
        return false;
    }

    bool validMagic = true;
    for(size_t i = 0; i < sizeof(M95_LOG_MAGIC); i++)
    {
        if(M95LogMeta.Magic[i] != M95_LOG_MAGIC[i])
        {
            validMagic = false;
            break;
        }
    }

    if(!validMagic || M95LogMeta.Version != M95_LOG_VERSION)
    {
        memset(&M95LogMeta, 0, sizeof(M95LogMeta));
        memcpy(M95LogMeta.Magic, M95_LOG_MAGIC, sizeof(M95_LOG_MAGIC));
        M95LogMeta.Version = M95_LOG_VERSION;
        M95LogMeta.NextSequence = 1;

        if(!PersistLogMeta())
        {
            return false;
        }
    }

    M95LogMetaLoaded = true;
    return true;
  #endif
}

//-------------------------------------------------------------------------------------------------

static uint32_t GetUsedLogBytesEstimate()
{
    uint32_t total = 0;
    for(uint8_t i = 0; i < M95_LOG_DAY_SLOT_COUNT; i++)
    {
        const SensorLogDayEntry& day = M95LogMeta.Days[i];
        if(day.Active == 0)
        {
            continue;
        }

        total += ((uint32_t)day.Load1Count * sizeof(float));
        total += ((uint32_t)day.Load2Count * sizeof(float));
        total += ((uint32_t)day.LuxCount * sizeof(float));
    }

    return total;
}

//-------------------------------------------------------------------------------------------------

static int8_t FindOldestActiveDaySlot()
{
    int8_t oldestSlot = -1;
    uint32_t oldestSeq = 0xFFFFFFFFu;

    for(uint8_t i = 0; i < M95_LOG_DAY_SLOT_COUNT; i++)
    {
        const SensorLogDayEntry& day = M95LogMeta.Days[i];
        if(day.Active == 0)
        {
            continue;
        }

        if(day.Sequence < oldestSeq)
        {
            oldestSeq = day.Sequence;
            oldestSlot = (int8_t)i;
        }
    }

    return oldestSlot;
}

//-------------------------------------------------------------------------------------------------

static void EnforceLogRetentionIfNeeded()
{
  #if !USE_M95P32
    return;
  #else
    const size_t thresholdBytes = (Eeprom.LittleFsTotalBytes() * LOG_RETENTION_THRESHOLD_PERCENT) / 100u;

    while(Eeprom.LittleFsUsedBytes() > thresholdBytes)
    {
        String filesJson = Eeprom.LittleFsListFilesJson();
        int32_t oldestDayKey = -1;

        int start = 0;
        while(true)
        {
            int namePos = filesJson.indexOf("\"name\":\"", start);
            if(namePos < 0)
            {
                break;
            }

            int valueStart = namePos + 8;
            int valueEnd = filesJson.indexOf('"', valueStart);
            if(valueEnd < 0)
            {
                break;
            }

            String fileName = filesJson.substring(valueStart, valueEnd);
            String typeName;
            String dayTag;
            if(ParseSensorLogFileName(fileName, typeName, dayTag))
            {
                int32_t dayKey = dayTag.toInt();
                if(oldestDayKey < 0 || dayKey < oldestDayKey)
                {
                    oldestDayKey = dayKey;
                }
            }

            start = valueEnd + 1;
        }

        if(oldestDayKey < 0)
        {
            break;
        }

        String oldestTag = DayKeyToTag(oldestDayKey);
        Eeprom.LittleFsRemove(BuildSensorLogPath("ADC_Load1", oldestTag));
        Eeprom.LittleFsRemove(BuildSensorLogPath("ADC_Load2", oldestTag));
        Eeprom.LittleFsRemove(BuildSensorLogPath("LUX", oldestTag));
    }
  #endif
}

//-------------------------------------------------------------------------------------------------

static int8_t FindDaySlotByKey(int32_t dayKey)
{
    for(uint8_t i = 0; i < M95_LOG_DAY_SLOT_COUNT; i++)
    {
        if(M95LogMeta.Days[i].Active != 0 && (int32_t)M95LogMeta.Days[i].DayKey == dayKey)
        {
            return (int8_t)i;
        }
    }

    return -1;
}

//-------------------------------------------------------------------------------------------------

static int8_t FindFreeDaySlot()
{
    for(uint8_t i = 0; i < M95_LOG_DAY_SLOT_COUNT; i++)
    {
        if(M95LogMeta.Days[i].Active == 0)
        {
            return (int8_t)i;
        }
    }

    return -1;
}

//-------------------------------------------------------------------------------------------------

static bool EnsureLogDayReady()
{
  #if !USE_M95P32
    return false;
  #else
    String dayTag;
    int32_t dayKey = -1;

    if(!BuildCurrentLogDay(dayTag, dayKey))
    {
        return false;
    }

    if(CurrentLogDayKey != dayKey)
    {
        EnforceLogRetentionIfNeeded();
        CurrentLogDayKey = dayKey;
        CurrentLogDayTag = dayTag;
      #ifdef USE_DEBUG_APP	
          Serial.printf("[LOG] New log day: %s\n", CurrentLogDayTag.c_str());
	  #endif
    }

    return true;
  #endif
}

//-------------------------------------------------------------------------------------------------

static bool AppendFloatToM95DayChannel(uint8_t daySlot, const char* prefix, float value)
{
  #if !USE_M95P32
    VAR_UNUSED(daySlot);
    VAR_UNUSED(prefix);
    VAR_UNUSED(value);
    return false;
  #else
    VAR_UNUSED(daySlot);

    if(CurrentLogDayTag.length() != 6)
    {
        return false;
    }

    String path = BuildSensorLogPath(prefix, CurrentLogDayTag);
    return Eeprom.LittleFsAppend(path, (const uint8_t*)&value, sizeof(value));
  #endif
}

//-------------------------------------------------------------------------------------------------

static void FlushLogChannelIfNeeded(SensorLogChannelState& state,
                                    uint32_t nowMs,
                                    uint32_t flushIntervalMs,
                                    const char* prefix)
{
    if((nowMs - state.LastFlushMs) < flushIntervalMs)
    {
        return;
    }

    state.LastFlushMs = nowMs;

    if(state.Count == 0)
    {
        return;
    }

    if(!EnsureLogDayReady())
    {
        state.Sum = 0.0f;
        state.Count = 0;
        return;
    }

    float average = state.Sum / (float)state.Count;
    if(!AppendFloatToM95DayChannel(0, prefix, average))
    {
      #ifdef USE_DEBUG_APP	
        Serial.printf("[LOG] Write failed: %s_%s.bin\n", prefix, CurrentLogDayTag.c_str());
	  #endif	
    }

    state.Sum = 0.0f;
    state.Count = 0;
}

//-------------------------------------------------------------------------------------------------

void InitSensorDataLogging()
{
  #if !USE_M95P32
   #ifdef USE_DEBUG_APP	
    Serial.println("[LOG] M95P32 logging disabled at build time.");
   #endif  
	return;
  #else
    uint32_t nowMs = millis();
    Load1LogState.LastSampleMs = nowMs;
    Load1LogState.LastFlushMs = nowMs;
    Load1LogState.Sum = 0.0f;
    Load1LogState.Count = 0;

    Load2LogState.LastSampleMs = nowMs;
    Load2LogState.LastFlushMs = nowMs;
    Load2LogState.Sum = 0.0f;
    Load2LogState.Count = 0;

    LuxLogState.LastSampleMs = nowMs;
    LuxLogState.LastFlushMs = nowMs;
    LuxLogState.Sum = 0.0f;
    LuxLogState.Count = 0;
   #ifdef USE_DEBUG_APP	
    Serial.printf("[LOG] Ready. M95 LittleFS used=%u total=%u\n", (uint32_t)Eeprom.LittleFsUsedBytes(), (uint32_t)Eeprom.LittleFsTotalBytes());
   #endif	
  #endif
}

//-------------------------------------------------------------------------------------------------

void UpdateSensorDataLogging(uint32_t nowMs)
{
    if((nowMs - Load1LogState.LastSampleMs) >= ADC_LOG_SAMPLE_INTERVAL_MS)
    {
        Load1LogState.LastSampleMs = nowMs;
        Load1LogState.Sum += Load1CurrentA;
        Load1LogState.Count++;
    }

    if((nowMs - Load2LogState.LastSampleMs) >= ADC_LOG_SAMPLE_INTERVAL_MS)
    {
        Load2LogState.LastSampleMs = nowMs;
        Load2LogState.Sum += Load2CurrentA;
        Load2LogState.Count++;
    }

    if((nowMs - LuxLogState.LastSampleMs) >= LUX_LOG_SAMPLE_INTERVAL_MS)
    {
        LuxLogState.LastSampleMs = nowMs;
        AmbientLightLux = ReadAmbientLightLux();
        LuxLogState.Sum += AmbientLightLux;
        LuxLogState.Count++;
    }

    FlushLogChannelIfNeeded(Load1LogState, nowMs, ADC_LOG_AVG_WINDOW_MS, "ADC_Load1");
    FlushLogChannelIfNeeded(Load2LogState, nowMs, ADC_LOG_AVG_WINDOW_MS, "ADC_Load2");
    FlushLogChannelIfNeeded(LuxLogState, nowMs, LUX_LOG_AVG_WINDOW_MS, "LUX");
}

//-------------------------------------------------------------------------------------------------

String GetSensorLogsManifestJson()
{
  #if !USE_M95P32
    return "{\"usedBytes\":0,\"totalBytes\":0,\"files\":[]}";
  #else
    if(!Eeprom.IsLittleFsMounted() && !Eeprom.MountLittleFs(true))
    {
        return "{\"usedBytes\":0,\"totalBytes\":0,\"files\":[]}";
    }

    String payload;
    payload.reserve(2048);
    payload += "{\"usedBytes\":";
    payload += String((uint32_t)Eeprom.LittleFsUsedBytes());
    payload += ",\"totalBytes\":";
    payload += String((uint32_t)Eeprom.LittleFsTotalBytes());
    payload += ",\"files\":[";

    String filesJson = Eeprom.LittleFsListFilesJson();
    bool first = true;
    int start = 0;
    while(true)
    {
        int namePos = filesJson.indexOf("\"name\":\"", start);
        if(namePos < 0)
        {
            break;
        }

        int nameStart = namePos + 8;
        int nameEnd = filesJson.indexOf('"', nameStart);
        if(nameEnd < 0)
        {
            break;
        }

        String name = filesJson.substring(nameStart, nameEnd);

        int sizePos = filesJson.indexOf("\"size\":", nameEnd);
        if(sizePos < 0)
        {
            break;
        }
        int sizeStart = sizePos + 7;
        int sizeEnd = filesJson.indexOf('}', sizeStart);
        if(sizeEnd < 0)
        {
            break;
        }

        String sizeText = filesJson.substring(sizeStart, sizeEnd);
        uint32_t sizeBytes = (uint32_t)sizeText.toInt();

        String typeName;
        String dayTag;
        if(ParseSensorLogFileName(name, typeName, dayTag))
        {
            if(!first)
            {
                payload += ",";
            }
            first = false;

            payload += "{\"name\":\"";
            payload += name;
            payload += "\",\"type\":\"";
            payload += typeName;
            payload += "\",\"date\":\"";
            payload += dayTag;
            payload += "\",\"bytes\":";
            payload += String((uint32_t)sizeBytes);
            payload += "}";
        }

        start = sizeEnd + 1;
    }

    payload += "]}";
    return payload;
  #endif
}

//-------------------------------------------------------------------------------------------------

bool GetSensorLogFileInfo(const String& requestedName, uint32_t& outSizeBytes)
{
  #if !USE_M95P32
    VAR_UNUSED(requestedName);
    outSizeBytes = 0;
    return false;
  #else
    if(requestedName.length() == 0 || requestedName.indexOf("/") >= 0 || requestedName.indexOf("\\") >= 0 || requestedName.indexOf("..") >= 0)
    {
        return false;
    }

    String typeName;
    String dayTag;

    if(!ParseSensorLogFileName(requestedName, typeName, dayTag))
    {
        return false;
    }

    if(!Eeprom.IsLittleFsMounted() && !Eeprom.MountLittleFs(true))
    {
        return false;
    }

    size_t sizeBytes = 0;
    String path = String("/") + requestedName;
    if(!Eeprom.LittleFsGetFileSize(path, sizeBytes))
    {
        return false;
    }

    VAR_UNUSED(typeName);
    VAR_UNUSED(dayTag);
    outSizeBytes = (uint32_t)sizeBytes;
    return true;
  #endif
}

//-------------------------------------------------------------------------------------------------

bool ReadSensorLogFileRange(const String& requestedName, uint32_t offsetBytes, uint8_t* pBuffer, size_t lengthBytes)
{
  #if !USE_M95P32
    VAR_UNUSED(requestedName);
    VAR_UNUSED(offsetBytes);
    VAR_UNUSED(pBuffer);
    VAR_UNUSED(lengthBytes);
    return false;
  #else
    if(pBuffer == nullptr || lengthBytes == 0)
    {
        return false;
    }

    if(!Eeprom.IsLittleFsMounted() && !Eeprom.MountLittleFs(true))
    {
        return false;
    }

    String typeName;
    String dayTag;
    if(!ParseSensorLogFileName(requestedName, typeName, dayTag))
    {
        return false;
    }

    uint32_t totalSizeBytes = 0;
    if(!GetSensorLogFileInfo(requestedName, totalSizeBytes))
    {
        return false;
    }

    if(offsetBytes + lengthBytes > totalSizeBytes)
    {
        return false;
    }

    size_t outRead = 0;
    String path = String("/") + requestedName;
    if(!Eeprom.LittleFsReadRange(path, offsetBytes, pBuffer, lengthBytes, outRead))
    {
        return false;
    }

    VAR_UNUSED(typeName);
    VAR_UNUSED(dayTag);
    return outRead == lengthBytes;
  #endif
}

//-------------------------------------------------------------------------------------------------

bool IRAM_ATTR TimerHandler(void* Arg)
{
    VAR_UNUSED(Arg);
    return false;
}

/*
Stepper usage examples (FastAccelStepper)

Example 1: Continuous forward on Stepper1
    if(Stepper1 != nullptr)
    {
        Stepper1->setEnablePin(STEPPER_PDN_SHARED_PIN, true);  // Optional, if used as enable
        Stepper1->setAutoEnable(true);
        Stepper1->setSpeedInHz(800);        // step rate
        Stepper1->setAcceleration(2000);    // steps/s^2
        Stepper1->runForward();
    }

Example 2: Move Stepper2 by fixed steps
    if(Stepper2 != nullptr)
    {
        Stepper2->setSpeedInHz(1200);
        Stepper2->setAcceleration(3000);
        Stepper2->move(1600);               // positive = one direction, negative = opposite
        // Optional wait loop:
        while(Stepper2->isRunning()) { delay(1); }
    }

Example 3: Stop Stepper3
    if(Stepper3 != nullptr)
    {
        Stepper3->stopMove();               // decelerated stop
        // or Stepper3->forceStop();        // immediate stop (hard stop)
    }

Example 4: Limit switch handling using PCA9538 IO4..IO7
    uint8_t limits = ReadLimitSwitchMask();
    if((limits & 0x01u) != 0u)
    {
        // Limit switch #1 active -> stop related axis
        if(Stepper1 != nullptr) { Stepper1->stopMove(); }
    }
*/

