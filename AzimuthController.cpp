#include "AzimuthController.h"
#include "ConfigModule.h"
#include <cmath>

// #define USE_DEBUG_AZIMUTH

static uint16_t DegToDeciDeg(float degrees)
{
    if(degrees <= 0.0f)
    {
        return 0u;
    }

    if(degrees >= 6553.5f)
    {
        return 65535u;
    }

    return (uint16_t)((degrees * 10.0f) + 0.5f);
}

static int32_t DeciDegToSteps(uint16_t deciDeg, float stepsPerDeg)
{
    float stepsFloat = ((float)deciDeg * stepsPerDeg) / 10.0f;
    return (int32_t)std::lround(stepsFloat);
}

extern ConfigModule Config;
extern FastAccelStepper* Stepper1;

// ----------------- Azimuth Controller Constructor -----------------

AzimuthController::AzimuthController()
{}

// ----------------- Azimuth control functions -----------------

int8_t AzimuthController::init()
{
    stepper = Stepper1;

    if(stepper == nullptr)
    {
	  #ifdef USE_DEBUG_AZIMUTH
        Serial.println(F("[ERROR] Stepper1 unavailable."));
	  #endif
        return -1;
    }

    float cfgStepsPerDeg = Config.GetAzimuthStepsPerDegree();
    if(cfgStepsPerDeg <= 0.0f) { cfgStepsPerDeg = 10.0f; }
    setStepsPerDegree(cfgStepsPerDeg);
    uint32_t cfgSpeed = Config.GetAzimuthStepSpeedHz();
    if(cfgSpeed == 0u) { cfgSpeed = 1200u; }
    if(cfgSpeed > 65535u) { cfgSpeed = 65535u; }
    stepSpeedHz = (uint16_t)cfgSpeed;

    uint32_t cfgAccel = Config.GetAzimuthStepAcceleration();
    if(cfgAccel == 0u) { cfgAccel = 3000u; }
    if(cfgAccel > 65535u) { cfgAccel = 65535u; }
    stepAcceleration = (uint16_t)cfgAccel;

    azimuthDegMinDeci = DegToDeciDeg((float)Config.GetAzimuthDegMin());
    azimuthDegMaxDeci = DegToDeciDeg((float)Config.GetAzimuthDegMax());
    if(azimuthDegMaxDeci <= azimuthDegMinDeci)
    {
        azimuthDegMinDeci = 0u;
        azimuthDegMaxDeci = 3600u;
    }

    uint32_t cfgThreshold = Config.GetAzimuthTimeThreshold();
    if(cfgThreshold > 65535u) { cfgThreshold = 65535u; }
    azimuthTimeThresholdMs = (uint16_t)cfgThreshold;

    currentAzimuthDeciDeg = azimuthDegMinDeci;
    currentAzimuthSteps = DeciDegToSteps(currentAzimuthDeciDeg, stepsPerDeg);

    stepper->setSpeedInHz(stepSpeedHz);
    stepper->setAcceleration(stepAcceleration);

    return 0;
}

void AzimuthController::initializeToPosition(float azimuthDegrees)
{
    // Initialize the current position to a calculated value instead of minimum.
    // This prevents unwanted motor movement at boot when the panel is already correctly positioned.
    
    if(azimuthDegrees < 0.0f) { azimuthDegrees = 0.0f; }
    if(azimuthDegrees > 360.0f) { azimuthDegrees = 360.0f; }

    currentAzimuthDeciDeg = DegToDeciDeg(azimuthDegrees);
    currentAzimuthSteps = DeciDegToSteps(currentAzimuthDeciDeg, stepsPerDeg);
    
  #ifdef USE_DEBUG_AZIMUTH
    Serial.printf("[AZIMUTH] Initialized to position %.1f degrees (%u steps)\n", azimuthDegrees, (uint32_t)currentAzimuthSteps);
  #endif
}

int16_t AzimuthController::moveToAngle(uint16_t targetAzimuthDeciDeg)
{
    if(stepper == nullptr)
    {
        return -1;
    }

    azimuthDegMinDeci = DegToDeciDeg((float)Config.GetAzimuthDegMin());
    azimuthDegMaxDeci = DegToDeciDeg((float)Config.GetAzimuthDegMax());
    if(azimuthDegMaxDeci <= azimuthDegMinDeci)
    {
        azimuthDegMinDeci = 0u;
        azimuthDegMaxDeci = 3600u;
    }

    uint32_t cfgThreshold = Config.GetAzimuthTimeThreshold();
    if(cfgThreshold > 65535u) { cfgThreshold = 65535u; }
    azimuthTimeThresholdMs = (uint16_t)cfgThreshold;

    float cfgStepsPerDeg = Config.GetAzimuthStepsPerDegree();
    if(cfgStepsPerDeg <= 0.0f) { cfgStepsPerDeg = 10.0f; }
    setStepsPerDegree(cfgStepsPerDeg);
    uint32_t cfgSpeed = Config.GetAzimuthStepSpeedHz();
    if(cfgSpeed == 0u) { cfgSpeed = 1200u; }
    if(cfgSpeed > 65535u) { cfgSpeed = 65535u; }
    stepSpeedHz = (uint16_t)cfgSpeed;

    uint32_t cfgAccel = Config.GetAzimuthStepAcceleration();
    if(cfgAccel == 0u) { cfgAccel = 3000u; }
    if(cfgAccel > 65535u) { cfgAccel = 65535u; }
    stepAcceleration = (uint16_t)cfgAccel;

    uint16_t requestedDeciDeg = targetAzimuthDeciDeg;
    
    if(requestedDeciDeg < azimuthDegMinDeci) { requestedDeciDeg = azimuthDegMinDeci; }
    if(requestedDeciDeg > azimuthDegMaxDeci) { requestedDeciDeg = azimuthDegMaxDeci; }

    int32_t targetSteps = DeciDegToSteps(requestedDeciDeg, stepsPerDeg);
    int32_t deltaSteps = targetSteps - currentAzimuthSteps;

    uint32_t thresholdSteps = 1u;
    if(stepSpeedHz > 0u)
    {
        uint32_t numerator = (uint32_t)azimuthTimeThresholdMs * (uint32_t)stepSpeedHz;
        uint32_t roundedUp = (numerator + 999u) / 1000u;
        if(roundedUp == 0u) { roundedUp = 1u; }
        thresholdSteps = roundedUp;
    }

    uint32_t deltaStepsAbs = (deltaSteps < 0) ? (uint32_t)(-deltaSteps) : (uint32_t)deltaSteps;

    if(deltaStepsAbs < thresholdSteps)
    {
        return (int16_t)currentAzimuthDeciDeg;
    }

    if(deltaSteps == 0)
    {
        currentAzimuthDeciDeg = requestedDeciDeg;
        currentAzimuthSteps = targetSteps;
        return (int16_t)currentAzimuthDeciDeg;
    }

    if(stepper->isRunning())
    {
        return (int16_t)currentAzimuthDeciDeg;
    }

    stepper->setSpeedInHz(stepSpeedHz);
    stepper->setAcceleration(stepAcceleration);
    
    auto queued = stepper->move(deltaSteps);
    
    if(static_cast<int>(queued) < 0)
    {
      #ifdef USE_DEBUG_AZIMUTH
        Serial.println(F("[ERROR] Azimuth move command rejected by FastAccelStepper."));
      #endif
        return -1;
    }

    currentAzimuthDeciDeg = requestedDeciDeg;
    currentAzimuthSteps = targetSteps;

    return (int16_t)currentAzimuthDeciDeg;
}

void AzimuthController::setStepsPerDegree(float value)
{
    if(value <= 0.0f)
    {
        return;
    }

    stepsPerDeg = value;
}