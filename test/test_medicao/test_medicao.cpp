#include <unity.h>

void test_toolchain_funciona(void) {
    TEST_ASSERT_EQUAL_INT(4, 2 + 2);
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_toolchain_funciona);
    return UNITY_END();
}
