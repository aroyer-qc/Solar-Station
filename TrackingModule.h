#ifndef TRACKING_MODULE_H
#define TRACKING_MODULE_H

#include <Arduino.h>
#include <time.h>

bool GetNextSunriseLocalEpoch(time_t& nextSunriseLocalEpoch, int32_t& targetDateKey);
bool ComputeSunPositionAtUtcEpoch(time_t utcEpoch, float& azimuthDeg, float& elevationDeg);
bool GetTodayAzimuthBounds(float& minAzimuthDeg, float& maxAzimuthDeg);
bool GetTodayElevationMax(float& maxElevationDeg);

bool InitializeTrackingFromSolarPosition();
bool MovePanelToCurrentCalculatedPosition();
bool JogMotorDirect(const char* axis, int8_t direction, float incrementDegrees);
bool SetTrackingTestOverride(float targetAzimuth, float targetElevation);
String GetTrackingOverrideLastFailureSummary();
bool CancelTrackingTestOverrideAndReturn();
bool IsTrackingTestOverrideActive();
void ApplySunsetReturnPreposition();

String GetCalculatedAzimuthSummary();
String GetCalculatedElevationSummary();
String GetTodayAzimuthMinSummary();
String GetTodayAzimuthMaxSummary();
String GetTodayElevationMaxSummary();
String GetTrackingOverrideStatusSummary();

#endif // TRACKING_MODULE_H
