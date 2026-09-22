// Regression coverage for decodeMiceLongitude() (src/APRSPacketLib.cpp),
// reported against APRSPacketLib 1.0.4 / current main.
//
// APRS101 §10 ("Mic-E Longitude Degrees Encoding"): the raw degree byte
// (informationField[0] - 28), after the destination-field +100 offset is
// applied, must be folded back down when it lands in two reserved bands:
//   180..189 actually means 100..109
//   190..199 actually means 0..9
// Without that fold, e.g. a station at 1.37E decodes as 191.37E instead
// (the exact case reported in the issue, reproduced here verbatim).
//
// decodeMiceLongitude() is a file-local helper reached at runtime via
// processReceivedPacket (src/APRSPacketLib.cpp:711). We forward-declare
// it here for the direct boundary-band unit tests; this is link-clean
// because platformio.ini sets test_build_src=yes, pulling
// APRSPacketLib.cpp into the test binary.

#include <Arduino.h>
#include <unity.h>
#include <APRSPacketLib.h>
#include <stdio.h>

namespace APRSPacketLib {
    float decodeMiceLongitude(const String& destinationField, const String& informationField);
}

void setUp(void) {}
void tearDown(void) {}

static const float kLonTolDeg = 0.001f;

// ---------- full-packet reproduction of the reported issue ----------

static void test_issue_reproduction_frame(void) {
    // F4MLV-7 at 42.96050 N, 1.37083 E -- previously decoded as 191.370833 E.
    String packet = "F4MLV-7>4R5WV3,WIDE1-1,WIDE2-1:`w25l*o[/\"=?}";
    APRSPacket got = APRSPacketLib::processReceivedPacket(packet, 0, 0, 0);

    TEST_ASSERT_EQUAL_INT_MESSAGE(4, got.type, "type (Mic-E)");
    TEST_ASSERT_FLOAT_WITHIN_MESSAGE(0.01f, 42.96050f, got.latitude, "latitude");
    TEST_ASSERT_FLOAT_WITHIN_MESSAGE(kLonTolDeg, 1.370833f, got.longitude, "longitude (was 191.370833 before the fix)");
}

// ---------- direct boundary-band checks on decodeMiceLongitude() ----------

// destinationField only needs bytes [4] and [5] set correctly for this
// decoder: [4] > '9' selects the +100 offset, [5] > '9' selects West.
// The other positions (latitude data) are irrelevant here.
static String destField(bool offset, bool west) {
    String d = "000000";
    d.setCharAt(4, offset ? 'V' : '5');
    d.setCharAt(5, west   ? 'W' : '3');
    return d;
}

static String infoField(int degreeByte) {
    String s;
    s += (char)degreeByte;
    s += (char)28;   // minutes byte -> m28 = 0
    s += (char)28;   // hundredths byte -> h28 = 0
    return s;
}

static void test_band_190_199_folds_to_0_9_east(void) {
    // offset applied, raw d28 (pre-offset) = 91 -> +100 = 191 -> fold -190 = 1
    String info = infoField(28 + 91);
    float got = APRSPacketLib::decodeMiceLongitude(destField(/*offset*/true, /*west*/false), info);
    TEST_ASSERT_FLOAT_WITHIN_MESSAGE(kLonTolDeg, 1.0f, got, "191 should fold to 1 degree E");
}

static void test_band_180_189_folds_to_100_109_east(void) {
    // offset applied, raw d28 (pre-offset) = 85 -> +100 = 185 -> fold -80 = 105
    String info = infoField(28 + 85);
    float got = APRSPacketLib::decodeMiceLongitude(destField(/*offset*/true, /*west*/false), info);
    TEST_ASSERT_FLOAT_WITHIN_MESSAGE(kLonTolDeg, 105.0f, got, "185 should fold to 105 degrees E");
}

static void test_value_outside_reserved_bands_unaffected_west(void) {
    // no offset, d28 = 71 -- must NOT be touched by the fold (regression guard).
    String info = infoField(28 + 71);
    float got = APRSPacketLib::decodeMiceLongitude(destField(/*offset*/false, /*west*/true), info);
    TEST_ASSERT_FLOAT_WITHIN_MESSAGE(kLonTolDeg, -71.0f, got, "71 must stay 71 degrees W, no folding");
}

static void test_value_outside_reserved_bands_unaffected_east(void) {
    // offset applied but result (110) is outside 180..199 -- must NOT be folded.
    String info = infoField(28 + 10);
    float got = APRSPacketLib::decodeMiceLongitude(destField(/*offset*/true, /*west*/false), info);
    TEST_ASSERT_FLOAT_WITHIN_MESSAGE(kLonTolDeg, 110.0f, got, "110 must stay 110 degrees E, no folding");
}

// ---------- runner ----------

static void run_all(void) {
    RUN_TEST(test_issue_reproduction_frame);
    RUN_TEST(test_band_190_199_folds_to_0_9_east);
    RUN_TEST(test_band_180_189_folds_to_100_109_east);
    RUN_TEST(test_value_outside_reserved_bands_unaffected_west);
    RUN_TEST(test_value_outside_reserved_bands_unaffected_east);
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
