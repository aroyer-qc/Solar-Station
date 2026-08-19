#include "TrackingModule.h"

#include "SchedulerModule.h"
#include "SolarFixed.h"
#include "ConfigModule.h"
#include "AzimuthController.h"
#include "ElevationController.h"
#include "FastAccelStepper.h"
#include <cmath>
#include <cstring>

extern FastAccelStepper* Stepper1;
extern FastAccelStepper* Stepper2;

namespace
{
    static constexpr uint16_t RETURN_DELAY_AFTER_SUNSET_MIN = 30u;

    static uint16_t QuantizeDegrees10(float value)
    {
        if(value <= 0.0f) { return 0u; }
        if(value >= 6553.5f) { return 65535u; }
        return (uint16_t)((value * 10.0f) + 0.5f);
    }

    static void StopTrackingAxisMotion()
    {
        if(Stepper1 != nullptr)
        {
            Stepper1->stopMove();
        }

        if(Stepper2 != nullptr)
        {
            Stepper2->stopMove();
        }
    }

    static bool IsTargetWithinDailyTrackingRange(float targetAzimuthDeg, float targetElevationDeg)
    {
        float minAzimuthDeg = 0.0f;
        float maxAzimuthDeg = 0.0f;
        if(!GetTodayAzimuthBounds(minAzimuthDeg, maxAzimuthDeg))
        {
            return false;
        }

        if((targetAzimuthDeg < minAzimuthDeg) || (targetAzimuthDeg > maxAzimuthDeg))
        {
            return false;
        }

        float todayElevationMaxDeg = 0.0f;
        if(!GetTodayElevationMax(todayElevationMaxDeg))
        {
            return false;
        }

        if(todayElevationMaxDeg < 0.0f)
        {
            return false;
        }

        return (targetElevationDeg >= 0.0f) && (targetElevationDeg <= todayElevationMaxDeg);
    }

    static bool GetSunsetReturnTriggerEpoch(time_t& triggerEpochLocal)
    {
        if(!IsSystemTimeValid())
        {
            return false;
        }

        uint16_t sunriseMinutesLocal = 0;
        uint16_t sunsetMinutesLocal = 0;
        if(!GetSunriseSunsetUtcMinutes(sunriseMinutesLocal, sunsetMinutesLocal))
        {
            return false;
        }

        time_t localNow = GetLocalTimeFromConfigOffset();
        struct tm localTm;
        if(gmtime_r(&localNow, &localTm) == nullptr)
        {
            return false;
        }

        time_t startOfTodayLocal = localNow
            - ((time_t)localTm.tm_hour * 3600)
            - ((time_t)localTm.tm_min * 60)
            - (time_t)localTm.tm_sec;

        time_t sunsetLocalEpoch = startOfTodayLocal + ((time_t)sunsetMinutesLocal * 60);
        triggerEpochLocal = sunsetLocalEpoch + ((time_t)RETURN_DELAY_AFTER_SUNSET_MIN * 60);
        return true;
    }
}

extern ConfigModule Config;
extern AzimuthController azimuthController;
extern ElevationController elevationController;

extern bool AreTrackingSteppersReady();

extern int32_t SunsetReturnLastAttemptDateKey;
extern bool SunsetReturnLastAttemptSucceeded;
extern bool TrackingTestOverrideActive;
extern float TrackingTestOverrideAzimuth;
extern float TrackingTestOverrideElevation;
extern uint8_t TrackingOverrideLastFailureCode;

bool GetNextSunriseLocalEpoch(time_t& nextSunriseLocalEpoch, int32_t& targetDateKey)
{
    if(!IsSystemTimeValid())
    {
        return false;
    }

    time_t localNow = GetLocalTimeFromConfigOffset();
    struct tm localTm;
    gmtime_r(&localNow, &localTm);

    uint16_t todaySunriseMinutes = 0;
    if(!GetSunriseMinutesForLocalDate(localTm.tm_year + 1900, localTm.tm_mon + 1, localTm.tm_mday, todaySunriseMinutes))
    {
        return false;
    }

    time_t startOfTodayLocal = localNow - ((time_t)localTm.tm_hour * 3600) - ((time_t)localTm.tm_min * 60) - (time_t)localTm.tm_sec;
    time_t todaySunriseLocalEpoch = startOfTodayLocal + ((time_t)todaySunriseMinutes * 60);

    if(localNow < todaySunriseLocalEpoch)
    {
        nextSunriseLocalEpoch = todaySunriseLocalEpoch;
        targetDateKey = BuildDateKey(localTm.tm_year + 1900, localTm.tm_mon + 1, localTm.tm_mday);
        return true;
    }

    time_t localTomorrow = localNow + 86400;
    struct tm tomorrowTm;
    gmtime_r(&localTomorrow, &tomorrowTm);

    uint16_t tomorrowSunriseMinutes = 0;
    if(!GetSunriseMinutesForLocalDate(tomorrowTm.tm_year + 1900, tomorrowTm.tm_mon + 1, tomorrowTm.tm_mday, tomorrowSunriseMinutes))
    {
        return false;
    }

    time_t startOfTomorrowLocal = localTomorrow - ((time_t)tomorrowTm.tm_hour * 3600) - ((time_t)tomorrowTm.tm_min * 60) - (time_t)tomorrowTm.tm_sec;
    nextSunriseLocalEpoch = startOfTomorrowLocal + ((time_t)tomorrowSunriseMinutes * 60);
    targetDateKey = BuildDateKey(tomorrowTm.tm_year + 1900, tomorrowTm.tm_mon + 1, tomorrowTm.tm_mday);
    return true;
}

bool ComputeSunPositionAtUtcEpoch(time_t utcEpoch, float& azimuthDeg, float& elevationDeg)
{
    struct tm utcTm;
    if(gmtime_r(&utcEpoch, &utcTm) == nullptr)
    {
        return false;
    }

    SolarPosition position;
    solar_compute(utcTm.tm_year + 1900,
                  utcTm.tm_mon + 1,
                  utcTm.tm_mday,
                  utcTm.tm_hour,
                  utcTm.tm_min,
                  utcTm.tm_sec,
                  Q16(Config.GetST_Latitude()),
                  Q16(Config.GetST_Longitude()),
                  &position);

    azimuthDeg = (float)position.azimuth / 65536.0f;
    elevationDeg = (float)position.elevation / 65536.0f;

    if(elevationDeg < 0.0f)
    {
        elevationDeg = 0.0f;
    }

    while(azimuthDeg < 0.0f) { azimuthDeg += 360.0f; }
    while(azimuthDeg >= 360.0f) { azimuthDeg -= 360.0f; }

    return true;
}

bool GetTodayAzimuthBounds(float& minAzimuthDeg, float& maxAzimuthDeg)
{
    if(!IsSystemTimeValid())
    {
        return false;
    }

    uint16_t sunriseMinutesLocal = 0;
    uint16_t sunsetMinutesLocal = 0;
    if(!GetSunriseSunsetUtcMinutes(sunriseMinutesLocal, sunsetMinutesLocal))
    {
        return false;
    }

    time_t localNow = GetLocalTimeFromConfigOffset();
    struct tm localTm;
    if(gmtime_r(&localNow, &localTm) == nullptr)
    {
        return false;
    }

    time_t startOfTodayLocal = localNow - ((time_t)localTm.tm_hour * 3600) - ((time_t)localTm.tm_min * 60) - (time_t)localTm.tm_sec;
    time_t sunriseLocalEpoch = startOfTodayLocal + ((time_t)sunriseMinutesLocal * 60);
    time_t sunsetLocalEpoch = startOfTodayLocal + ((time_t)sunsetMinutesLocal * 60);

    long offsetSeconds = (long)(GetConfiguredUtcOffsetHours() * 3600.0);
    time_t sunriseUtcEpoch = sunriseLocalEpoch - offsetSeconds;
    time_t sunsetUtcEpoch = sunsetLocalEpoch - offsetSeconds;

    float sunriseAzimuth = 0.0f;
    float sunriseElevation = 0.0f;
    if(!ComputeSunPositionAtUtcEpoch(sunriseUtcEpoch, sunriseAzimuth, sunriseElevation))
    {
        return false;
    }

    float sunsetAzimuth = 0.0f;
    float sunsetElevation = 0.0f;
    if(!ComputeSunPositionAtUtcEpoch(sunsetUtcEpoch, sunsetAzimuth, sunsetElevation))
    {
        return false;
    }

    minAzimuthDeg = (sunriseAzimuth < sunsetAzimuth) ? sunriseAzimuth : sunsetAzimuth;
    maxAzimuthDeg = (sunriseAzimuth > sunsetAzimuth) ? sunriseAzimuth : sunsetAzimuth;
    return true;
}

bool GetTodayElevationMax(float& maxElevationDeg)
{
    if(!IsSystemTimeValid())
    {
        return false;
    }

    uint16_t sunriseMinutesLocal = 0;
    uint16_t sunsetMinutesLocal = 0;
    if(!GetSunriseSunsetUtcMinutes(sunriseMinutesLocal, sunsetMinutesLocal))
    {
        return false;
    }

    time_t localNow = GetLocalTimeFromConfigOffset();
    struct tm localTm;
    if(gmtime_r(&localNow, &localTm) == nullptr)
    {
        return false;
    }

    time_t startOfTodayLocal = localNow - ((time_t)localTm.tm_hour * 3600) - ((time_t)localTm.tm_min * 60) - (time_t)localTm.tm_sec;
    time_t sunriseLocalEpoch = startOfTodayLocal + ((time_t)sunriseMinutesLocal * 60);
    time_t sunsetLocalEpoch = startOfTodayLocal + ((time_t)sunsetMinutesLocal * 60);

    if(sunsetLocalEpoch <= sunriseLocalEpoch)
    {
        return false;
    }

    long offsetSeconds = (long)(GetConfiguredUtcOffsetHours() * 3600.0);
    time_t sunriseUtcEpoch = sunriseLocalEpoch - offsetSeconds;
    time_t sunsetUtcEpoch = sunsetLocalEpoch - offsetSeconds;

    float bestElevation = 0.0f;
    time_t bestEpoch = sunriseUtcEpoch;
    bool hasSample = false;

    for(time_t sampleEpoch = sunriseUtcEpoch; sampleEpoch <= sunsetUtcEpoch; sampleEpoch += 600)
    {
        float azimuthDeg = 0.0f;
        float elevationDeg = 0.0f;
        if(!ComputeSunPositionAtUtcEpoch(sampleEpoch, azimuthDeg, elevationDeg))
        {
            continue;
        }

        if(!hasSample || elevationDeg > bestElevation)
        {
            hasSample = true;
            bestElevation = elevationDeg;
            bestEpoch = sampleEpoch;
        }
    }

    if(!hasSample)
    {
        return false;
    }

    time_t refineStart = bestEpoch - 600;
    time_t refineEnd = bestEpoch + 600;
    if(refineStart < sunriseUtcEpoch) { refineStart = sunriseUtcEpoch; }
    if(refineEnd > sunsetUtcEpoch) { refineEnd = sunsetUtcEpoch; }

    for(time_t sampleEpoch = refineStart; sampleEpoch <= refineEnd; sampleEpoch += 60)
    {
        float azimuthDeg = 0.0f;
        float elevationDeg = 0.0f;
        if(!ComputeSunPositionAtUtcEpoch(sampleEpoch, azimuthDeg, elevationDeg))
        {
            continue;
        }

        if(elevationDeg > bestElevation)
        {
            bestElevation = elevationDeg;
        }
    }

    if(bestElevation < 0.0f) { bestElevation = 0.0f; }
    if(bestElevation > 90.0f) { bestElevation = 90.0f; }

    maxElevationDeg = bestElevation;
    return true;
}

bool InitializeTrackingFromSolarPosition()
{
    // Initialize the tracking controllers with the current calculated solar position.
    // This prevents unwanted motor movement at boot-up when the panel is already correctly positioned.
    
    if(!AreTrackingSteppersReady())
    {
      #ifdef USE_DEBUG_APP
        Serial.println("[TRACKING] Stepper initialization skipped: azimuth/elevation STEP/DIR not initialized.");
      #endif
        return false;
    }

    if(!IsSystemTimeValid())
    {
      #ifdef USE_DEBUG_APP
        Serial.println("[TRACKING] Cannot initialize tracking: system time not valid yet.");
      #endif
        return false;
    }

    float currentAzimuth = 0.0f;
    float currentElevation = 0.0f;
    if(!ComputeSunPositionAtUtcEpoch(time(nullptr), currentAzimuth, currentElevation))
    {
      #ifdef USE_DEBUG_APP
        Serial.println("[TRACKING] Cannot initialize tracking: solar position computation failed.");
      #endif
        return false;
    }

    // Clamp to valid ranges
    if(currentAzimuth < 0.0f) { currentAzimuth = 0.0f; }
    if(currentAzimuth > 360.0f) { currentAzimuth = 360.0f; }
    if(currentElevation < 0.0f) { currentElevation = 0.0f; }
    if(currentElevation > 90.0f) { currentElevation = 90.0f; }

    // Initialize the controller internal positions to the current solar position
    azimuthController.initializeToPosition(currentAzimuth);
    elevationController.initializeToPosition(currentElevation);

  #ifdef USE_DEBUG_APP
    Serial.printf("[TRACKING] Tracking initialized: Azimuth=%.1f°, Elevation=%.1f°\n", currentAzimuth, currentElevation);
  #endif

    return true;
}

bool JogMotorDirect(const char* axis, int8_t direction, float incrementDegrees)
{
    // Direct motor jog: move motor by a specific angle without changing the controller's internal position.
    // This is for manual testing/diagnostics only - the system's assumed position remains unchanged.
    
    if(!AreTrackingSteppersReady())
    {
      #ifdef USE_DEBUG_APP
        Serial.println("[JOG] Motor jog skipped: steppers not initialized.");
      #endif
        return false;
    }

    if(incrementDegrees <= 0.0f)
    {
        return false;
    }

    // Clamp increment to reasonable values
    if(incrementDegrees > 10.0f) { incrementDegrees = 10.0f; }

    if(axis == nullptr)
    {
        return false;
    }

    int32_t jogSteps = 0;

    if(strcmp(axis, "azimuth") == 0)
    {
        float stepsPerDeg = azimuthController.getStepsPerDegree();
        if(stepsPerDeg <= 0.0f) { stepsPerDeg = 10.0f; }
        
        float totalStepsFloat = incrementDegrees * stepsPerDeg;
        jogSteps = (int32_t)std::lround(totalStepsFloat);
        
        if(direction < 0) { jogSteps = -jogSteps; }

        if(Stepper1 != nullptr && !Stepper1->isRunning())
        {
            Stepper1->move(jogSteps);
          #ifdef USE_DEBUG_APP
            Serial.printf("[JOG] Azimuth: %d steps (%s)\n", jogSteps, direction > 0 ? "right" : "left");
          #endif
            return true;
        }
    }
    else if(strcmp(axis, "elevation") == 0)
    {
        float stepsPerDeg = elevationController.getStepsPerDegree();
        if(stepsPerDeg <= 0.0f) { stepsPerDeg = 10.0f; }
        
        float totalStepsFloat = incrementDegrees * stepsPerDeg;
        jogSteps = (int32_t)std::lround(totalStepsFloat);
        
        if(direction < 0) { jogSteps = -jogSteps; }

        if(Stepper2 != nullptr && !Stepper2->isRunning())
        {
            Stepper2->move(jogSteps);
          #ifdef USE_DEBUG_APP
            Serial.printf("[JOG] Elevation: %d steps (%s)\n", jogSteps, direction > 0 ? "up" : "down");
          #endif
            return true;
        }
    }

    return false;
}

bool MovePanelToCurrentCalculatedPosition()
{
    if(!AreTrackingSteppersReady())
    {
      #ifdef USE_DEBUG_APP
        Serial.println("[STEPPER] Move skipped: azimuth/elevation STEP/DIR not initialized.");
      #endif
        return false;
    }

    if(!IsSystemTimeValid())
    {
        return false;
    }

    float targetAzimuth = 0.0f;
    float targetElevation = 0.0f;
    if(!ComputeSunPositionAtUtcEpoch(time(nullptr), targetAzimuth, targetElevation))
    {
        return false;
    }

    if(!IsTargetWithinDailyTrackingRange(targetAzimuth, targetElevation))
    {
        StopTrackingAxisMotion();
        return false;
    }

    uint16_t targetAzimuthDeg = QuantizeDegrees10(targetAzimuth);
    uint16_t targetElevationDeg = QuantizeDegrees10(targetElevation);

    int16_t azResult = azimuthController.moveToAngle(targetAzimuthDeg);
    int16_t elResult = elevationController.moveToAngle(targetElevationDeg);

    return (azResult >= 0) && (elResult >= 0);
}

bool SetTrackingTestOverride(float targetAzimuth, float targetElevation)
{
    if(!AreTrackingSteppersReady())
    {
        TrackingOverrideLastFailureCode = 1u;
      #ifdef USE_DEBUG_APP
        Serial.println("[STEPPER] Override move skipped: azimuth/elevation STEP/DIR not initialized.");
      #endif
        return false;
    }

    if(!IsTargetWithinDailyTrackingRange(targetAzimuth, targetElevation))
    {
        TrackingOverrideLastFailureCode = 2u;
        StopTrackingAxisMotion();
        TrackingTestOverrideActive = false;
        return false;
    }

    uint16_t targetAzimuthDeg = QuantizeDegrees10(targetAzimuth);
    uint16_t targetElevationDeg = QuantizeDegrees10(targetElevation);

    int16_t azResult = azimuthController.moveToAngle(targetAzimuthDeg);
    int16_t elResult = elevationController.moveToAngle(targetElevationDeg);

    bool ok = (azResult >= 0) && (elResult >= 0);
    TrackingOverrideLastFailureCode = ok ? 0u : 3u;

    TrackingTestOverrideAzimuth = targetAzimuth;
    TrackingTestOverrideElevation = targetElevation;
    TrackingTestOverrideActive = ok;

    return ok;
}

String GetTrackingOverrideLastFailureSummary()
{
    switch(TrackingOverrideLastFailureCode)
    {
        case 1u:
            return "steppers_not_ready";
        case 2u:
            return "target_out_of_daily_range";
        case 3u:
            return "move_command_rejected";
        default:
            return "none";
    }
}

bool CancelTrackingTestOverrideAndReturn()
{
    TrackingTestOverrideActive = false;
    return MovePanelToCurrentCalculatedPosition();
}

bool IsTrackingTestOverrideActive()
{
    return TrackingTestOverrideActive;
}

void ApplySunsetReturnPreposition()
{
    if(TrackingTestOverrideActive)
    {
        return;
    }

    if(!IsSystemTimeValid())
    {
        return;
    }

    time_t nextSunriseLocalEpoch = 0;
    int32_t targetDateKey = -1;

    if(!GetNextSunriseLocalEpoch(nextSunriseLocalEpoch, targetDateKey))
    {
        return;
    }

    if(SunsetReturnLastAttemptDateKey == targetDateKey)
    {
        return;
    }

    time_t triggerEpochLocal = 0;
    if(!GetSunsetReturnTriggerEpoch(triggerEpochLocal))
    {
        return;
    }

    time_t localNow = GetLocalTimeFromConfigOffset();
    if(localNow < triggerEpochLocal)
    {
        return;
    }

    long offsetSeconds = (long)(GetConfiguredUtcOffsetHours() * 3600.0);
    time_t sunriseUtcEpoch = nextSunriseLocalEpoch - offsetSeconds;

    float targetAzimuth = 0.0f;
    float targetElevation = 0.0f;
    if(!ComputeSunPositionAtUtcEpoch(sunriseUtcEpoch, targetAzimuth, targetElevation))
    {
        SunsetReturnLastAttemptDateKey = targetDateKey;
        SunsetReturnLastAttemptSucceeded = false;
        return;
    }

    if(!IsTargetWithinDailyTrackingRange(targetAzimuth, targetElevation))
    {
        StopTrackingAxisMotion();
        SunsetReturnLastAttemptDateKey = targetDateKey;
        SunsetReturnLastAttemptSucceeded = false;
        return;
    }

    uint16_t targetAzimuthDeg = QuantizeDegrees10(targetAzimuth);
    uint16_t targetElevationDeg = QuantizeDegrees10(targetElevation);

    int16_t azResult = azimuthController.moveToAngle(targetAzimuthDeg);
    int16_t elResult = elevationController.moveToAngle(targetElevationDeg);

    SunsetReturnLastAttemptDateKey = targetDateKey;
    SunsetReturnLastAttemptSucceeded = (azResult >= 0) && (elResult >= 0);

  #ifdef USE_DEBUG_APP
    if(SunsetReturnLastAttemptSucceeded)
    {
        Serial.printf("[SUNSET RETURN] Preposition done for %ld. Target az=%.2f el=%.2f\n",
                      (long)targetDateKey,
                      targetAzimuth,
                      targetElevation);
    }
    else
    {
        Serial.printf("[SUNSET RETURN] Preposition failed for %ld. Target az=%.2f el=%.2f\n",
                      (long)targetDateKey,
                      targetAzimuth,
                      targetElevation);
    }
  #endif
}

String GetCalculatedAzimuthSummary()
{
    if(!IsSystemTimeValid())
    {
        return "Waiting for NTP";
    }

    float azimuthDeg = 0.0f;
    float elevationDeg = 0.0f;
    if(!ComputeSunPositionAtUtcEpoch(time(nullptr), azimuthDeg, elevationDeg))
    {
        return "Unavailable";
    }

    return String(azimuthDeg, 1) + " deg";
}

String GetCalculatedElevationSummary()
{
    if(!IsSystemTimeValid())
    {
        return "Waiting for NTP";
    }

    float azimuthDeg = 0.0f;
    float elevationDeg = 0.0f;
    if(!ComputeSunPositionAtUtcEpoch(time(nullptr), azimuthDeg, elevationDeg))
    {
        return "Unavailable";
    }

    return String(elevationDeg, 1) + " deg";
}

String GetTodayAzimuthMinSummary()
{
    float minAzimuthDeg = 0.0f;
    float maxAzimuthDeg = 0.0f;
    if(!GetTodayAzimuthBounds(minAzimuthDeg, maxAzimuthDeg))
    {
        return "Waiting for NTP";
    }

    return String(minAzimuthDeg, 1) + " deg";
}

String GetTodayAzimuthMaxSummary()
{
    float minAzimuthDeg = 0.0f;
    float maxAzimuthDeg = 0.0f;
    if(!GetTodayAzimuthBounds(minAzimuthDeg, maxAzimuthDeg))
    {
        return "Waiting for NTP";
    }

    return String(maxAzimuthDeg, 1) + " deg";
}

String GetTodayElevationMaxSummary()
{
    if(!IsSystemTimeValid())
    {
        return "Waiting for NTP";
    }

    time_t localNow = GetLocalTimeFromConfigOffset();
    struct tm localTm;
    if(gmtime_r(&localNow, &localTm) == nullptr)
    {
        return "Waiting for NTP";
    }

    int32_t todayDateKey = BuildDateKey(localTm.tm_year + 1900, localTm.tm_mon + 1, localTm.tm_mday);

    static int32_t cachedDateKey = -1;
    static float cachedElevationMax = 0.0f;

    if(cachedDateKey != todayDateKey)
    {
        float calculatedElevationMax = 0.0f;
        if(!GetTodayElevationMax(calculatedElevationMax))
        {
            return "Waiting for NTP";
        }

        cachedDateKey = todayDateKey;
        cachedElevationMax = calculatedElevationMax;
    }

    return String(cachedElevationMax, 1) + " deg";
}

String GetTrackingOverrideStatusSummary()
{
    if(!TrackingTestOverrideActive)
    {
        return "Off";
    }

    char buffer[40];
    snprintf(buffer,
             sizeof(buffer),
             "ON (az %.1f el %.1f)",
             TrackingTestOverrideAzimuth,
             TrackingTestOverrideElevation);
    return String(buffer);
}
