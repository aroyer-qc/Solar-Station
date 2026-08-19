#ifndef SOLAR_FIXED_H
#define SOLAR_FIXED_H

#include <stdint.h>

#define Q16(x) ((int32_t)((x) * 65536.0f))

typedef struct
{
    int32_t azimuth;     // Q16.16 degrees
    int32_t elevation;   // Q16.16 degrees
} SolarPosition;

// Computes approximate solar azimuth/elevation from UTC date-time and lat/lon in degrees Q16.16.
// Output azimuth uses north=0, east=90, south=180, west=270.
void solar_compute(int year, int month, int day,
                   int hour, int minute, int second,
                   int32_t latitudeQ16, int32_t longitudeQ16,
                   SolarPosition *out);

// Computes sunrise and sunset minutes in UTC for the supplied civil date and location.
// Returns false for polar day/night or invalid inputs.
bool solar_compute_sunrise_sunset_utc(int year, int month, int day,
                                      int32_t latitudeQ16, int32_t longitudeQ16,
                                      uint16_t *sunriseMinutesUtc,
                                      uint16_t *sunsetMinutesUtc);

#endif