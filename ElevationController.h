#ifndef ElevationCONTROLLER_H
#define ElevationCONTROLLER_H

#include <Arduino.h>
#include "FastAccelStepper.h"

class ElevationController
{
    public:
      
                        ElevationController             ();

        // Movement methods
        void            init                            ();
        void            initializeToPosition            (float elevationDegrees);
        int16_t         moveToAngle                     (uint16_t targetElevationDeciDeg);
        void            setStepsPerDegree               (float value);
        float           getStepsPerDegree               ()      { return stepsPerDeg; }
        float           getCurrentElevation             ()      { return (float)currentElevationDeciDeg / 10.0f; }

    private:
        FastAccelStepper* stepper;

        uint16_t        currentElevationDeciDeg;
        int32_t         currentElevationSteps;
        float           stepsPerDeg;
        uint16_t        stepSpeedHz;                // Motion speed in steps/second
        uint16_t        stepAcceleration;           // Motion acceleration in steps/second^2

        uint16_t        elevationDegMaxDeci;
        uint16_t        elevationDegMinDeci;
        uint16_t        elevationTimeThresholdMs;
};

#endif // ElevationCONTROLLER_H
