/**
 * PosixTimeZoneInfo — UTC to local time conversion using TZif (IANA tzdata) files.
 *
 * Supports TZif v2/v3 files stored in PROGMEM. For timestamps beyond the last
 * explicit transition, the POSIX TZ footer rule (e.g. CET-1CEST,M3.5.0,M10.5.0/3)
 * is parsed and evaluated, giving correct DST behaviour indefinitely.
 *
 * https://github.com/rstephan/TimeZoneInfo
 *
 * This is an adaptation of TimeZoneInfo to handle POSIX TZ footers,
 * which are needed for correct DST handling beyond the last
 * TZif transition (2037-10-25 for tzdata 2024a).
 * The code is mostly shared between the two classes,
 * but kept separate for clarity and to avoid unnecessary code size increase for
 * users who don't need POSIX footer support.
 *
 * It is mostly backwards compatible.
 *
 */

#include "PosixTimeZoneInfo.h"

PosixTimeZoneInfo::PosixTimeZoneInfo() = default;

void PosixTimeZoneInfo::setLocation_P(const uint8_t* tzFile) {
    mTzFile   = const_cast<uint8_t*>(tzFile);
    mCharPos  = 0;
    mCharLen  = 0;
    mHasPosix = false;
    memset(&mTimeInfo, 0, sizeof(mTimeInfo));
    parsePosixFooter();
}

int64_t PosixTimeZoneInfo::utc2local(const int64_t utc) {
    const int64_t offset = findTimeInfo(utc);
    return utc + offset;
}

int64_t PosixTimeZoneInfo::local2utc(const int64_t local) {
    const int64_t offs = findTimeInfo(local);
    return local - offs;
}

// e.g. "CEST" / "CET" — returned pointer is valid until the next call
const char* PosixTimeZoneInfo::getShortName() {
    static char buf[8]; // longest TZ abbreviations are 5 chars + NUL
    uint32_t pos = mTimeInfo.ttAbbrInd;
    uint8_t i    = 0;

    if (mCharPos) {
        while (pos < mCharLen && i < sizeof(buf) - 1) {
            const uint8_t c = read8(mCharPos + pos);
            if (c == 0) {
                break;
            }
            buf[i++] = static_cast<char>(c);
            pos++;
        }
    }
    buf[i] = '\0';
    return buf;
}

bool PosixTimeZoneInfo::isDst() const {
    return mTimeInfo.ttIsDst != 0;
}

uint32_t PosixTimeZoneInfo::read32(const unsigned long pos) {
    uint32_t val = 0;
    val          += static_cast<uint32_t>(pgm_read_byte(&mTzFile[pos + 0])) << 24;
    val          += static_cast<uint32_t>(pgm_read_byte(&mTzFile[pos + 1])) << 16;
    val          += static_cast<uint32_t>(pgm_read_byte(&mTzFile[pos + 2])) << 8;
    val          += static_cast<uint32_t>(pgm_read_byte(&mTzFile[pos + 3]));
    return val;
}

uint8_t PosixTimeZoneInfo::read8(const unsigned long pos) {
    return pgm_read_byte(&mTzFile[pos]);
}

// ── POSIX TZ footer ──────────────────────────────────────────────────────────

bool PosixTimeZoneInfo::isLeap(const int64_t year) {
    return (year % 4 == 0 && year % 100 != 0) || (year % 400 == 0);
}

int64_t PosixTimeZoneInfo::daysInMonth(const int64_t year, const int8_t month) {
    static const int8_t dom[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    auto d                    = static_cast<int64_t>(static_cast<uint8_t>(dom[month - 1]));
    if (month == 2 && isLeap(year)) {
        d = 29;
    }
    return d;
}

/**
 * Given a year and an Mm.w.d rule, return the UTC unix timestamp of that
 * transition. wallOffset is the UTC offset in effect at transition time
 * (stdOffset for DST-start, dstOffset for DST-end), per POSIX.
 */
int64_t PosixTimeZoneInfo::transitionUtc(const int64_t year, const PosixRule& rule, const int64_t wallOffset) {
    // Day-of-week of Jan 1 for year. 0=Sun..6=Sat. Jan 1 1970 was Thursday (4).
    auto dowJan1 = [](const int64_t y) -> int8_t {
        const int64_t yr = y - 1970;
        int64_t leaps    = 0;
        for (int64_t i = 1970; i < y; i++) {
            if (isLeap(i)) {
                leaps++;
            }
        }
        return static_cast<int8_t>((4 + yr * 365 + leaps) % 7);
    };

    // Day-of-week of the 1st of rule.month
    const int8_t dow1 = static_cast<int8_t>((dowJan1(year) + [&]() -> int64_t {
        int64_t d = 0;
        for (int8_t m = 1; m < rule.month; m++) {
            d += daysInMonth(year, m);
        }
        return d;
    }()) % 7);

    // Day-of-month (1-based) of the first occurrence of rule.day in rule.month
    const auto diff = static_cast<int8_t>((rule.day - dow1 + 7) % 7);
    auto dom        = static_cast<int8_t>(1 + diff);

    if (rule.week == 5) {
        // Last occurrence — advance as far as possible while still in month
        const auto maxDom = static_cast<int8_t>(daysInMonth(year, rule.month));
        while (dom + 7 <= maxDom) {
            dom += 7;
        }
    } else {
        // Week 1 = first, 2 = second, etc.
        dom = static_cast<int8_t>(dom + (rule.week - 1) * 7);
    }

    // Days from epoch (1970-01-01) to this date
    int64_t days = 0;
    for (int64_t y = 1970; y < year; y++) {
        days += isLeap(y) ? 366 : 365;
    }
    for (int8_t m = 1; m < rule.month; m++) {
        days += daysInMonth(year, m);
    }
    days += dom - 1;

    // rule.time is wall-clock seconds from midnight; subtract wallOffset to get UTC
    return days * 86400LL + rule.time - wallOffset;
}

/**
 * Parse an integer from s starting at pos; advance pos past it.
 * Returns true if at least one digit was consumed.
 */
static bool parseInt(const char* s, int& pos, int32_t& out) {
    bool neg = false;
    if (s[pos] == '-') {
        neg = true;
        pos++;
    } else if (s[pos] == '+') {
        pos++;
    }
    if (!isdigit(static_cast<unsigned char>(s[pos]))) {
        return false;
    }
    out = 0;
    while (isdigit(static_cast<unsigned char>(s[pos]))) {
        out = out * 10 + (s[pos] - '0');
        pos++;
    }
    if (neg) {
        out = -out;
    }
    return true;
}

/**
 * Skip a timezone name (letters, or <...> quoted). Advance pos.
 */
static void skipName(const char* s, int& pos) {
    if (s[pos] == '<') {
        pos++;
        while (s[pos] && s[pos] != '>') {
            pos++;
        }
        if (s[pos] == '>') {
            pos++;
        }
    } else {
        while (isalpha(static_cast<unsigned char>(s[pos]))) {
            pos++;
        }
    }
}

/**
 * Parse offset hh[:mm[:ss]] — POSIX sign convention: west-positive.
 * Returns seconds-east-of-UTC.
 */
static bool parseOffset(const char* s, int& pos, int64_t& offsetEast) {
    int32_t h = 0;
    if (!parseInt(s, pos, h)) {
        return false;
    }
    int32_t m = 0, sec = 0;
    if (s[pos] == ':') {
        pos++;
        parseInt(s, pos, m);
    }
    if (s[pos] == ':') {
        pos++;
        parseInt(s, pos, sec);
    }
    // POSIX: positive = west; we store east
    offsetEast = -(h * 3600LL + m * 60LL + sec);
    return true;
}

/**
 * Parse an Mm.w.d[/time] rule.
 */
bool PosixTimeZoneInfo::parsePosixRule(const char* s, int& pos, PosixRule& rule) {
    rule.time = 2 * 3600LL; // default 02:00
    if (s[pos] != 'M') {
        return false;
    }
    pos++;
    int32_t m = 0, w = 0, d = 0;
    if (!parseInt(s, pos, m)) {
        return false;
    }
    if (s[pos] != '.') {
        return false;
    }
    pos++;
    if (!parseInt(s, pos, w)) {
        return false;
    }
    if (s[pos] != '.') {
        return false;
    }
    pos++;
    if (!parseInt(s, pos, d)) {
        return false;
    }
    rule.month = static_cast<int8_t>(m);
    rule.week  = static_cast<int8_t>(w);
    rule.day   = static_cast<int8_t>(d);
    if (s[pos] == '/') {
        pos++;
        int32_t th = 0;
        parseInt(s, pos, th);
        int32_t tm_ = 0, ts = 0;
        if (s[pos] == ':') {
            pos++;
            parseInt(s, pos, tm_);
        }
        if (s[pos] == ':') {
            pos++;
            parseInt(s, pos, ts);
        }
        rule.time = th * 3600LL + tm_ * 60LL + ts;
    }
    return true;
}

/**
 * Locate and parse the POSIX TZ footer from the TZif v2/v3 file.
 * Format (after the v2 data block): \nPOSIX_STRING\n
 *
 * Example: CET-1CEST,M3.5.0,M10.5.0/3
 */
void PosixTimeZoneInfo::parsePosixFooter() {
    if (!mTzFile) {
        return;
    }
    // File must be TZif v2 or v3
    if (read8(4) != '2' && read8(4) != '3') {
        return;
    }

    // Locate the v2 section by scanning for the second "TZif" magic after byte 44.
    // This avoids having to exactly compute the v1 block size.
    unsigned long v2Start = 0;
    for (unsigned long i = 44; i < 16384; i++) {
        if (read8(i) == 'T' && read8(i + 1) == 'Z' &&
            read8(i + 2) == 'i' && read8(i + 3) == 'f') {
            v2Start = i;
            break;
        }
    }
    if (v2Start == 0) {
        return;
    }

    // Read v2 header counts (RFC 8536)
    const uint32_t isgmtCnt = read32(v2Start + 20);
    const uint32_t isstdCnt = read32(v2Start + 24);
    const uint32_t lc2      = read32(v2Start + 28);
    const uint32_t tc2      = read32(v2Start + 32);
    const uint32_t typ2     = read32(v2Start + 36);
    const uint32_t ch2      = read32(v2Start + 40);

    // v2 data block size (bytes after the 44-byte header):
    //   timecnt * 8  (64-bit transition times)
    //   timecnt * 1  (type indices)
    //   ttypecnt * 6 (ttinfo structs)
    //   charcnt      (timezone abbreviations)
    //   leapcnt * 12 (64-bit leap second records)
    //   isstdcnt     (standard/wall indicators)
    //   isgmtcnt     (UT/local indicators)
    const unsigned long v2DataSize = tc2 * 8UL + tc2 + typ2 * 6UL + ch2 + lc2 * 12UL + isstdCnt + isgmtCnt;
    unsigned long footerStart      = v2Start + 44 + v2DataSize;

    // Footer format: \nPOSIX_STRING\n — skip the leading \n
    if (read8(footerStart) != '\n') {
        return;
    }
    footerStart++;

    // Read the POSIX string into a local buffer (max 64 chars)
    char buf[64];
    uint8_t len = 0;
    while (len < 63) {
        const uint8_t c = read8(footerStart + len);
        if (c == '\n' || c == 0) {
            break;
        }
        buf[len++] = static_cast<char>(c);
    }
    buf[len] = '\0';
    if (len == 0) {
        return;
    }

    // Parse: stdName stdOffset [dstName [dstOffset] , rule , rule]
    int pos = 0;
    skipName(buf, pos); // skip std name
    if (!parseOffset(buf, pos, mStdOffset)) {
        return;
    }

    if (buf[pos] == '\0') {
        // No DST — simple fixed offset
        mDstOffset = mStdOffset;
        mHasPosix  = true;
        return;
    }

    skipName(buf, pos); // skip dst name
    if (buf[pos] != '\0' && buf[pos] != ',') {
        parseOffset(buf, pos, mDstOffset); // explicit dst offset
    } else {
        mDstOffset = mStdOffset + 3600LL; // default: stdOffset + 1h
    }

    if (buf[pos] != ',') {
        mHasPosix = true;
        return;
    }
    pos++;
    if (!parsePosixRule(buf, pos, mDstStart)) {
        return;
    }
    if (buf[pos] != ',') {
        return;
    }
    pos++;
    if (!parsePosixRule(buf, pos, mDstEnd)) {
        return;
    }

    mHasPosix = true;
}

/**
 * Given a UTC timestamp beyond the last TZif transition, determine the
 * applicable offset using the POSIX TZ footer rule.
 */
int64_t PosixTimeZoneInfo::posixOffset(const int64_t utc) const {
    if (mDstOffset == mStdOffset) {
        return mStdOffset; // no DST
    }

    // Determine the year from utc by subtracting year lengths
    int64_t year      = 1970;
    int64_t remaining = utc;
    while (true) {
        const int64_t ylen = (isLeap(year) ? 366LL : 365LL) * 86400LL;
        if (remaining < ylen) {
            break;
        }
        remaining -= ylen;
        year++;
    }

    // DST-start rule time is in standard wall-clock time; DST-end is in DST wall-clock time
    const int64_t startUtc = transitionUtc(year, mDstStart, mStdOffset);
    const int64_t endUtc   = transitionUtc(year, mDstEnd, mDstOffset);

    bool inDst;
    if (startUtc < endUtc) {
        // Northern hemisphere: DST is between start and end
        inDst = (utc >= startUtc && utc < endUtc);
    } else {
        // Southern hemisphere: DST is outside start..end
        inDst = (utc >= startUtc || utc < endUtc);
    }

    if (inDst) {
        mTimeInfo.ttIsDst = 1;
        return mDstOffset;
    } else {
        mTimeInfo.ttIsDst = 0;
        return mStdOffset;
    }
}

// ─────────────────────────────────────────────────────────────────────────────

int64_t PosixTimeZoneInfo::findTimeInfo(const int64_t t) {
    bool found = false;

    constexpr uint32_t TZIF_MAGIC = 0x545a6966; // ASCII "TZif"
    const uint32_t magic          = read32(0);
    if (magic != TZIF_MAGIC) {
        return 0;
    }
    const uint32_t leapCount = read32(28);
    const uint32_t timeCount = read32(32);
    const uint32_t typeCount = read32(36);
    const uint32_t charCount = read32(40);

    uint32_t i          = 0;
    uint32_t valTimeOld = read32(44);
    for (i = 1; i < timeCount; i++) {
        const uint32_t valTime = read32(44 + (i * 4));
        // v1 transition times are signed 32-bit; cast via int32_t before widening to int64_t
        if (t >= static_cast<int64_t>(static_cast<int32_t>(valTimeOld)) &&
            t < static_cast<int64_t>(static_cast<int32_t>(valTime))) {
            found = true;
            i--;
            break;
        }
        valTimeOld = valTime;
    }
    if (found) {
        const uint32_t valIndex = read8(44L + (timeCount * 4) + i);
        if (valIndex < typeCount) {
            const unsigned long pos = 44L + (timeCount * 4) + timeCount + (sizeof(ttInfo) * valIndex);
            mTimeInfo.ttGmtOffset   = static_cast<int32_t>(read32(pos));
            mTimeInfo.ttIsDst       = static_cast<int8_t>(read8(pos + 4));
            mTimeInfo.ttAbbrInd     = read8(pos + 5);
            mCharPos                = 44L + (timeCount * 5) + (sizeof(ttInfo) * typeCount) + (8 * leapCount);
            mCharLen                = charCount;
        }
    } else if (mHasPosix) {
        // Timestamp is beyond the last TZif transition — use POSIX TZ footer rule
        return posixOffset(t);
    }

    return mTimeInfo.ttGmtOffset;
}
