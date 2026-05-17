/**
 * Unit tests for PosixTimeZoneInfo — runs natively on host via PlatformIO native env.
 *
 * Expected offsets verified against Python zoneinfo / IANA tzdata:
 *   CET  = UTC+3600  (standard, winter)
 *   CEST = UTC+7200  (summer DST)
 *
 * Run with:  pio test -e native_test
 */

#include <unity.h>
#include "PosixTimeZoneInfo.h"
#include "TimeUtils.h"
#include "Brussels.h"

static PosixTimeZoneInfo tz;

void setUp() {
    tz.setLocation_P(Brussels);
}

void tearDown() {
}

// ── helpers ──────────────────────────────────────────────────────────────────

// Thin wrapper so test call-sites stay readable
static int64_t makeUtc(int year, int month, int day, int hour, int min, int sec) {
    return toEpoch(
        static_cast<int16_t>(year), static_cast<uint8_t>(month),
        static_cast<uint8_t>(day), static_cast<uint8_t>(hour),
        static_cast<uint8_t>(min), static_cast<uint8_t>(sec)
    );
}

// ── TZif v1 range: pre-2038 timestamps handled by stored transitions ──────────

void test_winter_2026_is_CET() {
    // 2026-01-15 12:00:00 UTC → CET (UTC+1)
    const int64_t utc = makeUtc(2026, 1, 15, 12, 0, 0);
    TEST_ASSERT_EQUAL_INT64(utc + 3600LL, tz.utc2local(utc));
    TEST_ASSERT_FALSE(tz.isDst());
}

void test_summer_2026_is_CEST() {
    // 2026-07-15 12:00:00 UTC → CEST (UTC+2)
    const int64_t utc = makeUtc(2026, 7, 15, 12, 0, 0);
    TEST_ASSERT_EQUAL_INT64(utc + 7200LL, tz.utc2local(utc));
    TEST_ASSERT_TRUE(tz.isDst());
}

void test_dst_start_2026_boundary() {
    // 2026 DST start: last Sunday March = 2026-03-29, 01:00:00 UTC
    // 00:59:59 UTC → CET
    const int64_t before = makeUtc(2026, 3, 29, 0, 59, 59);
    TEST_ASSERT_EQUAL_INT64(before + 3600LL, tz.utc2local(before));
    TEST_ASSERT_FALSE(tz.isDst());
    // 01:00:01 UTC → CEST
    const int64_t after = makeUtc(2026, 3, 29, 1, 0, 1);
    TEST_ASSERT_EQUAL_INT64(after + 7200LL, tz.utc2local(after));
    TEST_ASSERT_TRUE(tz.isDst());
}

void test_dst_end_2026_boundary() {
    // 2026 DST end: last Sunday October = 2026-10-25, 01:00:00 UTC
    // 00:59:59 UTC → CEST
    const int64_t before = makeUtc(2026, 10, 25, 0, 59, 59);
    TEST_ASSERT_EQUAL_INT64(before + 7200LL, tz.utc2local(before));
    TEST_ASSERT_TRUE(tz.isDst());
    // 01:00:01 UTC → CET
    const int64_t after = makeUtc(2026, 10, 25, 1, 0, 1);
    TEST_ASSERT_EQUAL_INT64(after + 3600LL, tz.utc2local(after));
    TEST_ASSERT_FALSE(tz.isDst());
}

void test_last_tzif_transition_2037() {
    // Last explicit TZif transition ends DST: 2037-10-25 01:00:00 UTC
    // Just before: CEST
    const int64_t before = makeUtc(2037, 10, 25, 0, 59, 59);
    TEST_ASSERT_EQUAL_INT64(before + 7200LL, tz.utc2local(before));
    // Just after: CET (still within TZif range)
    const int64_t after = makeUtc(2037, 10, 25, 1, 0, 1);
    TEST_ASSERT_EQUAL_INT64(after + 3600LL, tz.utc2local(after));
}

// ── POSIX footer range: post-2037 timestamps handled by rule evaluation ───────

void test_winter_2038_is_CET() {
    const int64_t utc = makeUtc(2038, 1, 15, 12, 0, 0);
    TEST_ASSERT_EQUAL_INT64(utc + 3600LL, tz.utc2local(utc));
    TEST_ASSERT_FALSE(tz.isDst());
}

void test_summer_2038_is_CEST() {
    const int64_t utc = makeUtc(2038, 7, 15, 12, 0, 0);
    TEST_ASSERT_EQUAL_INT64(utc + 7200LL, tz.utc2local(utc));
    TEST_ASSERT_TRUE(tz.isDst());
}

void test_dst_start_2038_boundary() {
    // 2038 DST start: last Sunday March = 2038-03-28, 01:00:00 UTC
    const int64_t before = makeUtc(2038, 3, 28, 0, 59, 59);
    TEST_ASSERT_EQUAL_INT64(before + 3600LL, tz.utc2local(before));
    TEST_ASSERT_FALSE(tz.isDst());
    const int64_t after = makeUtc(2038, 3, 28, 1, 0, 1);
    TEST_ASSERT_EQUAL_INT64(after + 7200LL, tz.utc2local(after));
    TEST_ASSERT_TRUE(tz.isDst());
}

void test_dst_end_2038_boundary() {
    // 2038 DST end: last Sunday October = 2038-10-31, 01:00:00 UTC
    const int64_t before = makeUtc(2038, 10, 31, 0, 59, 59);
    TEST_ASSERT_EQUAL_INT64(before + 7200LL, tz.utc2local(before));
    TEST_ASSERT_TRUE(tz.isDst());
    const int64_t after = makeUtc(2038, 10, 31, 1, 0, 1);
    TEST_ASSERT_EQUAL_INT64(after + 3600LL, tz.utc2local(after));
    TEST_ASSERT_FALSE(tz.isDst());
}

void test_winter_2050_is_CET() {
    const int64_t utc = makeUtc(2050, 1, 15, 12, 0, 0);
    TEST_ASSERT_EQUAL_INT64(utc + 3600LL, tz.utc2local(utc));
    TEST_ASSERT_FALSE(tz.isDst());
}

void test_summer_2050_is_CEST() {
    const int64_t utc = makeUtc(2050, 7, 15, 12, 0, 0);
    TEST_ASSERT_EQUAL_INT64(utc + 7200LL, tz.utc2local(utc));
    TEST_ASSERT_TRUE(tz.isDst());
}

void test_dst_start_2050_boundary() {
    // 2050 DST start: last Sunday March = 2050-03-27, 01:00:00 UTC
    const int64_t before = makeUtc(2050, 3, 27, 0, 59, 59);
    TEST_ASSERT_EQUAL_INT64(before + 3600LL, tz.utc2local(before));
    TEST_ASSERT_FALSE(tz.isDst());
    const int64_t after = makeUtc(2050, 3, 27, 1, 0, 1);
    TEST_ASSERT_EQUAL_INT64(after + 7200LL, tz.utc2local(after));
    TEST_ASSERT_TRUE(tz.isDst());
}

void test_dst_end_2050_boundary() {
    // 2050 DST end: last Sunday October = 2050-10-30, 01:00:00 UTC
    const int64_t before = makeUtc(2050, 10, 30, 0, 59, 59);
    TEST_ASSERT_EQUAL_INT64(before + 7200LL, tz.utc2local(before));
    TEST_ASSERT_TRUE(tz.isDst());
    const int64_t after = makeUtc(2050, 10, 30, 1, 0, 1);
    TEST_ASSERT_EQUAL_INT64(after + 3600LL, tz.utc2local(after));
    TEST_ASSERT_FALSE(tz.isDst());
}

void test_winter_2100_is_CET() {
    const int64_t utc = makeUtc(2100, 1, 15, 12, 0, 0);
    TEST_ASSERT_EQUAL_INT64(utc + 3600LL, tz.utc2local(utc));
    TEST_ASSERT_FALSE(tz.isDst());
}

void test_summer_2100_is_CEST() {
    const int64_t utc = makeUtc(2100, 7, 15, 12, 0, 0);
    TEST_ASSERT_EQUAL_INT64(utc + 7200LL, tz.utc2local(utc));
    TEST_ASSERT_TRUE(tz.isDst());
}

void test_dst_start_2100_boundary() {
    // 2100 DST start: last Sunday March = 2100-03-28, 01:00:00 UTC
    const int64_t before = makeUtc(2100, 3, 28, 0, 59, 59);
    TEST_ASSERT_EQUAL_INT64(before + 3600LL, tz.utc2local(before));
    TEST_ASSERT_FALSE(tz.isDst());
    const int64_t after = makeUtc(2100, 3, 28, 1, 0, 1);
    TEST_ASSERT_EQUAL_INT64(after + 7200LL, tz.utc2local(after));
    TEST_ASSERT_TRUE(tz.isDst());
}

void test_dst_end_2100_boundary() {
    // 2100 DST end: last Sunday October = 2100-10-31, 01:00:00 UTC
    const int64_t before = makeUtc(2100, 10, 31, 0, 59, 59);
    TEST_ASSERT_EQUAL_INT64(before + 7200LL, tz.utc2local(before));
    TEST_ASSERT_TRUE(tz.isDst());
    const int64_t after = makeUtc(2100, 10, 31, 1, 0, 1);
    TEST_ASSERT_EQUAL_INT64(after + 3600LL, tz.utc2local(after));
    TEST_ASSERT_FALSE(tz.isDst());
}

// ── local2utc round-trip ──────────────────────────────────────────────────────

void test_local2utc_roundtrip_winter() {
    const int64_t utc   = makeUtc(2040, 1, 15, 12, 0, 0);
    const int64_t local = tz.utc2local(utc);
    TEST_ASSERT_EQUAL_INT64(utc, tz.local2utc(local));
}

void test_local2utc_roundtrip_summer() {
    const int64_t utc   = makeUtc(2040, 7, 15, 12, 0, 0);
    const int64_t local = tz.utc2local(utc);
    TEST_ASSERT_EQUAL_INT64(utc, tz.local2utc(local));
}

// ── exact transition instant ──────────────────────────────────────────────────

void test_exact_dst_start_instant() {
    // At exactly 01:00:00 UTC the transition fires → CEST
    const int64_t t = makeUtc(2045, 3, 26, 1, 0, 0); // 2045 last Sun Mar
    TEST_ASSERT_EQUAL_INT64(t + 7200LL, tz.utc2local(t));
    TEST_ASSERT_TRUE(tz.isDst());
}

void test_exact_dst_end_instant() {
    // At exactly 01:00:00 UTC the transition fires → CET
    const int64_t t = makeUtc(2045, 10, 29, 1, 0, 0); // 2045 last Sun Oct
    TEST_ASSERT_EQUAL_INT64(t + 3600LL, tz.utc2local(t));
    TEST_ASSERT_FALSE(tz.isDst());
}

// ── main ─────────────────────────────────────────────────────────────────────

int main(int argc, char** argv) {
    UNITY_BEGIN();

    // TZif range
    RUN_TEST(test_winter_2026_is_CET);
    RUN_TEST(test_summer_2026_is_CEST);
    RUN_TEST(test_dst_start_2026_boundary);
    RUN_TEST(test_dst_end_2026_boundary);
    RUN_TEST(test_last_tzif_transition_2037);

    // POSIX footer range
    RUN_TEST(test_winter_2038_is_CET);
    RUN_TEST(test_summer_2038_is_CEST);
    RUN_TEST(test_dst_start_2038_boundary);
    RUN_TEST(test_dst_end_2038_boundary);
    RUN_TEST(test_winter_2050_is_CET);
    RUN_TEST(test_summer_2050_is_CEST);
    RUN_TEST(test_dst_start_2050_boundary);
    RUN_TEST(test_dst_end_2050_boundary);
    RUN_TEST(test_winter_2100_is_CET);
    RUN_TEST(test_summer_2100_is_CEST);
    RUN_TEST(test_dst_start_2100_boundary);
    RUN_TEST(test_dst_end_2100_boundary);

    // Round-trip
    RUN_TEST(test_local2utc_roundtrip_winter);
    RUN_TEST(test_local2utc_roundtrip_summer);

    // Exact instants
    RUN_TEST(test_exact_dst_start_instant);
    RUN_TEST(test_exact_dst_end_instant);

    return UNITY_END();
}
