#ifndef SCHEDULER_MODULE_H
#define SCHEDULER_MODULE_H

#include <Arduino.h>
#include <time.h>

bool IsSystemTimeValid();
time_t GetLocalTimeFromConfigOffset();
double GetConfiguredUtcOffsetHours();
uint16_t GetCurrentLocalMinutes();
uint8_t GetCurrentLocalWeekdayMask();
uint16_t NormalizeMinutes(int32_t totalMinutes);
uint16_t ResolveScheduleBoundaryMinutes(uint8_t boundaryType,
                                        uint8_t hour,
                                        uint8_t minute,
                                        int16_t offsetMinutes,
                                        uint16_t sunriseMinutesUtc,
                                        uint16_t sunsetMinutesUtc);
bool IsScheduleActive(uint16_t startMinutesUtc, uint16_t endMinutesUtc, uint16_t currentMinutesUtc);
bool GetSunriseSunsetUtcMinutesForEpoch(time_t utcEpoch, uint16_t& sunriseMinutesUtc, uint16_t& sunsetMinutesUtc);
bool GetSunriseSunsetUtcMinutes(uint16_t& sunriseMinutesUtc, uint16_t& sunsetMinutesUtc);
bool GetSunriseMinutesForLocalDate(int year, int month, int day, uint16_t& sunriseMinutesLocal);
int32_t BuildDateKey(int year, int month, int day);
bool HasOutputScheduleConflict(uint8_t outputIndex);

void ApplyOutputSchedules();
String GetOutputScheduleSummary(uint8_t outputIndex);
String GetOutputControlStatus(uint8_t outputIndex);
String GetOutputNextEventTimeSummary(uint8_t outputIndex);
String GetOutputNextEventReasonSummary(uint8_t outputIndex);

String GetLocalDateTimeSummary();
String GetNtpSyncStatusSummary();
String GetTodaySunriseSummary();
String GetTodaySunsetSummary();

#endif // SCHEDULER_MODULE_H
