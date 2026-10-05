// Coverage for APRSPacketLib::generateMessagePacket() and its internal
// formatAddressee() helper. APRS101 ch.14: the addressee is a fixed
// 9-character field -- shorter addressees are padded with spaces, and
// anything longer must be cut to 9 so the ':' separator stays at its fixed
// position. Output for normal callsigns must stay byte-for-byte unchanged.

#include <Arduino.h>
#include <unity.h>
#include <APRSPacketLib.h>

namespace APRSPacketLib {
    String formatAddressee(const String& addressee);    // internal helper, not in the public header
}

void setUp(void) {}
void tearDown(void) {}

static void test_short_addressee_is_padded(void) {
    TEST_ASSERT_TRUE_MESSAGE(APRSPacketLib::formatAddressee("CA2RXU") == "CA2RXU   ", "short addressee must be padded with spaces to 9 chars");
}

static void test_nine_char_addressee_is_unchanged(void) {
    TEST_ASSERT_TRUE_MESSAGE(APRSPacketLib::formatAddressee("CA2RXU-10") == "CA2RXU-10", "9-char addressee must stay as is");
}

static void test_long_addressee_is_cut_to_nine(void) {
    TEST_ASSERT_TRUE_MESSAGE(APRSPacketLib::formatAddressee("ABCDEFG-15") == "ABCDEFG-1", "addressee longer than 9 chars must be cut to 9");
}

static void test_empty_addressee_is_all_spaces(void) {
    TEST_ASSERT_TRUE_MESSAGE(APRSPacketLib::formatAddressee("") == "         ", "empty addressee must become 9 spaces");
}

static void test_message_packet_unchanged_for_normal_callsign(void) {
    String got = APRSPacketLib::generateMessagePacket("CA2RXU-7", "APLRT1", "WIDE1-1", "CA2RXU", "hola{12");
    TEST_ASSERT_TRUE_MESSAGE(got == "CA2RXU-7>APLRT1,WIDE1-1::CA2RXU   :hola{12", "message packet for a normal callsign must keep the same format as before");
}

static void test_message_text_is_trimmed(void) {
    String got = APRSPacketLib::generateMessagePacket("CA2RXU-7", "APLRT1", "WIDE1-1", "CA2RXU-10", "  ack12  ");
    TEST_ASSERT_TRUE_MESSAGE(got == "CA2RXU-7>APLRT1,WIDE1-1::CA2RXU-10:ack12", "message text must be trimmed");
}

static void test_long_addressee_keeps_separator_position(void) {
    String got = APRSPacketLib::generateMessagePacket("CA2RXU-7", "APLRT1", "WIDE1-1", "ABCDEFG-15", "test");
    int doubleColonIndex = got.indexOf("::");
    TEST_ASSERT_EQUAL_INT_MESSAGE(':', got[doubleColonIndex + 2 + 9], "the ':' after the addressee must stay at its fixed position (9 chars after '::')");
    TEST_ASSERT_TRUE_MESSAGE(got.endsWith("::ABCDEFG-1:test"), "long addressee must be cut to 9 inside the packet");
}

static void test_bulletin_addressee(void) {
    String got = APRSPacketLib::generateMessagePacket("CA2RXU-10", "APLRG1", "WIDE1-1", "BLN1", "Net tonight 21:00");
    TEST_ASSERT_TRUE_MESSAGE(got == "CA2RXU-10>APLRG1,WIDE1-1::BLN1     :Net tonight 21:00", "bulletin addressee must be padded like any other");
}

static void run_all(void) {
    RUN_TEST(test_short_addressee_is_padded);
    RUN_TEST(test_nine_char_addressee_is_unchanged);
    RUN_TEST(test_long_addressee_is_cut_to_nine);
    RUN_TEST(test_empty_addressee_is_all_spaces);
    RUN_TEST(test_message_packet_unchanged_for_normal_callsign);
    RUN_TEST(test_message_text_is_trimmed);
    RUN_TEST(test_long_addressee_keeps_separator_position);
    RUN_TEST(test_bulletin_addressee);
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
