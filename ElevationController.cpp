#include "ElevationController.h"
#include "ConfigModule.h"
#include <cmath>

// #define USE_DEBUG_ELEVATION

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
extern FastAccelStepper* Stepper2;



// ----------------- Elevation Controller Constructor -----------------

ElevationController::ElevationController()
{}

// --------------------------------------------------

void ElevationController::init()
{
    stepper = Stepper2;
    currentElevationDeciDeg = 0u;

    float cfgStepsPerDeg = Config.GetElevationStepsPerDegree();
    if(cfgStepsPerDeg <= 0.0f) { cfgStepsPerDeg = 10.0f; }
    setStepsPerDegree(cfgStepsPerDeg);

    uint32_t cfgSpeed = Config.GetElevationStepSpeedHz();
    if(cfgSpeed == 0u) { cfgSpeed = 1000u; }
    if(cfgSpeed > 65535u) { cfgSpeed = 65535u; }
    stepSpeedHz = (uint16_t)cfgSpeed;

    uint32_t cfgAccel = Config.GetElevationStepAcceleration();
    if(cfgAccel == 0u) { cfgAccel = 2500u; }
    if(cfgAccel > 65535u) { cfgAccel = 65535u; }
    stepAcceleration = (uint16_t)cfgAccel;

    elevationDegMinDeci = DegToDeciDeg((float)Config.GetElevationDegMin());
    elevationDegMaxDeci = DegToDeciDeg((float)Config.GetElevationDegMax());
    if(elevationDegMaxDeci <= elevationDegMinDeci)
    {
        elevationDegMinDeci = 0u;
        elevationDegMaxDeci = 900u;
    }

    uint32_t cfgThreshold = Config.GetElevationTimeThreshold();
    if(cfgThreshold > 65535u) { cfgThreshold = 65535u; }
    elevationTimeThresholdMs = (uint16_t)cfgThreshold;

    if(stepper == nullptr)
    {
	  #ifdef USE_DEBUG_ELEVATION
        Serial.println(F("[ERROR] Stepper2 unavailable."));
	  #endif	
        return;
    }

    stepper->setSpeedInHz(stepSpeedHz);
    stepper->setAcceleration(stepAcceleration);

    currentElevationDeciDeg = elevationDegMinDeci;
    currentElevationSteps = DeciDegToSteps(currentElevationDeciDeg, stepsPerDeg);
}

void ElevationController::initializeToPosition(float elevationDegrees)
{
    // Initialize the current position to a calculated value instead of minimum.
    // This prevents unwanted motor movement at boot when the panel is already correctly positioned.
    
    if(elevationDegrees < 0.0f) { elevationDegrees = 0.0f; }
    if(elevationDegrees > 90.0f) { elevationDegrees = 90.0f; }

    currentElevationDeciDeg = DegToDeciDeg(elevationDegrees);
    currentElevationSteps = DeciDegToSteps(currentElevationDeciDeg, stepsPerDeg);
    
  #ifdef USE_DEBUG_ELEVATION
    Serial.printf("[ELEVATION] Initialized to position %.1f degrees (%u steps)\n", elevationDegrees, (uint32_t)currentElevationSteps);
  #endif
}

int16_t ElevationController::moveToAngle(uint16_t targetElevationDeciDeg)
{
    if(stepper == nullptr)
    {
        return -1;
    }

    elevationDegMinDeci = DegToDeciDeg((float)Config.GetElevationDegMin());
    elevationDegMaxDeci = DegToDeciDeg((float)Config.GetElevationDegMax());
    if(elevationDegMaxDeci <= elevationDegMinDeci)
    {
        elevationDegMinDeci = 0u;
        elevationDegMaxDeci = 900u;
    }

    uint32_t cfgThreshold = Config.GetElevationTimeThreshold();
    if(cfgThreshold > 65535u) { cfgThreshold = 65535u; }
    elevationTimeThresholdMs = (uint16_t)cfgThreshold;

    float cfgStepsPerDeg = Config.GetElevationStepsPerDegree();
    if(cfgStepsPerDeg <= 0.0f) { cfgStepsPerDeg = 10.0f; }
    setStepsPerDegree(cfgStepsPerDeg);
    uint32_t cfgSpeed = Config.GetElevationStepSpeedHz();
    if(cfgSpeed == 0u) { cfgSpeed = 1000u; }
    if(cfgSpeed > 65535u) { cfgSpeed = 65535u; }
    stepSpeedHz = (uint16_t)cfgSpeed;

    uint32_t cfgAccel = Config.GetElevationStepAcceleration();
    if(cfgAccel == 0u) { cfgAccel = 2500u; }
    if(cfgAccel > 65535u) { cfgAccel = 65535u; }
    stepAcceleration = (uint16_t)cfgAccel;

    uint16_t requestedDeciDeg = targetElevationDeciDeg;

    if(requestedDeciDeg < elevationDegMinDeci) { requestedDeciDeg = elevationDegMinDeci; }
    if(requestedDeciDeg > elevationDegMaxDeci) { requestedDeciDeg = elevationDegMaxDeci; }

    int32_t targetSteps = DeciDegToSteps(requestedDeciDeg, stepsPerDeg);
    int32_t deltaSteps = targetSteps - currentElevationSteps;

    uint32_t thresholdSteps = 1u;
    if(stepSpeedHz > 0u)
    {
        uint32_t numerator = (uint32_t)elevationTimeThresholdMs * (uint32_t)stepSpeedHz;
        uint32_t roundedUp = (numerator + 999u) / 1000u;
        if(roundedUp == 0u) { roundedUp = 1u; }
        thresholdSteps = roundedUp;
    }

    uint32_t deltaStepsAbs = (deltaSteps < 0) ? (uint32_t)(-deltaSteps) : (uint32_t)deltaSteps;

    if(deltaStepsAbs < thresholdSteps)
    {
        return (int16_t)currentElevationDeciDeg;
    }

    if(deltaSteps == 0)
    {
        currentElevationDeciDeg = requestedDeciDeg;
        currentElevationSteps = targetSteps;
        return (int16_t)currentElevationDeciDeg;
    }

    if(stepper->isRunning())
    {
        return (int16_t)currentElevationDeciDeg;
    }

    stepper->setSpeedInHz(stepSpeedHz);
    stepper->setAcceleration(stepAcceleration);
        auto queued = stepper->move(deltaSteps);
        if(static_cast<int>(queued) < 0)
        {
            #ifdef USE_DEBUG_ELEVATION
                Serial.println(F("[ERROR] Elevation move command rejected by FastAccelStepper."));
            #endif
                return -1;
        }

    currentElevationDeciDeg = requestedDeciDeg;
    currentElevationSteps = targetSteps;

    return (int16_t)currentElevationDeciDeg;
}

void ElevationController::setStepsPerDegree(float value)
{
    if(value <= 0.0f)
    {
        return;
    }

    stepsPerDeg = value;
}
