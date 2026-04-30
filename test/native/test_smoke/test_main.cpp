#include <unity.h>

void test_unity_works(void) {
    TEST_ASSERT_EQUAL(2, 1 + 1);
}

int main(int argc, char** argv) {
    UNITY_BEGIN();
    RUN_TEST(test_unity_works);
    return UNITY_END();
}
