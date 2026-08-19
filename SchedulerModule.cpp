#include "SchedulerModule.h"

#include "ConfigModule.h"
#include "SolarFixed.h"
#include <time.h>

extern ConfigModule Config;
extern bool SetMosfetOutput(uint8_t OutputIndex, bool IsOn);
extern bool SetMosfetPwmPercent(uint8_t OutputIndex, uint8_t DutyPercent);

namespace
{
    struct ResolvedScheduleState
    {
        bool Enabled;
        bool Valid;
        bool Active;
        uint16_t StartMinutes;
        uint16_t EndMinutes;
        uint8_t DutyPercent;
        uint8_t StartType;
        int16_t StartOffsetMinutes;
        uint8_t EndType;
        int16_t EndOffsetMinutes;
        uint8_t ActiveDaysMask;
    };

    static uint8_t GetPreviousWeekdayMask(uint8_t currentWeekdayMask)
    {
        return currentWeekdayMask == 0x01u ? 0x40u : (currentWeekdayMask >> 1u);
    }

    static bool IsScheduleActiveOnSelectedDay(const ResolvedScheduleState& state,
                                              uint8_t currentWeekdayMask,
                                              uint8_t previousWeekdayMask,
                                              uint16_t currentMinutes)
    {
        if(!state.Enabled || !state.Valid || state.StartMinutes == state.EndMinutes)
        {
            return false;
        }

        if(state.StartMinutes < state.EndMinutes)
        {
            return (state.ActiveDaysMask & currentWeekdayMask) != 0u &&
                   currentMinutes >= state.StartMinutes && currentMinutes < state.EndMinutes;
        }

        if(currentMinutes >= state.StartMinutes)
        {
            return (state.ActiveDaysMask & currentWeekdayMask) != 0u;
        }

        return currentMinutes < state.EndMinutes &&
               (state.ActiveDaysMask & previousWeekdayMask) != 0u;
    }

    static bool ScheduleNeedsSunTimes(const OutputSchedule_t& schedule)
    {
        return (schedule.StartType == 1u) || (schedule.StartType == 2u) ||
               (schedule.EndType == 1u) || (schedule.EndType == 2u);
    }

    static bool BuildResolvedScheduleState(uint8_t outputIndex,
                                           uint8_t slotIndex,
                                           uint16_t currentMinutes,
                                           uint16_t sunriseMinutes,
                                           uint16_t sunsetMinutes,
                                           bool hasSunTimes,
                                           ResolvedScheduleState& state,
                                           bool& outWaitSun)
    {
        const OutputSchedule_t& schedule = Config.GetOutputSchedule(outputIndex, slotIndex);
        state.Enabled = schedule.Enabled;
        state.Valid = false;
        state.Active = false;
        state.StartMinutes = 0u;
        state.EndMinutes = 0u;
        state.DutyPercent = schedule.DutyPercent;
        state.StartType = schedule.StartType;
        state.StartOffsetMinutes = schedule.StartOffsetMinutes;
        state.EndType = schedule.EndType;
        state.EndOffsetMinutes = schedule.EndOffsetMinutes;
        state.ActiveDaysMask = Config.GetOutputScheduleActiveDaysMask(outputIndex, slotIndex);

        if(!schedule.Enabled)
        {
            return false;
        }

        if(ScheduleNeedsSunTimes(schedule) && !hasSunTimes)
        {
            outWaitSun = true;
            return true;
        }

        state.StartMinutes = ResolveScheduleBoundaryMinutes(schedule.StartType,
                                                            schedule.StartHour,
                                                            schedule.StartMinute,
                                                            schedule.StartOffsetMinutes,
                                                            sunriseMinutes,
                                                            sunsetMinutes);

        state.EndMinutes = ResolveScheduleBoundaryMinutes(schedule.EndType,
                                                          schedule.EndHour,
                                                          schedule.EndMinute,
                                                          schedule.EndOffsetMinutes,
                                                          sunriseMinutes,
                                                          sunsetMinutes);

        state.Valid = true;
        uint8_t currentWeekdayMask = GetCurrentLocalWeekdayMask();
        state.Active = IsScheduleActiveOnSelectedDay(state,
                                  currentWeekdayMask,
                                  GetPreviousWeekdayMask(currentWeekdayMask),
                                  currentMinutes);
        return true;
    }

    static bool DoSchedulesOverlap(const ResolvedScheduleState& a, const ResolvedScheduleState& b)
    {
        if(!a.Enabled || !b.Enabled || !a.Valid || !b.Valid)
        {
            return false;
        }

        for(uint8_t weekdayIndex = 0u; weekdayIndex < 7u; weekdayIndex++)
        {
            uint8_t weekdayMask = (uint8_t)(1u << weekdayIndex);
            uint8_t previousWeekdayMask = GetPreviousWeekdayMask(weekdayMask);

            for(uint16_t minute = 0u; minute < 1440u; minute++)
            {
                if(IsScheduleActiveOnSelectedDay(a, weekdayMask, previousWeekdayMask, minute) &&
                   IsScheduleActiveOnSelectedDay(b, weekdayMask, previousWeekdayMask, minute))
                {
                    return true;
                }
            }
        }

        return false;
    }

    static void AppendScheduleSlotSummary(String& summary, uint8_t slotLabel, const ResolvedScheduleState& state)
    {
        if(summary.length() > 0)
        {
            summary += " | ";
        }

        summary += "S";
        summary += String(slotLabel);
        summary += ": ";

        if(!state.Enabled)
        {
            summary += "disabled";
            return;
        }

        if(!state.Valid)
        {
            summary += "waiting sun";
            return;
        }

        char buffer[48];
        snprintf(buffer, sizeof(buffer), "%02u:%02u -> %02u:%02u @ %u%%",
                 state.StartMinutes / 60u,
                 state.StartMinutes % 60u,
                 state.EndMinutes / 60u,
                 state.EndMinutes % 60u,
                 state.DutyPercent);
        summary += buffer;
            summary += " days 0x";
            summary += String(state.ActiveDaysMask, HEX);
    }
}

bool IsSystemTimeValid()
{
    time_t now = time(nullptr);
    return now > 1700000000;
}

time_t GetLocalTimeFromConfigOffset()
{
    time_t nowUtc = time(nullptr);
    long offsetSeconds = (long)(GetConfiguredUtcOffsetHours() * 3600.0);
    return nowUtc + offsetSeconds;
}

double GetConfiguredUtcOffsetHours()
{
    double offset = Config.GetST_TimeZoneOffset();

    if(Config.GetST_UseDST())
    {
        offset += 1.0;
    }

    return offset;
}

uint16_t GetCurrentLocalMinutes()
{
    time_t localNow = GetLocalTimeFromConfigOffset();
    struct tm localTimeInfo;
    gmtime_r(&localNow, &localTimeInfo);
    return (uint16_t)(localTimeInfo.tm_hour * 60) + localTimeInfo.tm_min;
}

uint8_t GetCurrentLocalWeekdayMask()
{
    time_t localNow = GetLocalTimeFromConfigOffset();
    struct tm localTimeInfo;
    gmtime_r(&localNow, &localTimeInfo);
    uint8_t mondayBasedDayIndex = (uint8_t)((localTimeInfo.tm_wday + 6) % 7);
    return (uint8_t)(1u << mondayBasedDayIndex);
}

uint16_t NormalizeMinutes(int32_t totalMinutes)
{
    while(totalMinutes < 0)
    {
        totalMinutes += 1440;
    }

    while(totalMinutes >= 1440)
    {
        totalMinutes -= 1440;
    }

    return (uint16_t)totalMinutes;
}

uint16_t ResolveScheduleBoundaryMinutes(uint8_t boundaryType,
                                        uint8_t hour,
                                        uint8_t minute,
                                        int16_t offsetMinutes,
                                        uint16_t sunriseMinutesUtc,
                                        uint16_t sunsetMinutesUtc)
{
    int32_t baseMinutes;

    switch(boundaryType)
    {
        case 1:
            baseMinutes = sunriseMinutesUtc;
            break;

        case 2:
            baseMinutes = sunsetMinutesUtc;
            break;

        case 0:
        default:
            baseMinutes = (int32_t)(hour * 60u) + minute;
            break;
    }

    return NormalizeMinutes(baseMinutes + offsetMinutes);
}

bool IsScheduleActive(uint16_t startMinutesUtc, uint16_t endMinutesUtc, uint16_t currentMinutesUtc)
{
    uint16_t startMinutes = startMinutesUtc;
    uint16_t endMinutes = endMinutesUtc;

    if(startMinutes == endMinutes)
    {
        return false;
    }

    if(startMinutes < endMinutes)
    {
        return currentMinutesUtc >= startMinutes && currentMinutesUtc < endMinutes;
    }

    return currentMinutesUtc >= startMinutes || currentMinutesUtc < endMinutes;
}

bool GetSunriseSunsetUtcMinutes(uint16_t& sunriseMinutesUtc, uint16_t& sunsetMinutesUtc)
{
    return GetSunriseSunsetUtcMinutesForEpoch(time(nullptr), sunriseMinutesUtc, sunsetMinutesUtc);
}

bool GetSunriseSunsetUtcMinutesForEpoch(time_t utcEpoch, uint16_t& sunriseMinutesUtc, uint16_t& sunsetMinutesUtc)
{
    time_t localEpoch = utcEpoch + (time_t)(GetConfiguredUtcOffsetHours() * 3600.0);
    struct tm localTimeInfo;
    gmtime_r(&localEpoch, &localTimeInfo);

    uint16_t sunriseUtcOnly = 0;
    uint16_t sunsetUtcOnly = 0;
    if(!solar_compute_sunrise_sunset_utc(localTimeInfo.tm_year + 1900,
                                         localTimeInfo.tm_mon + 1,
                                         localTimeInfo.tm_mday,
                                         Q16(Config.GetST_Latitude()),
                                         Q16(Config.GetST_Longitude()),
                                         &sunriseUtcOnly,
                                         &sunsetUtcOnly))
    {
        return false;
    }

    int32_t offsetMinutes = (int32_t)(GetConfiguredUtcOffsetHours() * 60.0);
    sunriseMinutesUtc = NormalizeMinutes((int32_t)sunriseUtcOnly + offsetMinutes);
    sunsetMinutesUtc = NormalizeMinutes((int32_t)sunsetUtcOnly + offsetMinutes);
    return true;
}

int32_t BuildDateKey(int year, int month, int day)
{
    return (int32_t)(year * 10000 + month * 100 + day);
}

bool GetSunriseMinutesForLocalDate(int year, int month, int day, uint16_t& sunriseMinutesLocal)
{
    uint16_t sunriseUtc = 0;
    uint16_t sunsetUtc = 0;
    if(!solar_compute_sunrise_sunset_utc(year,
                                         month,
                                         day,
                                         Q16(Config.GetST_Latitude()),
                                         Q16(Config.GetST_Longitude()),
                                         &sunriseUtc,
                                         &sunsetUtc))
    {
        return false;
    }

    int32_t offsetMinutes = (int32_t)(GetConfiguredUtcOffsetHours() * 60.0);
    sunriseMinutesLocal = NormalizeMinutes((int32_t)sunriseUtc + offsetMinutes);
    return true;
}

void ApplyOutputSchedules()
{
    bool hasValidTime = IsSystemTimeValid();

    uint16_t currentMinutesUtc = 0;
    uint16_t sunriseMinutesUtc = 0;
    uint16_t sunsetMinutesUtc = 0;
    bool hasSunTimes = false;

    if(hasValidTime)
    {
        currentMinutesUtc = GetCurrentLocalMinutes();
        hasSunTimes = GetSunriseSunsetUtcMinutes(sunriseMinutesUtc, sunsetMinutesUtc);
    }

    for(uint8_t i = 0; i < ConfigModule::OUTPUT_COUNT; i++)
    {
        if(!Config.GetOutputAutomaticMode(i))
        {
            continue;
        }

        if(!hasValidTime)
        {
            SetMosfetOutput(i, false);
            continue;
        }

        ResolvedScheduleState schedule1;
        ResolvedScheduleState schedule2;
        bool waitSun = false;
        BuildResolvedScheduleState(i, 0u, currentMinutesUtc, sunriseMinutesUtc, sunsetMinutesUtc, hasSunTimes, schedule1, waitSun);
        BuildResolvedScheduleState(i, 1u, currentMinutesUtc, sunriseMinutesUtc, sunsetMinutesUtc, hasSunTimes, schedule2, waitSun);

        bool hasEnabledSchedule = schedule1.Enabled || schedule2.Enabled;
        if(!hasEnabledSchedule)
        {
            SetMosfetOutput(i, false);
            continue;
        }

        if(waitSun)
        {
            SetMosfetOutput(i, false);
            continue;
        }

        bool shouldBeOn = (schedule1.Valid && schedule1.Active) || (schedule2.Valid && schedule2.Active);
        uint8_t targetDuty = 0u;
        if(schedule1.Valid && schedule1.Active && schedule1.DutyPercent > targetDuty)
        {
            targetDuty = schedule1.DutyPercent;
        }

        if(schedule2.Valid && schedule2.Active && schedule2.DutyPercent > targetDuty)
        {
            targetDuty = schedule2.DutyPercent;
        }

        if(shouldBeOn)
        {
            SetMosfetPwmPercent(i, targetDuty);
        }
        else
        {
            SetMosfetOutput(i, false);
        }
    }
}

bool HasOutputScheduleConflict(uint8_t outputIndex)
{
    if(outputIndex >= ConfigModule::OUTPUT_COUNT)
    {
        return false;
    }

    uint16_t sunriseMinutes = 0u;
    uint16_t sunsetMinutes = 0u;
    bool hasSunTimes = GetSunriseSunsetUtcMinutes(sunriseMinutes, sunsetMinutes);
    bool waitSun = false;
    uint16_t currentMinutes = GetCurrentLocalMinutes();

    ResolvedScheduleState schedule1;
    ResolvedScheduleState schedule2;
    BuildResolvedScheduleState(outputIndex, 0u, currentMinutes, sunriseMinutes, sunsetMinutes, hasSunTimes, schedule1, waitSun);
    BuildResolvedScheduleState(outputIndex, 1u, currentMinutes, sunriseMinutes, sunsetMinutes, hasSunTimes, schedule2, waitSun);

    if(waitSun)
    {
        return false;
    }

    return DoSchedulesOverlap(schedule1, schedule2);
}

String GetOutputScheduleSummary(uint8_t outputIndex)
{
    if(outputIndex >= ConfigModule::OUTPUT_COUNT)
    {
        return "Invalid output";
    }

    if(!Config.GetOutputAutomaticMode(outputIndex))
    {
        return "Manual mode";
    }

    uint16_t sunriseMinutesUtc = 0;
    uint16_t sunsetMinutesUtc = 0;
    bool hasSunTimes = GetSunriseSunsetUtcMinutes(sunriseMinutesUtc, sunsetMinutesUtc);
    uint16_t currentMinutes = GetCurrentLocalMinutes();
    bool waitSun = false;

    ResolvedScheduleState schedule1;
    ResolvedScheduleState schedule2;
    BuildResolvedScheduleState(outputIndex, 0u, currentMinutes, sunriseMinutesUtc, sunsetMinutesUtc, hasSunTimes, schedule1, waitSun);
    BuildResolvedScheduleState(outputIndex, 1u, currentMinutes, sunriseMinutesUtc, sunsetMinutesUtc, hasSunTimes, schedule2, waitSun);

    if(!schedule1.Enabled && !schedule2.Enabled)
    {
        return "Auto waiting: disabled schedules";
    }

    if(waitSun)
    {
        return "Waiting for NTP/sun times";
    }

    String summary;
    summary.reserve(160);

    AppendScheduleSlotSummary(summary, 1u, schedule1);
    AppendScheduleSlotSummary(summary, 2u, schedule2);

    if(DoSchedulesOverlap(schedule1, schedule2))
    {
        summary += " | CONFLICT";
    }

    return summary;
}

String GetOutputControlStatus(uint8_t outputIndex)
{
    if(outputIndex >= ConfigModule::OUTPUT_COUNT)
    {
        return "INVALID";
    }

    if(!Config.GetOutputAutomaticMode(outputIndex))
    {
        return "MANUAL";
    }

    if(!IsSystemTimeValid())
    {
        return "AUTO WAIT NTP";
    }

    uint16_t sunriseMinutesUtc = 0;
    uint16_t sunsetMinutesUtc = 0;
    bool hasSunTimes = GetSunriseSunsetUtcMinutes(sunriseMinutesUtc, sunsetMinutesUtc);
    uint16_t currentMinutesUtc = GetCurrentLocalMinutes();
    bool waitSun = false;

    ResolvedScheduleState schedule1;
    ResolvedScheduleState schedule2;
    BuildResolvedScheduleState(outputIndex, 0u, currentMinutesUtc, sunriseMinutesUtc, sunsetMinutesUtc, hasSunTimes, schedule1, waitSun);
    BuildResolvedScheduleState(outputIndex, 1u, currentMinutesUtc, sunriseMinutesUtc, sunsetMinutesUtc, hasSunTimes, schedule2, waitSun);

    if(!schedule1.Enabled && !schedule2.Enabled)
    {
        return "AUTO DISABLED";
    }

    if(waitSun)
    {
        return "AUTO WAIT SUN";
    }

    if(DoSchedulesOverlap(schedule1, schedule2))
    {
        return "AUTO CONFLICT";
    }

    if((schedule1.Valid && schedule1.Active) || (schedule2.Valid && schedule2.Active))
    {
        return "AUTO ACTIVE";
    }

    return "AUTO IDLE";
}

static String FormatScheduleBoundaryReason(uint8_t boundaryType, int16_t offsetMinutes)
{
    const char* base = "FIXED";

    if(boundaryType == 1)
    {
        base = "SUNRISE";
    }
    else if(boundaryType == 2)
    {
        base = "SUNSET";
    }

    if(offsetMinutes == 0)
    {
        return String(base);
    }

    char buffer[24];
    snprintf(buffer, sizeof(buffer), "%s %c %d", base, (offsetMinutes > 0) ? '+' : '-', abs((int)offsetMinutes));
    return String(buffer);
}

static bool GetOutputNextEventData(uint8_t outputIndex, uint16_t& eventMinutes, String& eventReason)
{
    if(outputIndex >= ConfigModule::OUTPUT_COUNT)
    {
        return false;
    }

    if(!Config.GetOutputAutomaticMode(outputIndex))
    {
        eventReason = "MANUAL";
        return false;
    }

    uint16_t sunriseMinutes = 0;
    uint16_t sunsetMinutes = 0;
    bool hasSunTimes = GetSunriseSunsetUtcMinutes(sunriseMinutes, sunsetMinutes);
    uint16_t currentMinutes = GetCurrentLocalMinutes();
    bool hasEnabledSchedule = false;
    bool waitSun = false;
    uint16_t bestDelta = 0xFFFFu;
    uint16_t bestEvent = 0u;
    String bestReason;

    for(uint8_t slot = 0u; slot < ConfigModule::OUTPUT_SCHEDULE_SLOT_COUNT; slot++)
    {
        ResolvedScheduleState state;
        BuildResolvedScheduleState(outputIndex,
                                   slot,
                                   currentMinutes,
                                   sunriseMinutes,
                                   sunsetMinutes,
                                   hasSunTimes,
                                   state,
                                   waitSun);

        if(!state.Enabled)
        {
            continue;
        }

        hasEnabledSchedule = true;
        if(!state.Valid)
        {
            continue;
        }

        uint16_t candidateEvent = state.Active ? state.EndMinutes : state.StartMinutes;
        int16_t candidateOffset = state.Active ? state.EndOffsetMinutes : state.StartOffsetMinutes;
        uint8_t candidateType = state.Active ? state.EndType : state.StartType;
        uint16_t candidateDelta = (uint16_t)((candidateEvent + 1440u - currentMinutes) % 1440u);

        if(bestDelta == 0xFFFFu || candidateDelta < bestDelta)
        {
            bestDelta = candidateDelta;
            bestEvent = candidateEvent;
            bestReason = "S";
            bestReason += String(slot + 1u);
            bestReason += " ";
            bestReason += FormatScheduleBoundaryReason(candidateType, candidateOffset);
        }
    }

    if(!hasEnabledSchedule)
    {
        eventReason = "DISABLED";
        return false;
    }

    if(bestDelta == 0xFFFFu)
    {
        eventReason = waitSun ? "WAIT SUN" : "N/A";
        return false;
    }

    eventMinutes = bestEvent;
    eventReason = bestReason;
    return true;
}

String GetOutputNextEventTimeSummary(uint8_t outputIndex)
{
    if(!IsSystemTimeValid())
    {
        return "Waiting for NTP";
    }

    uint16_t eventMinutes = 0;
    String eventReason;
    if(!GetOutputNextEventData(outputIndex, eventMinutes, eventReason))
    {
        if(eventReason.length() == 0)
        {
            return "N/A";
        }

        return eventReason;
    }

    char buffer[8];
    snprintf(buffer, sizeof(buffer), "%02u:%02u", eventMinutes / 60u, eventMinutes % 60u);
    return String(buffer);
}

String GetOutputNextEventReasonSummary(uint8_t outputIndex)
{
    if(!IsSystemTimeValid())
    {
        return "Waiting for NTP";
    }

    uint16_t eventMinutes = 0;
    String eventReason;
    if(!GetOutputNextEventData(outputIndex, eventMinutes, eventReason))
    {
        if(eventReason.length() == 0)
        {
            return "N/A";
        }

        return eventReason;
    }

    return eventReason;
}

String GetLocalDateTimeSummary()
{
    if(!IsSystemTimeValid())
    {
        return "Waiting for NTP";
    }

    time_t localNow = GetLocalTimeFromConfigOffset();
    struct tm localTimeInfo;
    gmtime_r(&localNow, &localTimeInfo);

    char buffer[32];
    snprintf(buffer,
             sizeof(buffer),
             "%04d-%02d-%02d %02d:%02d:%02d",
             localTimeInfo.tm_year + 1900,
             localTimeInfo.tm_mon + 1,
             localTimeInfo.tm_mday,
             localTimeInfo.tm_hour,
             localTimeInfo.tm_min,
             localTimeInfo.tm_sec);

    return String(buffer);
}

String GetNtpSyncStatusSummary()
{
    if(!IsSystemTimeValid())
    {
        return "Waiting for first sync";
    }

    String summary = "Synced (UTC ";
    summary += String(GetConfiguredUtcOffsetHours(), 1);
    summary += "h)";
    return summary;
}

String GetTodaySunriseSummary()
{
    uint16_t sunriseMinutes = 0;
    uint16_t sunsetMinutes = 0;

    if(!GetSunriseSunsetUtcMinutes(sunriseMinutes, sunsetMinutes))
    {
        return "Waiting for NTP";
    }

    char buffer[16];
    snprintf(buffer, sizeof(buffer), "%02u:%02u", sunriseMinutes / 60u, sunriseMinutes % 60u);
    return String(buffer);
}

String GetTodaySunsetSummary()
{
    uint16_t sunriseMinutes = 0;
    uint16_t sunsetMinutes = 0;

    if(!GetSunriseSunsetUtcMinutes(sunriseMinutes, sunsetMinutes))
    {
        return "Waiting for NTP";
    }

    char buffer[16];
    snprintf(buffer, sizeof(buffer), "%02u:%02u", sunsetMinutes / 60u, sunsetMinutes % 60u);
    return String(buffer);
}
