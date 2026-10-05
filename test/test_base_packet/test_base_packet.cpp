// Coverage for APRSPacketLib::generateBasePacket(). The path used to be kept
// only when it started with "WIDE" (anything else was silently dropped, so
// RFONLY / NOGATE / explicit digi callsigns never made it into the header).
// Now the path is cleaned hop by hop: spaces and empty hops are dropped and
// every other hop is kept, so empty or sloppy paths still give a valid header.

#include <Arduino.h>
#include <unity.h>
#include <APRSPacketLib.h>

void setUp(void) {}
void tearDown(void) {}

static void test_empty_path_gives_no_comma(void) {
    TEST_ASSERT_TRUE_MESSAGE(APRSPacketLib::generateBasePacket("CA2RXU-10", "APLRG1", "") == "CA2RXU-10>APLRG1", "empty path must not leave a trailing comma");
}

static void test_wide_paths_unchanged(void) {
    TEST_ASSERT_TRUE_MESSAGE(APRSPacketLib::generateBasePacket("CA2RXU-10", "APLRG1", "WIDE1-1") == "CA2RXU-10>APLRG1,WIDE1-1", "single WIDE hop must stay as before");
    TEST_ASSERT_TRUE_MESSAGE(APRSPacketLib::generateBasePacket("CA2RXU-7", "APLRT1", "WIDE1-1,WIDE2-1") == "CA2RXU-7>APLRT1,WIDE1-1,WIDE2-1", "multi-hop WIDE path must stay as before");
}

static void test_trailing_comma_is_dropped(void) {
    TEST_ASSERT_TRUE_MESSAGE(APRSPacketLib::generateBasePacket("CA2RXU-10", "APLRG1", "WIDE1-1,") == "CA2RXU-10>APLRG1,WIDE1-1", "trailing comma in the path must be dropped");
}

static void test_rfonly_path_is_kept(void) {
    TEST_ASSERT_TRUE_MESSAGE(APRSPacketLib::generateBasePacket("CA2RXU-10", "APLRG1", "RFONLY,WIDE1-1") == "CA2RXU-10>APLRG1,RFONLY,WIDE1-1", "RFONLY must no longer be dropped");
}

static void test_rfonly_with_empty_configured_path(void) {
    String configuredPath = "";
    TEST_ASSERT_TRUE_MESSAGE(APRSPacketLib::generateBasePacket("CA2RXU-10", "APLRG1", "RFONLY," + configuredPath) == "CA2RXU-10>APLRG1,RFONLY", "\"RFONLY,\" + empty path must not leave a trailing comma");
}

static void test_explicit_digi_callsign_is_kept(void) {
    TEST_ASSERT_TRUE_MESSAGE(APRSPacketLib::generateBasePacket("CA2RXU-7", "APLRT1", "CA2RXU-10") == "CA2RXU-7>APLRT1,CA2RXU-10", "explicit digipeater callsign path must be kept");
}

static void test_spaces_around_hops_are_trimmed(void) {
    TEST_ASSERT_TRUE_MESSAGE(APRSPacketLib::generateBasePacket("CA2RXU-7", "APLRT1", " WIDE1-1 , WIDE2-1 ") == "CA2RXU-7>APLRT1,WIDE1-1,WIDE2-1", "spaces around hops must be trimmed");
}

static void test_empty_hops_in_the_middle_are_dropped(void) {
    TEST_ASSERT_TRUE_MESSAGE(APRSPacketLib::generateBasePacket("CA2RXU-7", "APLRT1", "WIDE1-1,,WIDE2-1") == "CA2RXU-7>APLRT1,WIDE1-1,WIDE2-1", "empty hops in the middle must be dropped");
    TEST_ASSERT_TRUE_MESSAGE(APRSPacketLib::generateBasePacket("CA2RXU-7", "APLRT1", ",") == "CA2RXU-7>APLRT1", "a path with only commas must give no path");
}

static void test_generators_use_the_cleaned_path(void) {
    String status = APRSPacketLib::generateStatusPacket("CA2RXU-10", "APLRG1", "RFONLY,", "test");
    TEST_ASSERT_TRUE_MESSAGE(status == "CA2RXU-10>APLRG1,RFONLY:>test", "status packet must use the cleaned path");
    String message = APRSPacketLib::generateMessagePacket("CA2RXU-10", "APLRG1", "RFONLY,WIDE1-1", "CA2RXU-7", "ack12");
    TEST_ASSERT_TRUE_MESSAGE(message == "CA2RXU-10>APLRG1,RFONLY,WIDE1-1::CA2RXU-7 :ack12", "message packet (ack over RF) must keep RFONLY");
}

static void run_all(void) {
    RUN_TEST(test_empty_path_gives_no_comma);
    RUN_TEST(test_wide_paths_unchanged);
    RUN_TEST(test_trailing_comma_is_dropped);
    RUN_TEST(test_rfonly_path_is_kept);
    RUN_TEST(test_rfonly_with_empty_configured_path);
    RUN_TEST(test_explicit_digi_callsign_is_kept);
    RUN_TEST(test_spaces_around_hops_are_trimmed);
    RUN_TEST(test_empty_hops_in_the_middle_are_dropped);
    RUN_TEST(test_generators_use_the_cleaned_path);
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
