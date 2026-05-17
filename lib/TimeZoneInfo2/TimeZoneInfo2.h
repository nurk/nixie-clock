#ifndef TIMEZONEINFO2_H
#define TIMEZONEINFO2_H

#include <Arduino.h>

// https://github.com/rstephan/TimeZoneInfo

struct ttInfo {
    int32_t ttGmtOffset;
    int8_t ttIsDst;
    uint8_t ttAbbrInd;
} __attribute__((packed)); // TZif stores these as exactly 6 bytes — no padding

// Parsed Mm.w.d DST transition rule
struct PosixRule {
    int8_t month; // 1–12
    int8_t week; // 1–5  (5 = last)
    int8_t day; // 0–6  (0 = Sunday)
    int64_t time; // seconds from midnight (default 2*3600)
};

class TimeZoneInfo2 {
public:
    TimeZoneInfo2();
    void setLocation_P(const byte* tzFile);
    int64_t utc2local(int64_t utc);
    int64_t local2utc(int64_t local);
    String getShortName(); // e.g. CEST/CET
    boolean isDst() const;

private:
    int64_t findTimeInfo(const int64_t t);
    uint32_t read32(unsigned long pos);
    uint8_t read8(unsigned long pos);

    // POSIX TZ footer support
    void parsePosixFooter();
    static bool parsePosixRule(const char* s, int& pos, PosixRule& rule);
    int64_t posixOffset(int64_t utc) const;
    static int64_t transitionUtc(int64_t year, const PosixRule& rule, int64_t stdOffset);
    static int64_t daysInMonth(int64_t year, int8_t month);
    static bool isLeap(int64_t year);

    byte* mTzFile            = nullptr;
    mutable ttInfo mTimeInfo = {0, 0, 0}; // mutable: updated by const posixOffset()
    unsigned long mCharPos   = 0;
    uint32_t mCharLen        = 0;

    // POSIX footer
    bool mHasPosix      = false;
    int64_t mStdOffset  = 0; // seconds east of UTC for standard time
    int64_t mDstOffset  = 0; // seconds east of UTC for DST
    PosixRule mDstStart = {};
    PosixRule mDstEnd   = {};
};

#endif
