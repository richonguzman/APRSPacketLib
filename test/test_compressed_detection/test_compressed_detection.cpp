// Regression coverage for the compressed-vs-uncompressed position
// detection in processReceivedPacket() (src/APRSPacketLib.cpp).
//
// Historically this branch was gated on the compression-type byte
// (payload byte offset+12), whitelisted to a handful of observed
// values {G,Q,[,H,X,T}. That byte is actually a free-form base91
// value (~90 valid values per APRS Protocol Reference v1.2 §9), so
// any conformant compressed packet using a different T-byte fell
// through to the "Degrees and Decimal Minutes" branch and got
// mis-decoded. The fix now gates on the symbol table id (payload
// byte offset+0), which per spec can only be '/', '\', or an
// overlay 'A'-'Z' / 'a'-'j'.
//
// These tests build full synthetic packets (not just the internal
// decode helpers) so they exercise the actual branch-selection logic
// in processReceivedPacket(), which the other test files don't touch.

#include <Arduino.h>
#include <unity.h>
#include <APRSPacketLib.h>
#include <stdio.h>

void setUp(void) {}
void tearDown(void) {}

struct FixedPoint {
    const char* name;
    float       lat_deg;
    float       lon_deg;
    const char* base91_lat;   // 4 chars
    const char* base91_lon;   // 4 chars
    char        base91_c;
    char        base91_s;
};

// Reused from test/common/aprs_vectors.h (Munich / CapeTown rows).
static const FixedPoint kMunich   = { "Munich",   48.138630f,  11.573410f, "6/Y`", "QG2.", '8', 'N' };
static const FixedPoint kCapeTown = { "CapeTown", -33.918861f, 18.423300f, "_[9A", "S.cy", 'N', '@' };

static const float kLatLonTolDeg = 0.01f;   // generous: this checks "right branch", not decode precision

static String buildCompressedPacket(const FixedPoint& p, char symbolTable, char symbol, char tByte) {
    String payload = String(symbolTable) + p.base91_lat + p.base91_lon + String(symbol) +
                      String(p.base91_c) + String(p.base91_s) + String(tByte);
    return "CA2RXU>APRS,WIDE1-1:!" + payload;
}

// ---------- T-bytes outside the old {G,Q,[,H,X,T} whitelist ----------

static void assert_decodes_as_compressed(const char* label, const FixedPoint& p, char tByte) {
    String packet = buildCompressedPacket(p, '/', '>', tByte);
    APRSPacket got = APRSPacketLib::processReceivedPacket(packet, 0, 0, 0);

    char msg[160];
    snprintf(msg, sizeof(msg), "%s (T-byte 0x%02X '%c'): type", label, (unsigned char)tByte, tByte);
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, got.type, msg);

    snprintf(msg, sizeof(msg), "%s (T-byte 0x%02X '%c'): latitude", label, (unsigned char)tByte, tByte);
    TEST_ASSERT_FLOAT_WITHIN_MESSAGE(kLatLonTolDeg, p.lat_deg, got.latitude, msg);

    snprintf(msg, sizeof(msg), "%s (T-byte 0x%02X '%c'): longitude", label, (unsigned char)tByte, tByte);
    TEST_ASSERT_FLOAT_WITHIN_MESSAGE(kLatLonTolDeg, p.lon_deg, got.longitude, msg);
}

static void test_previously_whitelisted_t_bytes_still_work(void) {
    // The old whitelist -- must still decode correctly (no regression).
    const char oldWhitelist[] = { 'G', 'Q', '[', 'H', 'X', 'T' };
    for (char t : oldWhitelist) {
        assert_decodes_as_compressed("CapeTown/whitelisted", kCapeTown, t);
    }
}

static void test_previously_rejected_t_bytes_now_work(void) {
    // Real, spec-valid T-byte values that were NOT in the old whitelist.
    // These used to silently fall through to the wrong branch.
    const char previouslyRejected[] = { '!', '"', '#', '$', ')', '1', 'M', 'Z', ']', 'z', '{' };
    for (char t : previouslyRejected) {
        assert_decodes_as_compressed("CapeTown/previously-rejected", kCapeTown, t);
        assert_decodes_as_compressed("Munich/previously-rejected", kMunich, t);
    }
}

static void test_alternate_symbol_tables_still_detected(void) {
    // Symbol table id itself varies too ('/', '\\', overlay letters) --
    // confirm the new gate accepts all of them regardless of T-byte.
    assert_decodes_as_compressed("CapeTown/backslash-table", kCapeTown, '!');

    String packet = buildCompressedPacket(kCapeTown, '\\', '>', '!');
    APRSPacket got = APRSPacketLib::processReceivedPacket(packet, 0, 0, 0);
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, got.type, "backslash table: type");
    TEST_ASSERT_FLOAT_WITHIN_MESSAGE(kLatLonTolDeg, kCapeTown.lat_deg, got.latitude, "backslash table: latitude");

    String packetOverlay = buildCompressedPacket(kCapeTown, 'W', '>', '!');   // overlay letter A-Z
    APRSPacket gotOverlay = APRSPacketLib::processReceivedPacket(packetOverlay, 0, 0, 0);
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, gotOverlay.type, "overlay A-Z table: type");
    TEST_ASSERT_FLOAT_WITHIN_MESSAGE(kLatLonTolDeg, kCapeTown.lat_deg, gotOverlay.latitude, "overlay A-Z table: latitude");

    String packetOverlayDigit = buildCompressedPacket(kCapeTown, 'e', '>', '!');   // overlay digit a-j
    APRSPacket gotOverlayDigit = APRSPacketLib::processReceivedPacket(packetOverlayDigit, 0, 0, 0);
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, gotOverlayDigit.type, "overlay a-j table: type");
    TEST_ASSERT_FLOAT_WITHIN_MESSAGE(kLatLonTolDeg, kCapeTown.lat_deg, gotOverlayDigit.latitude, "overlay a-j table: latitude");
}

// ---------- uncompressed positions must still be classified correctly ----------

static void test_uncompressed_position_not_misdetected_as_compressed(void) {
    // Classic Degrees-and-Decimal-Minutes packet. Its first info byte is
    // a digit ('4'), so it must never enter the compressed branch,
    // regardless of what byte happens to land at the old offset+12 check.
    String packet = "CA2RXU>APRS,WIDE1-1:!4903.50N/07201.75W>Test comment";
    APRSPacket got = APRSPacketLib::processReceivedPacket(packet, 0, 0, 0);

    TEST_ASSERT_EQUAL_INT_MESSAGE(0, got.type, "uncompressed: type");
    TEST_ASSERT_FLOAT_WITHIN_MESSAGE(0.01f, 49.0583f, got.latitude, "uncompressed: latitude");
    TEST_ASSERT_FLOAT_WITHIN_MESSAGE(0.01f, -72.0292f, got.longitude, "uncompressed: longitude");
}

// ---------- runner ----------

static void run_all(void) {
    RUN_TEST(test_previously_whitelisted_t_bytes_still_work);
    RUN_TEST(test_previously_rejected_t_bytes_now_work);
    RUN_TEST(test_alternate_symbol_tables_still_detected);
    RUN_TEST(test_uncompressed_position_not_misdetected_as_compressed);
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
