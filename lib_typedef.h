#pragma once

struct Time_t
{
    uint8_t     Second;
    uint8_t     Minute;
    uint8_t     Hour;
};

 struct Date_t
{
    uint8_t     Day;
    uint8_t     Month;
    uint16_t    Year;
} ;


struct DateAndTime_t
{
    Date_t      Date;
    Time_t      Time;
};