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

int main(int argc, char** argv) {
    UNITY_BEGIN();
    RUN_TEST(test_no_enabled_windows_returns_false);
    RUN_TEST(test_inside_single_enabled_window_returns_true);
    RUN_TEST(test_at_window_start_returns_true);
    RUN_TEST(test_at_window_end_returns_false);
    RUN_TEST(test_disabled_window_does_not_match);
    RUN_TEST(test_multiple_windows_any_match_returns_true);
    RUN_TEST(test_edges_at_midnight_boundaries);
    return UNITY_END();
}
