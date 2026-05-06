// Minimal isolation test: verifies that pio test can flash the board, that
// the application starts, and that USB CDC enumerates and carries serial
// output. Unrelated to APRS encoding — used to separate "upload pipeline
// works" from "library code under test works".

#include <Arduino.h>
#include <unity.h>

void setUp(void) {}
void tearDown(void) {}

static void test_alive(void) {
    TEST_PASS();
}

#ifdef NATIVE_TEST_BUILD
int main(int /*argc*/, char ** /*argv*/) {
    UNITY_BEGIN();
    RUN_TEST(test_alive);
    return UNITY_END();
}
#else
void setup(void) {
    Serial.begin(115200);
    delay(2000);
    UNITY_BEGIN();
    RUN_TEST(test_alive);
    UNITY_END();
}

void loop(void) {
    delay(1000);
}
#endif
