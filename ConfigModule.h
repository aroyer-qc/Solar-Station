#ifndef CONFIG_MODULE_H
#define CONFIG_MODULE_H

#include <Arduino.h>
#include <Preferences.h>

struct OutputSchedule_t
{
    bool            Enabled;
    uint8_t         StartType;
    uint8_t         StartHour;
    uint8_t         StartMinute;
    int16_t         StartOffsetMinutes;
    uint8_t         EndType;
    uint8_t         EndHour;
    uint8_t         EndMinute;
    int16_t         EndOffsetMinutes;
    uint8_t         DutyPercent;
};


struct ConfigData_t
{
    // Constants
    uint8_t         ReservedConstant0;

    // Solar Tracking Settings
    double          ST_Latitude;                    // Latitude of the solar panel (in degrees)
    double          ST_Longitude;                   // Longitude of the solar panel (in degrees)
    double          ST_Altitude;                    // Altitude of the site (in meters)
    double          ST_TimeZoneOffset;              // Local time zone offset from UTC (hours)
    bool            ST_UseDST;                      // Apply daylight saving time (+1h)
    uint8_t         ST_ReservedFlag0;               // Reserved for backward-compatible blob layout
    uint16_t        ST_ReservedValue0;              // Reserved for backward-compatible blob layout
    double          ST_Pressure;                    // Atmospheric pressure in kPa
    double          ST_Temperature;                 // Atmospheric temperature in Kelvin

    // Azimuth Settings
    float           AzimuthDegMAX;                  // Maximum azimuth value (degrees)
    float           AzimuthDegMIN;                  // Minimum azimuth value (degrees)
    float           AzimuthStepsPerDegree;          // Legacy stored ratio (computed value is derived from shared stepper config)
    uint32_t        AzimuthStepSpeedHz;             // Azimuth speed in steps per second
    uint32_t        AzimuthStepAcceleration;        // Azimuth acceleration in steps per second^2
    uint32_t        AzimuthTimeThreshold;           // Threshold in milliseconds to trigger motor adjustment
    uint32_t        AzimuthTimeMaxB4_Calibration;   // Max time before entering error mode
    float           AzimuthGearReduction;           // Azimuth gearbox/mechanical reduction ratio

    // Elevation Settings
    float           ElevationDegMAX;                // Maximum elevation value (degrees)
    float           ElevationDegMIN;                // Minimum elevation value (degrees)
    float           ElevationStepsPerDegree;        // Legacy stored ratio (computed value is derived from shared stepper config)
    uint32_t        ElevationStepSpeedHz;           // Elevation speed in steps per second
    uint32_t        ElevationStepAcceleration;      // Elevation acceleration in steps per second^2
    uint32_t        ElevationTimeThreshold;         // Threshold in milliseconds to trigger motor adjustment
    float           ElevationGearReduction;         // Elevation gearbox/mechanical reduction ratio

    // Shared stepper settings (legacy compatibility)
    uint16_t        StepperMotorStepsPerRevolution; // Legacy fallback value for migration
    uint8_t         StepperMicrostepMode;           // Allowed values: 8, 16, 32, 64
    uint8_t         ReservedStepperSettings[1];

    // NTP Settings
    char            NTP_Server1[64];
    char            NTP_Server2[64];
    char            NTP_Server3[64];

    // WIFI Credentials
    char            WifiSSID[32];
    char            WIFI_Password[32];
    char            WifiSSID2[32];
    char            WIFI_Password2[32];
    bool            WIFI_UseDHCP;
    char            WIFI_StaticIP[16];
    char            WIFI_Gateway[16];
    char            WIFI_SubnetMask[16];
    char            WIFI_DNS1[16];

    // Output schedules (daily settings)
    OutputSchedule_t OutputSchedules[3];
};


class ConfigModule
{
    public:
    static constexpr uint8_t OUTPUT_COUNT = 3u;
    static constexpr uint8_t OUTPUT_SCHEDULE_SLOT_COUNT = 2u;
    static constexpr uint8_t OUTPUT_SCHEDULE_DAYS_ALL = 0x7Fu;
    static constexpr uint8_t OUTPUT_NAME_SIZE = 17u;             // 16 visible characters plus the terminator
  
                        ConfigModule                            ();
        void            Begin                                   ();
        bool            LoadConfig                              ();
        void            SaveConfig                              ();
        ConfigData_t&   GetConfig                               ();

        void            ResetConfig                             ();
        void            ResetConstantsConfig                    ();
        void            ResetSolarTrackingConfig                ();
        void            ResetAzimuthConfig                      ();
        void            ResetElevationConfig                    ();
        void            ResetNTP_Config                         ();
        void            ResetWiFiConfig                         ();
        void            ResetOutputScheduleConfig               ();
        void            ResetOutputAutomaticModeConfig          ();
        void            ResetOutputNameConfig                   ();

        void            PrintConfig                             ();

        // Setters
        void            SetST_Latitude                          (double value);
        void            SetST_Longitude                         (double value);
        void            SetST_Altitude                          (double value);
        void            SetST_TimeZoneOffset                    (double value);
        void            SetST_UseDST                            (bool value);
        void            SetST_Pressure                          (double value);
        void            SetST_Temperature                       (double value);

        void            SetAzimuthDegMax                        (float value);
        void            SetAzimuthDegMin                        (float value);
        void            SetAzimuthStepsPerDegree                (float value);
        void            SetAzimuthStepSpeedHz                   (uint32_t value);
        void            SetAzimuthStepAcceleration              (uint32_t value);
        void            SetAzimuthTimeThreshold                 (uint32_t value);
        void            SetAzimuthTimeMaxBeforeCalibration      (uint32_t value);
        void            SetAzimuthGearReduction                 (float value);

        void            SetElevationDegMax                      (float value);
        void            SetElevationDegMin                      (float value);
        void            SetElevationStepsPerDegree              (float value);
        void            SetElevationStepSpeedHz                 (uint32_t value);
        void            SetElevationStepAcceleration            (uint32_t value);
        void            SetElevationTimeThreshold               (uint32_t value);
        void            SetElevationGearReduction               (float value);

        void            SetAzimuthMotorStepsPerRevolution       (uint16_t value);
        void            SetElevationMotorStepsPerRevolution     (uint16_t value);
        void            SetStepper3MotorStepsPerRevolution      (uint16_t value);
        void            SetStepperMotorStepsPerRevolution       (uint16_t value);
        void            SetStepperMicrostepMode                 (uint8_t value);

        void            SetNTP_Server1                          (const char *value);
        void            SetNTP_Server2                          (const char *value);
        void            SetNTP_Server3                          (const char *value);

        void            SetWIFI_SSID                            (const char *value);
        void            SetWIFI_Password                        (const char *value);
        void            SetWIFI_SSID2                           (const char *value);
        void            SetWIFI_Password2                       (const char *value);
        void            SetWIFI_UseDHCP                         (bool value);
        void            SetWIFI_StaticIP                        (const char *value);
        void            SetWIFI_Gateway                         (const char *value);
        void            SetWIFI_SubnetMask                      (const char *value);
        void            SetWIFI_DNS1                            (const char *value);
        void            SetOutputSchedule                       (uint8_t index, const OutputSchedule_t& schedule);
        void            SetOutputSchedule                       (uint8_t index, uint8_t slotIndex, const OutputSchedule_t& schedule);
        void            SetOutputScheduleActiveDaysMask         (uint8_t index, uint8_t slotIndex, uint8_t activeDaysMask);
        void            SetOutputAutomaticMode                  (uint8_t index, bool automaticMode);
        void            SetOutputName                           (uint8_t index, const char *value);

        double          GetST_Latitude                          ()                  { return m_ConfigData.ST_Latitude; }
        double          GetST_Longitude                         ()                  { return m_ConfigData.ST_Longitude; }
        double          GetST_Altitude                          ()                  { return m_ConfigData.ST_Altitude; }
        double          GetST_TimeZoneOffset                    ()                  { return m_ConfigData.ST_TimeZoneOffset; }
        bool            GetST_UseDST                            ()                  { return m_ConfigData.ST_UseDST; }
        double          GetST_Pressure                          ()                  { return m_ConfigData.ST_Pressure; }
        double          GetST_Temperature                       ()                  { return m_ConfigData.ST_Temperature; }

        float           GetAzimuthDegMax                        ()                  { return m_ConfigData.AzimuthDegMAX; }
        float           GetAzimuthDegMin                        ()                  { return m_ConfigData.AzimuthDegMIN; }
        float           GetAzimuthStepsPerDegree                ();
        uint32_t        GetAzimuthStepSpeedHz                   ()                  { return m_ConfigData.AzimuthStepSpeedHz; }
        uint32_t        GetAzimuthStepAcceleration              ()                  { return m_ConfigData.AzimuthStepAcceleration; }
        uint32_t        GetAzimuthTimeThreshold                 ()                  { return m_ConfigData.AzimuthTimeThreshold; }
        uint32_t        GetAzimuthTimeMaxBeforeCalibration      ()                  { return m_ConfigData.AzimuthTimeMaxB4_Calibration; }
        float           GetAzimuthGearReduction                 ()                  { return m_ConfigData.AzimuthGearReduction; }

        float           GetElevationDegMax                      ()                  { return m_ConfigData.ElevationDegMAX; }
        float           GetElevationDegMin                      ()                  { return m_ConfigData.ElevationDegMIN; }
        float           GetElevationStepsPerDegree              ();
        uint32_t        GetElevationStepSpeedHz                 ()                  { return m_ConfigData.ElevationStepSpeedHz; }
        uint32_t        GetElevationStepAcceleration            ()                  { return m_ConfigData.ElevationStepAcceleration; }
        uint32_t        GetElevationTimeThreshold               ()                  { return m_ConfigData.ElevationTimeThreshold; }
        float           GetElevationGearReduction               ()                  { return m_ConfigData.ElevationGearReduction; }

        uint16_t        GetAzimuthMotorStepsPerRevolution       ()                  { return m_AzimuthMotorStepsPerRevolution; }
        uint16_t        GetElevationMotorStepsPerRevolution     ()                  { return m_ElevationMotorStepsPerRevolution; }
        uint16_t        GetStepper3MotorStepsPerRevolution      ()                  { return m_Stepper3MotorStepsPerRevolution; }
        uint16_t        GetStepperMotorStepsPerRevolution       ()                  { return m_ConfigData.StepperMotorStepsPerRevolution; }
        uint8_t         GetStepperMicrostepMode                 ()                  { return m_ConfigData.StepperMicrostepMode; }

        const char*     GetNTP_Server1                          ()                  { return m_ConfigData.NTP_Server1; }
        const char*     GetNTP_Server2                          ()                  { return m_ConfigData.NTP_Server2; }
        const char*     GetNTP_Server3                          ()                  { return m_ConfigData.NTP_Server3; }

        const char*     GetWIFI_SSID                            ()                  { return m_ConfigData.WifiSSID; }
        const char*     GetWIFI_Password                        ()                  { return m_ConfigData.WIFI_Password; }
        const char*     GetWIFI_SSID2                           ()                  { return m_ConfigData.WifiSSID2; }
        const char*     GetWIFI_Password2                       ()                  { return m_ConfigData.WIFI_Password2; }
        bool            GetWIFI_UseDHCP                         ()                  { return m_ConfigData.WIFI_UseDHCP; }
        const char*     GetWIFI_StaticIP                        ()                  { return m_ConfigData.WIFI_StaticIP; }
        const char*     GetWIFI_Gateway                         ()                  { return m_ConfigData.WIFI_Gateway; }
        const char*     GetWIFI_SubnetMask                      ()                  { return m_ConfigData.WIFI_SubnetMask; }
        const char*     GetWIFI_DNS1                            ()                  { return m_ConfigData.WIFI_DNS1; }
        const OutputSchedule_t& GetOutputSchedule               (uint8_t index) const { return m_ConfigData.OutputSchedules[index]; }
        const OutputSchedule_t& GetOutputSchedule               (uint8_t index, uint8_t slotIndex) const;
        uint8_t         GetOutputScheduleActiveDaysMask         (uint8_t index, uint8_t slotIndex) const;
        bool            GetOutputAutomaticMode                  (uint8_t index) const;
        const char*     GetOutputName                           (uint8_t index) const;

    private:
  
        void            SetDefaultConfig                        ();
        float           ComputeDerivedStepsPerDegree            (float reductionRatio, uint16_t motorStepsPerRevolution) const;
        
        ConfigData_t    m_ConfigData;
        uint16_t        m_AzimuthMotorStepsPerRevolution = 200u;
        uint16_t        m_ElevationMotorStepsPerRevolution = 200u;
        uint16_t        m_Stepper3MotorStepsPerRevolution = 200u;
        bool            m_OutputAutomaticModes[OUTPUT_COUNT];
        char            m_OutputNames[OUTPUT_COUNT][OUTPUT_NAME_SIZE];
        OutputSchedule_t m_OutputScheduleSlot2[OUTPUT_COUNT];
        uint8_t         m_OutputScheduleActiveDaysMasks[OUTPUT_COUNT][OUTPUT_SCHEDULE_SLOT_COUNT];
        Preferences     m_Preferences;
};

    // Global configuration module instance defined in ConfigModule.ino
    extern ConfigModule Config;

#endif // CONFIG_MODULE_H
