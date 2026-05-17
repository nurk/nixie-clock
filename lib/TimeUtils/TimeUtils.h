#ifndef TIMEUTILS_H
#define TIMEUTILS_H

#include <stdint.h>

/**
 * Lightweight UTC calendar helpers using int64_t Unix epochs.
 * Bypasses AVR-libc's 32-bit time_t / mktime / gmtime_r entirely,
 * so there is no Year 2038 limitation.
 */

static bool isLeapYear(const int32_t year) {
    return (year % 4 == 0 && year % 100 != 0) || (year % 400 == 0);
}

/**
 * Convert a UTC broken-down date/time to a Unix epoch (int64_t).
 * Pure calendar arithmetic — no timezone conversion.
 */
static int64_t toEpoch(const int16_t year, const uint8_t month,
                       const uint8_t day, const uint8_t hour,
                       const uint8_t minute, const uint8_t second) {
    int64_t days = 0;
    for (int32_t y = 1970; y < year; y++) {
        days += isLeapYear(y) ? 366 : 365;
    }
    static const uint8_t dom[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    for (uint8_t m = 1; m < month; m++) {
        days += dom[m - 1];
        if (m == 2 && isLeapYear(year)) {
            days++;
        }
    }
    days += day - 1;
    return days * 86400LL + hour * 3600LL + minute * 60LL + second;
}

/**
 * Convert a Unix epoch (int64_t) to UTC broken-down h/m/s.
 * Only extracts time-of-day — sufficient for a clock display.
 */
static void toHMS(int64_t epoch,
                  uint8_t& hour, uint8_t& minute, uint8_t& second) {
    second = static_cast<uint8_t>(epoch % 60);
    epoch  /= 60;
    minute = static_cast<uint8_t>(epoch % 60);
    epoch  /= 60;
    hour   = static_cast<uint8_t>(epoch % 24);
}

#endif // TIMEUTILS_H
