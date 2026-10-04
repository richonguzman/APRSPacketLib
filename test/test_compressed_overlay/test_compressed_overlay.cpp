// Coverage for the overlay handling in APRSPacketLib::generateBase91GPSBeaconPacket().
// APRS101 ch.9: in compressed format the symbol table / overlay is the first
// byte of the position, so an overlay digit 0-9 must be sent as a-j -- a digit
// there would be read as the start of an uncompressed latitude and the whole
// position would be mis-decoded. Letters and the '/' '\' tables pass unchanged.

#include <Arduino.h>
#include <unity.h>
#include <APRSPacketLib.h>

void setUp(void) {}
void tearDown(void) {}

static String beaconWithOverlay(const String& overlay) {
    String gps = APRSPacketLib::encodeGPSIntoBase91(-33.0416, -71.5683, 0, 0, "#", false, 0, true);
    return APRSPacketLib::generateBase91GPSBeaconPacket("CA2RXU-10", "APLRG1", "WIDE1-1", overlay, gps);
}

static void test_overlay_digits_are_sent_as_a_to_j(void) {
    const char* digits  = "0123456789";
    const char* letters = "abcdefghij";
    for (int i = 0; i < 10; i++) {
        String packet   = beaconWithOverlay(String(digits[i]));
        String expected = String(":=") + letters[i];
        TEST_ASSERT_TRUE_MESSAGE(packet.indexOf(expected) == packet.indexOf(":"), "overlay digit must be converted to its a-j letter right after ':='");
    }
}

static void test_letter_overlay_and_tables_are_unchanged(void) {
    TEST_ASSERT_TRUE_MESSAGE(beaconWithOverlay("L").indexOf(":=L") != -1, "letter overlay must pass unchanged");
    TEST_ASSERT_TRUE_MESSAGE(beaconWithOverlay("/").indexOf(":=/") != -1, "primary table must pass unchanged");
    TEST_ASSERT_TRUE_MESSAGE(beaconWithOverlay("\\").indexOf(":=\\") != -1, "alternate table must pass unchanged");
}

static void test_digit_overlay_packet_decodes_as_compressed(void) {
    APRSPacket decoded = APRSPacketLib::processReceivedPacket(beaconWithOverlay("1"), 0, 0, 0);
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, decoded.type, "packet must be recognized as a GPS position");
    TEST_ASSERT_TRUE_MESSAGE(decoded.overlay == "b", "decoder must see the converted overlay 'b' as the compressed symbol table");
    TEST_ASSERT_TRUE_MESSAGE(decoded.symbol == "#", "symbol must be decoded from the compressed block");
    TEST_ASSERT_FLOAT_WITHIN_MESSAGE(0.0001, -33.0416, decoded.latitude, "latitude must decode correctly (was mis-decoded as uncompressed before the fix)");
    TEST_ASSERT_FLOAT_WITHIN_MESSAGE(0.0001, -71.5683, decoded.longitude, "longitude must decode correctly");
}

static void run_all(void) {
    RUN_TEST(test_overlay_digits_are_sent_as_a_to_j);
    RUN_TEST(test_letter_overlay_and_tables_are_unchanged);
    RUN_TEST(test_digit_overlay_packet_decodes_as_compressed);
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
