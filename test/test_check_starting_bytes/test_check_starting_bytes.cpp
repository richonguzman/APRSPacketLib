// Regression coverage for APRSPacketLib::checkForStartingBytes(), moved
// here from LoRa_APRS_iGate's aprs_is_utils.cpp -- a defensive trim that
// cuts a payload/message at the LoRa RF frame marker (\x3c\xff\x01) if it
// happens to appear embedded inside it.

#include <Arduino.h>
#include <unity.h>
#include <APRSPacketLib.h>

void setUp(void) {}
void tearDown(void) {}

static void test_no_marker_returns_unchanged(void) {
    String in = "hello world, this is a normal comment";
    String got = APRSPacketLib::checkForStartingBytes(in);
    TEST_ASSERT_TRUE_MESSAGE(got == in, "no marker present: string should pass through unchanged");
}

static void test_marker_in_middle_cuts_there(void) {
    String in = String("hello") + "\x3c\xff\x01" + "injected fake packet";
    String got = APRSPacketLib::checkForStartingBytes(in);
    TEST_ASSERT_TRUE_MESSAGE(got == "hello", "marker mid-string: everything from the marker onward must be cut");
}

static void test_marker_at_start_returns_empty(void) {
    String in = String("\x3c\xff\x01") + "whole thing is the marker onward";
    String got = APRSPacketLib::checkForStartingBytes(in);
    TEST_ASSERT_TRUE_MESSAGE(got == "", "marker at position 0: result should be empty");
}

static void test_empty_string_returns_empty(void) {
    String got = APRSPacketLib::checkForStartingBytes("");
    TEST_ASSERT_TRUE_MESSAGE(got == "", "empty input should return empty, not crash");
}

static void run_all(void) {
    RUN_TEST(test_no_marker_returns_unchanged);
    RUN_TEST(test_marker_in_middle_cuts_there);
    RUN_TEST(test_marker_at_start_returns_empty);
    RUN_TEST(test_empty_string_returns_empty);
}

#ifdef NATIVE_TEST_BUILD
int main(int /*argc*/, char ** /*argv*/) {
    UNITY_BEGIN();
    run_all();
    return UNITY_END();
}
#else
void setup(void) {
    Serial.begin(115200);
    delay(2000);
    UNITY_BEGIN();
    run_all();
    UNITY_END();
}

void loop(void) {
    delay(1000);
}
#endif
