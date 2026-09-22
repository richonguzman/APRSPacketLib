// Regression coverage for processReceivedPacket() leaving just the
// free-text comment in aprsPacket.payload for Mic-E packets, in both
// the no-altitude (8 fixed bytes) and with-altitude (13 fixed bytes,
// `xxx} block included) cases.

#include <Arduino.h>
#include <unity.h>
#include <APRSPacketLib.h>

void setUp(void) {}
void tearDown(void) {}

// Fixed 8-byte Mic-E info field: 3 longitude + 3 speed/course + symbol + overlay.
// Values reused from the real captured frame used in test_mice_longitude
// (F4MLV-7's reproduction) -- byte content doesn't matter for this test,
// only where the comment starts.
static const char* kMiceFixedFields = "w25l*o[/";

static void test_no_altitude_payload_is_just_the_comment(void) {
    String packet = String("F4MLV-7>4R5WV3,WIDE1-1:`") + kMiceFixedFields + "Test comment!";
    APRSPacket got = APRSPacketLib::processReceivedPacket(packet, 0, 0, 0);

    TEST_ASSERT_EQUAL_INT_MESSAGE(4, got.type, "type (Mic-E)");
    TEST_ASSERT_TRUE_MESSAGE(got.payload == "Test comment!", "no-altitude payload should be just the comment");
}

static void test_with_altitude_payload_is_just_the_comment(void) {
    // `xxx} altitude block (5 bytes) between the fixed fields and the comment.
    String packet = String("F4MLV-7>4R5WV3,WIDE1-1:`") + kMiceFixedFields + "`123}Test comment!";
    APRSPacket got = APRSPacketLib::processReceivedPacket(packet, 0, 0, 0);

    TEST_ASSERT_EQUAL_INT_MESSAGE(4, got.type, "type (Mic-E)");
    TEST_ASSERT_TRUE_MESSAGE(got.payload == "Test comment!", "with-altitude payload should be just the comment, altitude block excluded");
}

static void test_no_comment_stays_empty(void) {
    String packet = String("F4MLV-7>4R5WV3,WIDE1-1:`") + kMiceFixedFields;
    APRSPacket got = APRSPacketLib::processReceivedPacket(packet, 0, 0, 0);

    TEST_ASSERT_TRUE_MESSAGE(got.payload == "", "no trailing text should mean an empty comment, not a crash");
}

static void test_speed_course_symbol_overlay_still_correct_after_trim(void) {
    // Sanity: fields already extracted from the full payload before the
    // trim must be unaffected by the later reassignment.
    String packet = String("F4MLV-7>4R5WV3,WIDE1-1:`") + kMiceFixedFields + "Test comment!";
    APRSPacket got = APRSPacketLib::processReceivedPacket(packet, 0, 0, 0);

    TEST_ASSERT_TRUE_MESSAGE(got.symbol == "[", "symbol unaffected by payload trim");
    TEST_ASSERT_TRUE_MESSAGE(got.overlay == "/", "overlay unaffected by payload trim");
}

static void run_all(void) {
    RUN_TEST(test_no_altitude_payload_is_just_the_comment);
    RUN_TEST(test_with_altitude_payload_is_just_the_comment);
    RUN_TEST(test_no_comment_stays_empty);
    RUN_TEST(test_speed_course_symbol_overlay_still_correct_after_trim);
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
