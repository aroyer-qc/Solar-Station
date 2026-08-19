#include "SolarFixed.h"
#include <math.h>

struct SolarDayTerms
{
    float equationOfTimeMin;
    float declinationRad;
};

static float Q16ToFloat(int32_t value)
{
    return (float)value / 65536.0f;
}

static int32_t FloatToQ16Degrees(float value)
{
    if(value >= 32767.0f)
    {
        return INT32_MAX;
    }

    if(value <= -32768.0f)
    {
        return INT32_MIN;
    }

    return (int32_t)(value * 65536.0f);
}

static int DayOfYear(int year, int month, int day)
{
    static const uint16_t daysBeforeMonth[12] =
    {
        0u, 31u, 59u, 90u, 120u, 151u, 181u, 212u, 243u, 273u, 304u, 334u
    };

    bool isLeap = ((year % 4) == 0) && (((year % 100) != 0) || ((year % 400) == 0));
    int result = (int)daysBeforeMonth[month - 1] + day;
    if(isLeap && month > 2)
    {
        result += 1;
    }

    return result;
}

static float NormalizeDegrees360(float degrees)
{
    while(degrees < 0.0f)
    {
        degrees += 360.0f;
    }

    while(degrees >= 360.0f)
    {
        degrees -= 360.0f;
    }

    return degrees;
}

static uint16_t NormalizeMinutesFloatToUint16(float minutes)
{
    while(minutes < 0.0f)
    {
        minutes += 1440.0f;
    }

    while(minutes >= 1440.0f)
    {
        minutes -= 1440.0f;
    }

    return (uint16_t)minutes;
}

static bool ComputeSolarDayTerms(int year, int month, int day, struct SolarDayTerms* out)
{
    if(out == nullptr || month < 1 || month > 12 || day < 1 || day > 31)
    {
        return false;
    }

    const float pi = 3.14159265f;
    int dayOfYear = DayOfYear(year, month, day);
    float gamma = 2.0f * pi * ((float)dayOfYear - 1.0f) / 365.0f;

    out->equationOfTimeMin = 229.18f * (0.000075f + 0.001868f * cosf(gamma) - 0.032077f * sinf(gamma)
                                      - 0.014615f * cosf(2.0f * gamma) - 0.040849f * sinf(2.0f * gamma));
    out->declinationRad = 0.006918f - 0.399912f * cosf(gamma) + 0.070257f * sinf(gamma)
                        - 0.006758f * cosf(2.0f * gamma) + 0.000907f * sinf(2.0f * gamma)
                        - 0.002697f * cosf(3.0f * gamma) + 0.001480f * sinf(3.0f * gamma);
    return true;
}

void solar_compute(int year, int month, int day,
                   int hour, int minute, int second,
                   int32_t latQ16, int32_t lonQ16,
                   SolarPosition *out)
{
    if(out == nullptr)
    {
        return;
    }

    const float pi = 3.14159265f;
    const float degToRad = pi / 180.0f;
    const float radToDeg = 180.0f / pi;

    float latitudeDeg = Q16ToFloat(latQ16);
    float longitudeDeg = Q16ToFloat(lonQ16);
    float latitudeRad = latitudeDeg * degToRad;

    int dayOfYear = DayOfYear(year, month, day);
    float minutesUtc = (float)(hour * 60 + minute) + ((float)second / 60.0f);
    float gamma = 2.0f * pi * (((float)dayOfYear - 1.0f) + ((minutesUtc - 720.0f) / 1440.0f)) / 365.0f;

    float equationOfTimeMin = 229.18f * (0.000075f + 0.001868f * cosf(gamma) - 0.032077f * sinf(gamma)
                                       - 0.014615f * cosf(2.0f * gamma) - 0.040849f * sinf(2.0f * gamma));
    float declinationRad = 0.006918f - 0.399912f * cosf(gamma) + 0.070257f * sinf(gamma)
                         - 0.006758f * cosf(2.0f * gamma) + 0.000907f * sinf(2.0f * gamma)
                         - 0.002697f * cosf(3.0f * gamma) + 0.001480f * sinf(3.0f * gamma);

    float trueSolarTimeMin = minutesUtc + equationOfTimeMin + (4.0f * longitudeDeg);
    while(trueSolarTimeMin < 0.0f) { trueSolarTimeMin += 1440.0f; }
    while(trueSolarTimeMin >= 1440.0f) { trueSolarTimeMin -= 1440.0f; }

    float hourAngleDeg = (trueSolarTimeMin / 4.0f) - 180.0f;
    float hourAngleRad = hourAngleDeg * degToRad;

    float sinLat = sinf(latitudeRad);
    float cosLat = cosf(latitudeRad);
    float sinDecl = sinf(declinationRad);
    float cosDecl = cosf(declinationRad);

    float sinElevation = (sinLat * sinDecl) + (cosLat * cosDecl * cosf(hourAngleRad));
    if(sinElevation > 1.0f) { sinElevation = 1.0f; }
    if(sinElevation < -1.0f) { sinElevation = -1.0f; }

    float elevationDeg = asinf(sinElevation) * radToDeg;

    float azimuthDeg = atan2f(sinf(hourAngleRad), (cosf(hourAngleRad) * sinLat) - (tanf(declinationRad) * cosLat)) * radToDeg + 180.0f;
    azimuthDeg = NormalizeDegrees360(azimuthDeg);

    out->azimuth = FloatToQ16Degrees(azimuthDeg);
    out->elevation = FloatToQ16Degrees(elevationDeg);
}

bool solar_compute_sunrise_sunset_utc(int year, int month, int day,
                                      int32_t latitudeQ16, int32_t longitudeQ16,
                                      uint16_t *sunriseMinutesUtc,
                                      uint16_t *sunsetMinutesUtc)
{
    if(sunriseMinutesUtc == nullptr || sunsetMinutesUtc == nullptr)
    {
        return false;
    }

    struct SolarDayTerms terms;
    if(!ComputeSolarDayTerms(year, month, day, &terms))
    {
        return false;
    }

    const float pi = 3.14159265f;
    const float radToDeg = 180.0f / pi;
    const float degToRad = pi / 180.0f;
    const float solarZenithRad = 90.833f * degToRad;

    float latitudeDeg = Q16ToFloat(latitudeQ16);
    float longitudeDeg = Q16ToFloat(longitudeQ16);
    float latitudeRad = latitudeDeg * degToRad;

    float cosLat = cosf(latitudeRad);
    if(fabsf(cosLat) < 1.0e-6f)
    {
        return false;
    }

    float cosHourAngle = (cosf(solarZenithRad) / (cosLat * cosf(terms.declinationRad)))
                       - (tanf(latitudeRad) * tanf(terms.declinationRad));

    if(cosHourAngle < -1.0f || cosHourAngle > 1.0f)
    {
        return false;
    }

    float hourAngleDeg = acosf(cosHourAngle) * radToDeg;
    float solarNoonUtcMin = 720.0f - (4.0f * longitudeDeg) - terms.equationOfTimeMin;
    float sunriseUtcMin = solarNoonUtcMin - (4.0f * hourAngleDeg);
    float sunsetUtcMin = solarNoonUtcMin + (4.0f * hourAngleDeg);

    *sunriseMinutesUtc = NormalizeMinutesFloatToUint16(sunriseUtcMin);
    *sunsetMinutesUtc = NormalizeMinutesFloatToUint16(sunsetUtcMin);
    return true;
}