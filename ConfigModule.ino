#include "ConfigModule.h"

static void CopyCStringSafe(char* destination, size_t destinationSize, const char* source)
{
    if(destination == nullptr || destinationSize == 0u)
    {
        return;
    }

    if(source == nullptr)
    {
        destination[0] = '\0';
        return;
    }

    strncpy(destination, source, destinationSize);
    destination[destinationSize - 1u] = '\0';
}

static uint8_t NormalizeMicrostepMode(uint8_t value)
{
    if(value == 8u || value == 16u || value == 32u || value == 64u)
    {
        return value;
    }

    return 8u;
}

static void NormalizeOutputSchedule(OutputSchedule_t& schedule)
{
    if(schedule.StartType > 2u) { schedule.StartType = 0u; }
    if(schedule.EndType > 2u) { schedule.EndType = 0u; }
    if(schedule.StartHour > 23u) { schedule.StartHour = 23u; }
    if(schedule.EndHour > 23u) { schedule.EndHour = 23u; }
    if(schedule.StartMinute > 59u) { schedule.StartMinute = 59u; }
    if(schedule.EndMinute > 59u) { schedule.EndMinute = 59u; }
    if(schedule.StartOffsetMinutes < -720) { schedule.StartOffsetMinutes = -720; }
    if(schedule.StartOffsetMinutes > 720) { schedule.StartOffsetMinutes = 720; }
    if(schedule.EndOffsetMinutes < -720) { schedule.EndOffsetMinutes = -720; }
    if(schedule.EndOffsetMinutes > 720) { schedule.EndOffsetMinutes = 720; }
    if(schedule.DutyPercent > 100u) { schedule.DutyPercent = 100u; }
}

// Constructor
ConfigModule::ConfigModule()
{
}

// Initialize the module
void ConfigModule::Begin()
{
    m_Preferences.begin("config", false);

    ResetOutputAutomaticModeConfig();
  
    if(LoadConfig())
    {
        uint16_t legacyFallbackSteps = m_ConfigData.StepperMotorStepsPerRevolution;
        if(legacyFallbackSteps == 0u)
        {
            legacyFallbackSteps = 200u;
            m_ConfigData.StepperMotorStepsPerRevolution = legacyFallbackSteps;
        }

        m_AzimuthMotorStepsPerRevolution = m_Preferences.getUShort("az_mtr_steps", legacyFallbackSteps);
        if(m_AzimuthMotorStepsPerRevolution == 0u)
        {
            m_AzimuthMotorStepsPerRevolution = 200u;
        }

        m_ElevationMotorStepsPerRevolution = m_Preferences.getUShort("el_mtr_steps", legacyFallbackSteps);
        if(m_ElevationMotorStepsPerRevolution == 0u)
        {
            m_ElevationMotorStepsPerRevolution = 200u;
        }

        m_Stepper3MotorStepsPerRevolution = m_Preferences.getUShort("s3_mtr_steps", legacyFallbackSteps);
        if(m_Stepper3MotorStepsPerRevolution == 0u)
        {
            m_Stepper3MotorStepsPerRevolution = 200u;
        }

        m_ConfigData.StepperMicrostepMode = NormalizeMicrostepMode(m_ConfigData.StepperMicrostepMode);

        for(uint8_t i = 0; i < OUTPUT_COUNT; i++)
        {
            char key[16];
            snprintf(key, sizeof(key), "out_auto_%u", i);
            m_OutputAutomaticModes[i] = m_Preferences.getBool(key, false);

            OutputSchedule_t secondary = {};
            char scheduleKey[16];
            snprintf(scheduleKey, sizeof(scheduleKey), "out_s2_%u", i);
            size_t scheduleBytes = m_Preferences.getBytesLength(scheduleKey);
            if(scheduleBytes == sizeof(OutputSchedule_t))
            {
                m_Preferences.getBytes(scheduleKey, &secondary, sizeof(OutputSchedule_t));
                NormalizeOutputSchedule(secondary);
                m_OutputScheduleSlot2[i] = secondary;
            }
            else
            {
                m_OutputScheduleSlot2[i].Enabled = false;
                m_OutputScheduleSlot2[i].StartType = 0u;
                m_OutputScheduleSlot2[i].StartHour = 8u;
                m_OutputScheduleSlot2[i].StartMinute = 0u;
                m_OutputScheduleSlot2[i].StartOffsetMinutes = 0;
                m_OutputScheduleSlot2[i].EndType = 0u;
                m_OutputScheduleSlot2[i].EndHour = 18u;
                m_OutputScheduleSlot2[i].EndMinute = 0u;
                m_OutputScheduleSlot2[i].EndOffsetMinutes = 0;
                m_OutputScheduleSlot2[i].DutyPercent = 100u;
            }

            for(uint8_t slotIndex = 0u; slotIndex < OUTPUT_SCHEDULE_SLOT_COUNT; slotIndex++)
            {
                char daysKey[20];
                snprintf(daysKey, sizeof(daysKey), "out_days_%u_%u", i, slotIndex);
                m_OutputScheduleActiveDaysMasks[i][slotIndex] = m_Preferences.getUChar(daysKey, OUTPUT_SCHEDULE_DAYS_ALL) & OUTPUT_SCHEDULE_DAYS_ALL;
            }

            NormalizeOutputSchedule(m_ConfigData.OutputSchedules[i]);
        }
    }
    else
    {
        // First time or failed to load, set defaults and save
        SetDefaultConfig();
        SaveConfig();
    }
}

// Load configuration from NVS
bool ConfigModule::LoadConfig()
{
    size_t expectedSize = sizeof(m_ConfigData);
    size_t actualSize = m_Preferences.getBytesLength("m_ConfigData");
  
    if(actualSize == expectedSize)
    {
        m_Preferences.getBytes("m_ConfigData", &m_ConfigData, expectedSize);
        return true;
    }
  
    return false;
}

// Save configuration to NVS
void ConfigModule::SaveConfig()
{
    m_Preferences.putBytes("m_ConfigData", &m_ConfigData, sizeof(m_ConfigData));
    m_Preferences.putUShort("az_mtr_steps", m_AzimuthMotorStepsPerRevolution);
    m_Preferences.putUShort("el_mtr_steps", m_ElevationMotorStepsPerRevolution);
    m_Preferences.putUShort("s3_mtr_steps", m_Stepper3MotorStepsPerRevolution);

    for(uint8_t i = 0; i < OUTPUT_COUNT; i++)
    {
        char key[16];
        snprintf(key, sizeof(key), "out_auto_%u", i);
        m_Preferences.putBool(key, m_OutputAutomaticModes[i]);

        NormalizeOutputSchedule(m_OutputScheduleSlot2[i]);
        char scheduleKey[16];
        snprintf(scheduleKey, sizeof(scheduleKey), "out_s2_%u", i);
        m_Preferences.putBytes(scheduleKey, &m_OutputScheduleSlot2[i], sizeof(OutputSchedule_t));

        for(uint8_t slotIndex = 0u; slotIndex < OUTPUT_SCHEDULE_SLOT_COUNT; slotIndex++)
        {
            char daysKey[20];
            snprintf(daysKey, sizeof(daysKey), "out_days_%u_%u", i, slotIndex);
            m_Preferences.putUChar(daysKey, m_OutputScheduleActiveDaysMasks[i][slotIndex] & OUTPUT_SCHEDULE_DAYS_ALL);
        }
    }
}

// Get reference to configuration data
ConfigData_t& ConfigModule::GetConfig()
{
    return m_ConfigData;
}

// Reset configuration to default values
void ConfigModule::ResetConfig()
{
    SetDefaultConfig();
    SaveConfig();
}

void ConfigModule::SetDefaultConfig()
{
    m_ConfigData.StepperMotorStepsPerRevolution = 200u;
    m_ConfigData.StepperMicrostepMode = 8u;
    m_ConfigData.ReservedStepperSettings[0] = 0u;
    m_AzimuthMotorStepsPerRevolution = 200u;
    m_ElevationMotorStepsPerRevolution = 200u;
    m_Stepper3MotorStepsPerRevolution = 200u;

    ResetConstantsConfig();
    ResetSolarTrackingConfig();
    ResetAzimuthConfig();
    ResetElevationConfig();
    ResetNTP_Config();
    ResetWiFiConfig();
    ResetOutputScheduleConfig();
    ResetOutputAutomaticModeConfig();
}

void ConfigModule::ResetConstantsConfig()
{
    m_ConfigData.ReservedConstant0 = 0u;
}

void ConfigModule::ResetSolarTrackingConfig()
{
    m_ConfigData.ST_Latitude = 45.8944094;
    m_ConfigData.ST_Longitude = -74.0015057;
    m_ConfigData.ST_Altitude = 0.0;
    m_ConfigData.ST_TimeZoneOffset = -5.0;
    m_ConfigData.ST_UseDST = false;
    m_ConfigData.ST_ReservedFlag0 = 0u;
    m_ConfigData.ST_ReservedValue0 = 0u;
    m_ConfigData.ST_Pressure = 101.0;
    m_ConfigData.ST_Temperature = 283.0;
}

void ConfigModule::ResetAzimuthConfig()
{
    m_ConfigData.AzimuthDegMAX = 275.0;
    m_ConfigData.AzimuthDegMIN = 90.0;
    m_ConfigData.AzimuthGearReduction = 2.25f;
    m_ConfigData.AzimuthStepsPerDegree = ComputeDerivedStepsPerDegree(m_ConfigData.AzimuthGearReduction, m_AzimuthMotorStepsPerRevolution);
    m_ConfigData.AzimuthStepSpeedHz = 1200;
    m_ConfigData.AzimuthStepAcceleration = 3000;
    m_ConfigData.AzimuthTimeThreshold = 0;
    m_ConfigData.AzimuthTimeMaxB4_Calibration = 180000;
}

void ConfigModule::ResetElevationConfig()
{
    m_ConfigData.ElevationDegMAX = 90.0;
    m_ConfigData.ElevationDegMIN = 0.0;
    m_ConfigData.ElevationGearReduction = 2.25f;
    m_ConfigData.ElevationStepsPerDegree = ComputeDerivedStepsPerDegree(m_ConfigData.ElevationGearReduction, m_ElevationMotorStepsPerRevolution);
    m_ConfigData.ElevationStepSpeedHz = 1000;
    m_ConfigData.ElevationStepAcceleration = 2500;
    m_ConfigData.ElevationTimeThreshold = 0;
}

void ConfigModule::ResetNTP_Config()
{
    // NTP Settings
    strncpy(m_ConfigData.NTP_Server1, "ca.pool.ntp.org", sizeof(m_ConfigData.NTP_Server1) - 1);
    m_ConfigData.NTP_Server1[sizeof(m_ConfigData.NTP_Server1) - 1] = '\0';
    strncpy(m_ConfigData.NTP_Server2, "0.ca.pool.ntp.org", sizeof(m_ConfigData.NTP_Server2) - 1);
    m_ConfigData.NTP_Server2[sizeof(m_ConfigData.NTP_Server2) - 1] = '\0';
    strncpy(m_ConfigData.NTP_Server3, "time.google.com", sizeof(m_ConfigData.NTP_Server3) - 1);
    m_ConfigData.NTP_Server3[sizeof(m_ConfigData.NTP_Server3) - 1] = '\0';
}

void ConfigModule::ResetWiFiConfig()
{
    // Wi-Fi Credentials
    strncpy(m_ConfigData.WifiSSID, "", sizeof(m_ConfigData.WifiSSID));
    m_ConfigData.WifiSSID[sizeof(m_ConfigData.WifiSSID) - 1] = '\0';
    strncpy(m_ConfigData.WIFI_Password, "", sizeof(m_ConfigData.WIFI_Password));
    m_ConfigData.WIFI_Password[sizeof(m_ConfigData.WIFI_Password) - 1] = '\0';
    strncpy(m_ConfigData.WifiSSID2, "", sizeof(m_ConfigData.WifiSSID2));
    m_ConfigData.WifiSSID2[sizeof(m_ConfigData.WifiSSID2) - 1] = '\0';
    strncpy(m_ConfigData.WIFI_Password2, "", sizeof(m_ConfigData.WIFI_Password2));
    m_ConfigData.WIFI_Password2[sizeof(m_ConfigData.WIFI_Password2) - 1] = '\0';
    m_ConfigData.WIFI_UseDHCP = true;
    strncpy(m_ConfigData.WIFI_StaticIP, "", sizeof(m_ConfigData.WIFI_StaticIP));
    m_ConfigData.WIFI_StaticIP[sizeof(m_ConfigData.WIFI_StaticIP) - 1] = '\0';
    strncpy(m_ConfigData.WIFI_Gateway, "", sizeof(m_ConfigData.WIFI_Gateway));
    m_ConfigData.WIFI_Gateway[sizeof(m_ConfigData.WIFI_Gateway) - 1] = '\0';
    strncpy(m_ConfigData.WIFI_SubnetMask, "", sizeof(m_ConfigData.WIFI_SubnetMask));
    m_ConfigData.WIFI_SubnetMask[sizeof(m_ConfigData.WIFI_SubnetMask) - 1] = '\0';
    strncpy(m_ConfigData.WIFI_DNS1, "", sizeof(m_ConfigData.WIFI_DNS1));
    m_ConfigData.WIFI_DNS1[sizeof(m_ConfigData.WIFI_DNS1) - 1] = '\0';
}

void ConfigModule::ResetOutputScheduleConfig()
{
    for(uint8_t i = 0; i < OUTPUT_COUNT; i++)
    {
        m_ConfigData.OutputSchedules[i].Enabled = false;
        m_ConfigData.OutputSchedules[i].StartType = 0;
        m_ConfigData.OutputSchedules[i].StartHour = 8;
        m_ConfigData.OutputSchedules[i].StartMinute = 0;
        m_ConfigData.OutputSchedules[i].StartOffsetMinutes = 0;
        m_ConfigData.OutputSchedules[i].EndType = 0;
        m_ConfigData.OutputSchedules[i].EndHour = 18;
        m_ConfigData.OutputSchedules[i].EndMinute = 0;
        m_ConfigData.OutputSchedules[i].EndOffsetMinutes = 0;
        m_ConfigData.OutputSchedules[i].DutyPercent = 100;

        m_OutputScheduleSlot2[i].Enabled = false;
        m_OutputScheduleSlot2[i].StartType = 0;
        m_OutputScheduleSlot2[i].StartHour = 8;
        m_OutputScheduleSlot2[i].StartMinute = 0;
        m_OutputScheduleSlot2[i].StartOffsetMinutes = 0;
        m_OutputScheduleSlot2[i].EndType = 0;
        m_OutputScheduleSlot2[i].EndHour = 18;
        m_OutputScheduleSlot2[i].EndMinute = 0;
        m_OutputScheduleSlot2[i].EndOffsetMinutes = 0;
        m_OutputScheduleSlot2[i].DutyPercent = 100;

        for(uint8_t slotIndex = 0u; slotIndex < OUTPUT_SCHEDULE_SLOT_COUNT; slotIndex++)
        {
            m_OutputScheduleActiveDaysMasks[i][slotIndex] = OUTPUT_SCHEDULE_DAYS_ALL;
        }
    }
}

void ConfigModule::ResetOutputAutomaticModeConfig()
{
    for(uint8_t i = 0; i < OUTPUT_COUNT; i++)
    {
        m_OutputAutomaticModes[i] = false;
    }
}

// ------------------------ SETTERS ----------------------------------

void ConfigModule::SetST_Latitude(double value)
{
    m_ConfigData.ST_Latitude = value;
}

void ConfigModule::SetST_Longitude(double value)
{
    m_ConfigData.ST_Longitude = value;
}

void ConfigModule::SetST_Altitude(double value)
{
    m_ConfigData.ST_Altitude = value;
}

void ConfigModule::SetST_TimeZoneOffset(double value)
{
    m_ConfigData.ST_TimeZoneOffset = value;
}

void ConfigModule::SetST_UseDST(bool value)
{
    m_ConfigData.ST_UseDST = value;
}

void ConfigModule::SetST_Pressure(double value)
{
    m_ConfigData.ST_Pressure = value;
}

void ConfigModule::SetST_Temperature(double value)
{
    m_ConfigData.ST_Temperature = value;
}

void ConfigModule::SetAzimuthDegMax(float value)
{
    m_ConfigData.AzimuthDegMAX = value;
}

void ConfigModule::SetAzimuthDegMin(float value)
{
    m_ConfigData.AzimuthDegMIN = value;
}

void ConfigModule::SetAzimuthStepsPerDegree(float value)
{
    if(value <= 0.0f)
    {
        return;
    }

    float denominator = (float)m_AzimuthMotorStepsPerRevolution * (float)m_ConfigData.StepperMicrostepMode;
    if(denominator <= 0.0f)
    {
        m_AzimuthMotorStepsPerRevolution = 200u;
        m_ConfigData.StepperMicrostepMode = 8u;
        denominator = 1600.0f;
    }

    m_ConfigData.AzimuthGearReduction = (value * 360.0f) / denominator;
    m_ConfigData.AzimuthStepsPerDegree = ComputeDerivedStepsPerDegree(m_ConfigData.AzimuthGearReduction, m_AzimuthMotorStepsPerRevolution);
}

void ConfigModule::SetAzimuthStepSpeedHz(uint32_t value)
{
    if(value == 0u)
    {
        return;
    }

    m_ConfigData.AzimuthStepSpeedHz = value;
}

void ConfigModule::SetAzimuthStepAcceleration(uint32_t value)
{
    if(value == 0u)
    {
        return;
    }

    m_ConfigData.AzimuthStepAcceleration = value;
}

void ConfigModule::SetAzimuthTimeThreshold(uint32_t value)
{
    m_ConfigData.AzimuthTimeThreshold = value;
}

void ConfigModule::SetAzimuthTimeMaxBeforeCalibration(uint32_t value)
{
    m_ConfigData.AzimuthTimeMaxB4_Calibration = value;
}

void ConfigModule::SetAzimuthGearReduction(float value)
{
    if(value <= 0.0f)
    {
        return;
    }

    m_ConfigData.AzimuthGearReduction = value;
    m_ConfigData.AzimuthStepsPerDegree = ComputeDerivedStepsPerDegree(m_ConfigData.AzimuthGearReduction, m_AzimuthMotorStepsPerRevolution);
}

void ConfigModule::SetElevationDegMax(float value)
{
    m_ConfigData.ElevationDegMAX = value;
}

void ConfigModule::SetElevationDegMin(float value)
{
    m_ConfigData.ElevationDegMIN = value;
}

void ConfigModule::SetElevationStepsPerDegree(float value)
{
    if(value <= 0.0f)
    {
        return;
    }

    float denominator = (float)m_ElevationMotorStepsPerRevolution * (float)m_ConfigData.StepperMicrostepMode;
    if(denominator <= 0.0f)
    {
        m_ElevationMotorStepsPerRevolution = 200u;
        m_ConfigData.StepperMicrostepMode = 8u;
        denominator = 1600.0f;
    }

    m_ConfigData.ElevationGearReduction = (value * 360.0f) / denominator;
    m_ConfigData.ElevationStepsPerDegree = ComputeDerivedStepsPerDegree(m_ConfigData.ElevationGearReduction, m_ElevationMotorStepsPerRevolution);
}

void ConfigModule::SetElevationStepSpeedHz(uint32_t value)
{
    if(value == 0u)
    {
        return;
    }

    m_ConfigData.ElevationStepSpeedHz = value;
}

void ConfigModule::SetElevationStepAcceleration(uint32_t value)
{
    if(value == 0u)
    {
        return;
    }

    m_ConfigData.ElevationStepAcceleration = value;
}

void ConfigModule::SetElevationTimeThreshold(uint32_t value)
{
    m_ConfigData.ElevationTimeThreshold = value;
}

void ConfigModule::SetElevationGearReduction(float value)
{
    if(value <= 0.0f)
    {
        return;
    }

    m_ConfigData.ElevationGearReduction = value;
    m_ConfigData.ElevationStepsPerDegree = ComputeDerivedStepsPerDegree(m_ConfigData.ElevationGearReduction, m_ElevationMotorStepsPerRevolution);
}

void ConfigModule::SetAzimuthMotorStepsPerRevolution(uint16_t value)
{
    if(value == 0u)
    {
        return;
    }

    m_AzimuthMotorStepsPerRevolution = value;
    m_ConfigData.StepperMotorStepsPerRevolution = value;
    m_ConfigData.AzimuthStepsPerDegree = ComputeDerivedStepsPerDegree(m_ConfigData.AzimuthGearReduction, m_AzimuthMotorStepsPerRevolution);
}

void ConfigModule::SetElevationMotorStepsPerRevolution(uint16_t value)
{
    if(value == 0u)
    {
        return;
    }

    m_ElevationMotorStepsPerRevolution = value;
    m_ConfigData.StepperMotorStepsPerRevolution = value;
    m_ConfigData.ElevationStepsPerDegree = ComputeDerivedStepsPerDegree(m_ConfigData.ElevationGearReduction, m_ElevationMotorStepsPerRevolution);
}

void ConfigModule::SetStepperMotorStepsPerRevolution(uint16_t value)
{
    if(value == 0u)
    {
        return;
    }

    m_ConfigData.StepperMotorStepsPerRevolution = value;
    m_AzimuthMotorStepsPerRevolution = value;
    m_ElevationMotorStepsPerRevolution = value;
    m_Stepper3MotorStepsPerRevolution = value;
    m_ConfigData.AzimuthStepsPerDegree = ComputeDerivedStepsPerDegree(m_ConfigData.AzimuthGearReduction, m_AzimuthMotorStepsPerRevolution);
    m_ConfigData.ElevationStepsPerDegree = ComputeDerivedStepsPerDegree(m_ConfigData.ElevationGearReduction, m_ElevationMotorStepsPerRevolution);
}

void ConfigModule::SetStepper3MotorStepsPerRevolution(uint16_t value)
{
    if(value == 0u)
    {
        return;
    }

    m_Stepper3MotorStepsPerRevolution = value;
    m_ConfigData.StepperMotorStepsPerRevolution = value;
}

void ConfigModule::SetStepperMicrostepMode(uint8_t value)
{
    m_ConfigData.StepperMicrostepMode = NormalizeMicrostepMode(value);
    m_ConfigData.AzimuthStepsPerDegree = ComputeDerivedStepsPerDegree(m_ConfigData.AzimuthGearReduction, m_AzimuthMotorStepsPerRevolution);
    m_ConfigData.ElevationStepsPerDegree = ComputeDerivedStepsPerDegree(m_ConfigData.ElevationGearReduction, m_ElevationMotorStepsPerRevolution);
}

void ConfigModule::SetNTP_Server1(const char *value)
{
    CopyCStringSafe(m_ConfigData.NTP_Server1, sizeof(m_ConfigData.NTP_Server1), value);
}

void ConfigModule::SetNTP_Server2(const char *value)
{
    CopyCStringSafe(m_ConfigData.NTP_Server2, sizeof(m_ConfigData.NTP_Server2), value);
}

void ConfigModule::SetNTP_Server3(const char *value)
{
    CopyCStringSafe(m_ConfigData.NTP_Server3, sizeof(m_ConfigData.NTP_Server3), value);
}

void ConfigModule::SetWIFI_SSID(const char *value)
{
        CopyCStringSafe(m_ConfigData.WifiSSID, sizeof(m_ConfigData.WifiSSID), value);
}

void ConfigModule::SetWIFI_Password(const char *value)
{
    CopyCStringSafe(m_ConfigData.WIFI_Password, sizeof(m_ConfigData.WIFI_Password), value);
}

void ConfigModule::SetWIFI_SSID2(const char *value)
{
    CopyCStringSafe(m_ConfigData.WifiSSID2, sizeof(m_ConfigData.WifiSSID2), value);
}

void ConfigModule::SetWIFI_Password2(const char *value)
{
    CopyCStringSafe(m_ConfigData.WIFI_Password2, sizeof(m_ConfigData.WIFI_Password2), value);
}

void ConfigModule::SetWIFI_UseDHCP(bool value)
{
    m_ConfigData.WIFI_UseDHCP = value;
}

void ConfigModule::SetWIFI_StaticIP(const char *value)
{
    CopyCStringSafe(m_ConfigData.WIFI_StaticIP, sizeof(m_ConfigData.WIFI_StaticIP), value);
}

void ConfigModule::SetWIFI_Gateway(const char *value)
{
    CopyCStringSafe(m_ConfigData.WIFI_Gateway, sizeof(m_ConfigData.WIFI_Gateway), value);
}

void ConfigModule::SetWIFI_SubnetMask(const char *value)
{
    CopyCStringSafe(m_ConfigData.WIFI_SubnetMask, sizeof(m_ConfigData.WIFI_SubnetMask), value);
}

void ConfigModule::SetWIFI_DNS1(const char *value)
{
    CopyCStringSafe(m_ConfigData.WIFI_DNS1, sizeof(m_ConfigData.WIFI_DNS1), value);
}

void ConfigModule::SetOutputSchedule(uint8_t index, const OutputSchedule_t& schedule)
{
    SetOutputSchedule(index, 0u, schedule);
}

void ConfigModule::SetOutputSchedule(uint8_t index, uint8_t slotIndex, const OutputSchedule_t& schedule)
{
    if(index >= OUTPUT_COUNT || slotIndex >= OUTPUT_SCHEDULE_SLOT_COUNT)
    {
        return;
    }

    OutputSchedule_t normalized = schedule;
    NormalizeOutputSchedule(normalized);

    if(slotIndex == 0u)
    {
        m_ConfigData.OutputSchedules[index] = normalized;
        return;
    }

    m_OutputScheduleSlot2[index] = normalized;
}

void ConfigModule::SetOutputScheduleActiveDaysMask(uint8_t index, uint8_t slotIndex, uint8_t activeDaysMask)
{
    if(index >= OUTPUT_COUNT || slotIndex >= OUTPUT_SCHEDULE_SLOT_COUNT)
    {
        return;
    }

    m_OutputScheduleActiveDaysMasks[index][slotIndex] = activeDaysMask & OUTPUT_SCHEDULE_DAYS_ALL;
}

void ConfigModule::SetOutputAutomaticMode(uint8_t index, bool automaticMode)
{
    if(index >= 3)
    {
        return;
    }

    m_OutputAutomaticModes[index] = automaticMode;
}

bool ConfigModule::GetOutputAutomaticMode(uint8_t index) const
{
    if(index >= OUTPUT_COUNT)
    {
        return false;
    }

    return m_OutputAutomaticModes[index];
}

const OutputSchedule_t& ConfigModule::GetOutputSchedule(uint8_t index, uint8_t slotIndex) const
{
    static const OutputSchedule_t kDefaultSchedule = {false, 0u, 8u, 0u, 0, 0u, 18u, 0u, 0, 100u};

    if(index >= OUTPUT_COUNT || slotIndex >= OUTPUT_SCHEDULE_SLOT_COUNT)
    {
        return kDefaultSchedule;
    }

    if(slotIndex == 0u)
    {
        return m_ConfigData.OutputSchedules[index];
    }

    return m_OutputScheduleSlot2[index];
}

uint8_t ConfigModule::GetOutputScheduleActiveDaysMask(uint8_t index, uint8_t slotIndex) const
{
    if(index >= OUTPUT_COUNT || slotIndex >= OUTPUT_SCHEDULE_SLOT_COUNT)
    {
        return OUTPUT_SCHEDULE_DAYS_ALL;
    }

    return m_OutputScheduleActiveDaysMasks[index][slotIndex] & OUTPUT_SCHEDULE_DAYS_ALL;
}

float ConfigModule::ComputeDerivedStepsPerDegree(float reductionRatio, uint16_t motorStepsPerRevolution) const
{
    if(reductionRatio <= 0.0f)
    {
        return 0.0f;
    }

    uint8_t microstepMode = NormalizeMicrostepMode(m_ConfigData.StepperMicrostepMode);
    uint16_t motorSteps = motorStepsPerRevolution;
    if(motorSteps == 0u)
    {
        motorSteps = 200u;
    }

    float numerator = (float)motorSteps * (float)microstepMode * reductionRatio;
    return numerator / 360.0f;
}

float ConfigModule::GetAzimuthStepsPerDegree()
{
    float computed = ComputeDerivedStepsPerDegree(m_ConfigData.AzimuthGearReduction, m_AzimuthMotorStepsPerRevolution);
    if(computed > 0.0f)
    {
        return computed;
    }

    return m_ConfigData.AzimuthStepsPerDegree;
}

float ConfigModule::GetElevationStepsPerDegree()
{
    float computed = ComputeDerivedStepsPerDegree(m_ConfigData.ElevationGearReduction, m_ElevationMotorStepsPerRevolution);
    if(computed > 0.0f)
    {
        return computed;
    }

    return m_ConfigData.ElevationStepsPerDegree;
}

void ConfigModule::PrintConfig()
{
    Serial.println("Configuration:");
    Serial.printf("ST_Latitude: %f\n", m_ConfigData.ST_Latitude);
    Serial.printf("ST_Longitude: %f\n", m_ConfigData.ST_Longitude);
    Serial.printf("ST_Altitude: %f\n", m_ConfigData.ST_Altitude);
    Serial.printf("ST_TimeZoneOffset: %f\n", m_ConfigData.ST_TimeZoneOffset);
    Serial.printf("ST_UseDST: %d\n", m_ConfigData.ST_UseDST);
    Serial.printf("ST_Pressure: %f\n", m_ConfigData.ST_Pressure);
    Serial.printf("ST_Temperature: %f\n", m_ConfigData.ST_Temperature);
    Serial.printf("AzimuthDegMAX: %f\n", m_ConfigData.AzimuthDegMAX);
    Serial.printf("AzimuthDegMIN: %f\n", m_ConfigData.AzimuthDegMIN);
    Serial.printf("AzimuthStepsPerDegree: %f\n", m_ConfigData.AzimuthStepsPerDegree);
    Serial.printf("AzimuthStepSpeedHz: %u\n", m_ConfigData.AzimuthStepSpeedHz);
    Serial.printf("AzimuthStepAcceleration: %u\n", m_ConfigData.AzimuthStepAcceleration);
    Serial.printf("AzimuthTimeThreshold: %d\n", m_ConfigData.AzimuthTimeThreshold);
    Serial.printf("AzimuthTimeMaxB4_Calibration: %d\n", m_ConfigData.AzimuthTimeMaxB4_Calibration);
    Serial.printf("AzimuthGearReduction: %f\n", m_ConfigData.AzimuthGearReduction);
    Serial.printf("ElevationDegMAX: %f\n", m_ConfigData.ElevationDegMAX);
    Serial.printf("ElevationDegMIN: %f\n", m_ConfigData.ElevationDegMIN);
    Serial.printf("ElevationStepsPerDegree: %f\n", m_ConfigData.ElevationStepsPerDegree);
    Serial.printf("ElevationStepSpeedHz: %u\n", m_ConfigData.ElevationStepSpeedHz);
    Serial.printf("ElevationStepAcceleration: %u\n", m_ConfigData.ElevationStepAcceleration);
    Serial.printf("ElevationTimeThreshold: %d\n", m_ConfigData.ElevationTimeThreshold);
    Serial.printf("ElevationGearReduction: %f\n", m_ConfigData.ElevationGearReduction);
    Serial.printf("AzimuthMotorStepsPerRevolution: %u\n", m_AzimuthMotorStepsPerRevolution);
    Serial.printf("ElevationMotorStepsPerRevolution: %u\n", m_ElevationMotorStepsPerRevolution);
    Serial.printf("Stepper3MotorStepsPerRevolution: %u\n", m_Stepper3MotorStepsPerRevolution);
    Serial.printf("StepperMotorStepsPerRevolution: %u\n", m_ConfigData.StepperMotorStepsPerRevolution);
    Serial.printf("StepperMicrostepMode: %u\n", m_ConfigData.StepperMicrostepMode);
    Serial.printf("NTP_Server1: %s\n", m_ConfigData.NTP_Server1);
    Serial.printf("NTP_Server2: %s\n", m_ConfigData.NTP_Server2);
    Serial.printf("NTP_Server3: %s\n", m_ConfigData.NTP_Server3);
    Serial.printf("WifiSSID: %s\n", m_ConfigData.WifiSSID);
    Serial.printf("WIFI_Password: %s\n", m_ConfigData.WIFI_Password);
    Serial.printf("WifiSSID2: %s\n", m_ConfigData.WifiSSID2);
    Serial.printf("WIFI_Password2: %s\n", m_ConfigData.WIFI_Password2);

    for(uint8_t i = 0; i < OUTPUT_COUNT; i++)
    {
        for(uint8_t slot = 0; slot < OUTPUT_SCHEDULE_SLOT_COUNT; slot++)
        {
            const OutputSchedule_t& schedule = GetOutputSchedule(i, slot);
            Serial.printf("OutputSchedule[%u][%u]: auto=%d enabled=%d startType=%u start=%02u:%02u startOffset=%d endType=%u end=%02u:%02u endOffset=%d duty=%u\n",
                           i,
                           slot,
                           m_OutputAutomaticModes[i],
                           schedule.Enabled,
                           schedule.StartType,
                           schedule.StartHour,
                           schedule.StartMinute,
                           schedule.StartOffsetMinutes,
                           schedule.EndType,
                           schedule.EndHour,
                           schedule.EndMinute,
                           schedule.EndOffsetMinutes,
                           schedule.DutyPercent);
        }
    }
}

ConfigModule Config;