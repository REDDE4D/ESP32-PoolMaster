#include <unity.h>
#include <cstring>
#include <initializer_list>
#include "PresetsLogic.h"

using namespace Presets;

static PresetData makeManual(std::initializer_list<Window> ws) {
    PresetData d{};
    std::strcpy(d.name, "test");
    d.type = Type::Manual;
    int i = 0;
    for (auto& w : ws) { if (i < WINDOWS_PER) d.windows[i++] = w; }
    while (i < WINDOWS_PER) d.windows[i++] = {0, 0, false};
    return d;
}

void test_no_enabled_windows_returns_false(void) {
    auto d = makeManual({});
    TEST_ASSERT_FALSE(isInActiveWindow(d, 600));
}

void test_inside_single_enabled_window_returns_true(void) {
    auto d = makeManual({ { 480, 1080, true } });   // 08:00 - 18:00
    TEST_ASSERT_TRUE(isInActiveWindow(d, 600));     // 10:00
}

void test_at_window_start_returns_true(void) {
    auto d = makeManual({ { 480, 1080, true } });
    TEST_ASSERT_TRUE(isInActiveWindow(d, 480));
}

void test_at_window_end_returns_false(void) {
    // half-open [start, end) -- end is exclusive
    auto d = makeManual({ { 480, 1080, true } });
    TEST_ASSERT_FALSE(isInActiveWindow(d, 1080));
}

void test_disabled_window_does_not_match(void) {
    auto d = makeManual({ { 480, 1080, false } });
    TEST_ASSERT_FALSE(isInActiveWindow(d, 600));
}

void test_multiple_windows_any_match_returns_true(void) {
    auto d = makeManual({
        { 360, 540,  true },  // 06:00 - 09:00
        { 720, 990,  true },  // 12:00 - 16:30
        { 0,   0,    false },
        { 1080, 1260, true }, // 18:00 - 21:00
    });
    TEST_ASSERT_TRUE(isInActiveWindow(d, 800));
    TEST_ASSERT_TRUE(isInActiveWindow(d, 1200));
    TEST_ASSERT_FALSE(isInActiveWindow(d, 1000));
}

void test_edges_at_midnight_boundaries(void) {
    auto d = makeManual({ { 0, 1, true } });
    TEST_ASSERT_TRUE(isInActiveWindow(d, 0));
    TEST_ASSERT_FALSE(isInActiveWindow(d, 1));
    TEST_ASSERT_FALSE(isInActiveWindow(d, 1439));
}

void test_autotemp_cold_uses_2h_duration(void) {
    // tempValue < lowThreshold → fixed 2h duration centered on centerHour
    Window w = computeAutoTempWindow(
        /*temp*/ 5.0, /*low*/ 10.0, /*setpoint*/ 27.0,
        /*center*/ 15, /*startMin*/ 8, /*stopMax*/ 22);
    TEST_ASSERT_TRUE(w.enabled);
    TEST_ASSERT_EQUAL_UINT16(14 * 60, w.start_min);   // 14:00
    TEST_ASSERT_EQUAL_UINT16(16 * 60, w.end_min);     // 16:00
}

void test_autotemp_mid_uses_temp_div3(void) {
    // tempValue in [low, setpoint) → duration = round(temp/3)
    // temp=24 → duration=8h, start=15-4=11, stop=11+8=19
    Window w = computeAutoTempWindow(
        24.0, 10.0, 27.0, 15, 8, 22);
    TEST_ASSERT_EQUAL_UINT16(11 * 60, w.start_min);
    TEST_ASSERT_EQUAL_UINT16(19 * 60, w.end_min);
}

void test_autotemp_warm_uses_temp_div2(void) {
    // tempValue >= setpoint → duration = round(temp/2)
    // temp=28 → duration=14h, start=clamp(15-7,8,21)=8, stop=min(8+14,22)=22
    Window w = computeAutoTempWindow(
        28.0, 10.0, 27.0, 15, 8, 22);
    TEST_ASSERT_EQUAL_UINT16(8 * 60,  w.start_min);
    TEST_ASSERT_EQUAL_UINT16(22 * 60, w.end_min);
}

void test_autotemp_clamps_start_to_startminhour(void) {
    // duration=20h, start=15-10=5 → clamp up to startMinHour=8
    Window w = computeAutoTempWindow(
        40.0, 10.0, 27.0, 15, 8, 22);
    TEST_ASSERT_EQUAL_UINT16(8 * 60, w.start_min);
}

void test_autotemp_clamps_stop_to_stopmaxhour(void) {
    // start clamped to 8, duration=20, stop would be 28 → clamp to 22
    Window w = computeAutoTempWindow(
        40.0, 10.0, 27.0, 15, 8, 22);
    TEST_ASSERT_EQUAL_UINT16(22 * 60, w.end_min);
}

void test_autotemp_odd_duration_rounds_half_up(void) {
    // tempValue=20 in mid band → duration = round(20/3) = 7
    // half_dur = 4 (round-half-up); start = 15 - 4 = 11; stop = 11 + 7 = 18
    // Locks down legacy parity with PoolMaster.cpp:136 round() behavior.
    Window w = computeAutoTempWindow(
        20.0, 10.0, 27.0, 15, 8, 22);
    TEST_ASSERT_EQUAL_UINT16(11 * 60, w.start_min);
    TEST_ASSERT_EQUAL_UINT16(18 * 60, w.end_min);
}

int main(int argc, char** argv) {
    UNITY_BEGIN();
    RUN_TEST(test_no_enabled_windows_returns_false);
    RUN_TEST(test_inside_single_enabled_window_returns_true);
    RUN_TEST(test_at_window_start_returns_true);
    RUN_TEST(test_at_window_end_returns_false);
    RUN_TEST(test_disabled_window_does_not_match);
    RUN_TEST(test_multiple_windows_any_match_returns_true);
    RUN_TEST(test_edges_at_midnight_boundaries);
    RUN_TEST(test_autotemp_cold_uses_2h_duration);
    RUN_TEST(test_autotemp_mid_uses_temp_div3);
    RUN_TEST(test_autotemp_warm_uses_temp_div2);
    RUN_TEST(test_autotemp_clamps_start_to_startminhour);
    RUN_TEST(test_autotemp_clamps_stop_to_stopmaxhour);
    RUN_TEST(test_autotemp_odd_duration_rounds_half_up);
    return UNITY_END();
}
