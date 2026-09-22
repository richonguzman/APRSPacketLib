// Regression coverage for processReceivedPacket() leaving just the
// free-text comment in aprsPacket.payload for GPS packets (both
// compressed and uncompressed), instead of the raw position/symbol/
// course/speed/altitude bytes + comment mixed together.

#include <Arduino.h>
#include <unity.h>
#include <APRSPacketLib.h>

void setUp(void) {}
void tearDown(void) {}

static void test_compressed_payload_is_just_the_comment(void) {
    // "/" symbol table, CapeTown fixed point (_[9A / S.cy), 'N' 'G' course/speed
    // bytes, '!' compression-type byte (13th char -- deliberately not 'T'/'Q',
    // which the decoder treats specially), then a free-text comment.
    String packet = "CA2RXU>APRS,WIDE1-1:!/_[9AS.cy>NG!Test comment here";
    APRSPacket got = APRSPacketLib::processReceivedPacket(packet, 0, 0, 0);

    TEST_ASSERT_EQUAL_INT_MESSAGE(0, got.type, "type");
    TEST_ASSERT_TRUE_MESSAGE(got.payload == "Test comment here", "compressed payload should be just the comment");
}

static void test_uncompressed_payload_is_just_the_comment(void) {
    String packet = "CA2RXU>APRS,WIDE1-1:!4903.50N/07201.75W>Test comment here";
    APRSPacket got = APRSPacketLib::processReceivedPacket(packet, 0, 0, 0);

    TEST_ASSERT_EQUAL_INT_MESSAGE(0, got.type, "type");
    TEST_ASSERT_TRUE_MESSAGE(got.payload == "Test comment here", "uncompressed payload should be just the comment");
}

static void test_compressed_payload_empty_comment_stays_empty(void) {
    String packet = "CA2RXU>APRS,WIDE1-1:!/_[9AS.cy>NG!";   // no trailing text at all
    APRSPacket got = APRSPacketLib::processReceivedPacket(packet, 0, 0, 0);

    TEST_ASSERT_TRUE_MESSAGE(got.payload == "", "no trailing text should mean an empty comment, not a crash");
}

static void test_lat_lon_still_correct_after_payload_trim(void) {
    // Sanity: trimming payload must not have disturbed the lat/lon math,
    // since extraction still reads from temp0, not from the (now shorter) payload.
    String packet = "CA2RXU>APRS,WIDE1-1:!4903.50N/07201.75W>Test comment here";
    APRSPacket got = APRSPacketLib::processReceivedPacket(packet, 0, 0, 0);
    TEST_ASSERT_FLOAT_WITHIN_MESSAGE(0.01f, 49.0583f, got.latitude, "latitude unaffected by payload trim");
    TEST_ASSERT_FLOAT_WITHIN_MESSAGE(0.01f, -72.0292f, got.longitude, "longitude unaffected by payload trim");
}

static void run_all(void) {
    RUN_TEST(test_compressed_payload_is_just_the_comment);
    RUN_TEST(test_uncompressed_payload_is_just_the_comment);
    RUN_TEST(test_compressed_payload_empty_comment_stays_empty);
    RUN_TEST(test_lat_lon_still_correct_after_payload_trim);
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
