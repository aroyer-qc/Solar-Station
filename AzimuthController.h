#ifndef AZIMUTH_CONTROLLER_H
#define AZIMUTH_CONTROLLER_H

#include <Arduino.h>
#include "FastAccelStepper.h"

class AzimuthController
{
    public:
  
                 AzimuthController          ();
          
        int8_t   init                       ();
        void     initializeToPosition       (float azimuthDegrees);

        int16_t  moveToAngle                (uint16_t targetAzimuthDeciDeg);
        void     setStepsPerDegree          (float value);
        float    getStepsPerDegree          ()                                                  { return stepsPerDeg; }
        float    getCurrentAzimuth          ()                                                  { return (float)currentAzimuthDeciDeg / 10.0f; }

    private:
        FastAccelStepper*               stepper;
        uint16_t                        currentAzimuthDeciDeg;
        int32_t                         currentAzimuthSteps;
        float                           stepsPerDeg;
        uint16_t                        stepSpeedHz;
        uint16_t                        stepAcceleration;
        uint16_t                        azimuthDegMaxDeci;
        uint16_t                        azimuthDegMinDeci;
        uint16_t                        azimuthTimeThresholdMs;
};

#endif // AZIMUTH_CONTROLLER_H
