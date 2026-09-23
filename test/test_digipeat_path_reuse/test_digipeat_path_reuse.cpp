// Regression coverage for APRSPacketLib::generateDigipeatedPacket() -- the
// path token it's told to consume (e.g. "WIDE1-1") must be matched as a
// whole comma-delimited hop, not as a raw substring. A substring search
// also matches inside an already-consumed hop written with the non-standard
// bare marker form (e.g. "WIDE1-1*"), which would then get corrupted by the
// replace instead of being correctly refused as already-used.

#include <Arduino.h>
#include <unity.h>
#include <APRSPacketLib.h>

void setUp(void) {}
void tearDown(void) {}

static void test_single_hop_is_replaced(void) {
    String packet = String("\x3c\xff\x01") + "N0CALL>APRS,WIDE1-1:!4903.50N/07201.75W-Test";
    String got = APRSPacketLib::generateDigipeatedPacket(packet, "MYCALL", "WIDE1-1");
    TEST_ASSERT_TRUE_MESSAGE(got == "N0CALL>APRS,MYCALL*:!4903.50N/07201.75W-Test", "exact single-hop match should be replaced with callsign*");
}

static void test_already_consumed_nonstandard_marker_is_not_reused(void) {
    String packet = String("\x3c\xff\x01") + "N0CALL>APRS,WIDE1-1*:!4903.50N/07201.75W-Test";
    String got = APRSPacketLib::generateDigipeatedPacket(packet, "MYCALL", "WIDE1-1");
    TEST_ASSERT_TRUE_MESSAGE(got == "X", "an already-consumed 'WIDE1-1*' hop must not be re-matched by a 'WIDE1-1' search (was corrupted into 'MYCALL**' before the fix)");
}

static void test_second_hop_in_multihop_path_preserves_earlier_hop(void) {
    String packet = String("\x3c\xff\x01") + "N0CALL>APRS,N9OTHER*,WIDE2-1:!4903.50N/07201.75W-Test";
    String got = APRSPacketLib::generateDigipeatedPacket(packet, "MYCALL", "WIDE2-1");
    TEST_ASSERT_TRUE_MESSAGE(got == "N0CALL>APRS,N9OTHER*,MYCALL*:!4903.50N/07201.75W-Test", "only the target token should be replaced, the already-consumed earlier hop must be untouched");
}

static void test_missing_token_returns_X(void) {
    String packet = String("\x3c\xff\x01") + "N0CALL>APRS,WIDE2-2:!4903.50N/07201.75W-Test";
    String got = APRSPacketLib::generateDigipeatedPacket(packet, "MYCALL", "WIDE1-1");
    TEST_ASSERT_TRUE_MESSAGE(got == "X", "packet without the configured trigger token should not be repeated");
}

static void run_all(void) {
    RUN_TEST(test_single_hop_is_replaced);
    RUN_TEST(test_already_consumed_nonstandard_marker_is_not_reused);
    RUN_TEST(test_second_hop_in_multihop_path_preserves_earlier_hop);
    RUN_TEST(test_missing_token_returns_X);
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
