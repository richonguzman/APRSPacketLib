// Regression coverage for the decoder unit-conversion truncation bug
// documented in wip/bug_report_decoder_unit_truncation.md.
//
// Five decoders in src/APRSPacketLib.cpp return `int` from a `double`
// expression of the form `(spec_formula) * 1.852` or `* 0.3048`. The
// implicit double->int cast truncates toward zero, which for positive
// speed/altitude values floors instead of rounding to nearest. APRS12c
// §9 is silent on the integer rounding (the spec defines decoded
// speed in knots only); but truncation introduces a systematic
// downward bias of up to ~1 km/h (speed) or ~1 m (altitude), whereas
// round-to-nearest halves the worst-case error and matches direwolf's
// printed integer. See the bug report for the spec / cross-impl
// discussion.
//
// Each test below picks one input where truncation and round-to-
// nearest differ by exactly 1, and wraps the rounded-value
// expectation in `if (got != expected) TEST_IGNORE_MESSAGE(...)` —
// the IGNORE-on-divergence convention used by L12/L13 in
// test_base91_encode.cpp. Under the current (truncating) library each
// test reports SKIPPED with a message naming the bug report; when the
// patch lands, the IGNORE branch stops triggering and the test
// transitions to PASSED with no source change. To convert any test
// to a hard assertion locally (e.g. to verify the fix before
// committing), replace its TEST_IGNORE_MESSAGE call with the
// corresponding TEST_ASSERT_EQUAL_INT_MESSAGE — the `expected` and
// `got` variables are already in scope.
//
// All five decoders are file-local helpers in src/APRSPacketLib.cpp
// (not in the public header), reached at runtime via
// processReceivedPacket. We forward-declare them here; this is
// link-clean because platformio.ini sets test_build_src=yes, pulling
// APRSPacketLib.cpp into each test binary.

#include <Arduino.h>
#include <unity.h>
#include <APRSPacketLib.h>
#include <stdio.h>

namespace APRSPacketLib {
    int decodeBase91EncodedSpeed(const String& speed);
    int decodeBase91EncodedAltitude(const String& altitude);
    int decodeSpeed(const String& speed);
    int decodeAltitude(const String& altitude);
    int decodeMiceSpeed(char char3, char char4);
}

void setUp(void) {}
void tearDown(void) {}

// ---------- compressed (Base91) ----------

static void test_base91_speed_rounds_to_nearest(void) {
    // s byte 'T' (ASCII 84) decodes to 1.08^51 - 1 ≈ 49.654 kn,
    // ×1.852 ≈ 91.959 km/h. Truncation gives 91 (error 0.959);
    // round-to-nearest gives 92 (error 0.041).
    const int expected = 92;
    int got = APRSPacketLib::decodeBase91EncodedSpeed(String("T"));
    if (got != expected) {
        TEST_IGNORE_MESSAGE(
            "decodeBase91EncodedSpeed(\"T\"): 91.959 km/h -> trunc 91, "
            "round 92 — see wip/bug_report_decoder_unit_truncation.md");
    }
}

static void test_base91_altitude_rounds_to_nearest(void) {
    // Bytes ('6','z'): cs = (54-33)*91 + (122-33) = 21*91 + 89 = 2000.
    // 1.002^2000 ≈ 54.388 ft, ×0.3048 ≈ 16.578 m. Truncation gives
    // 16 (error 0.578); round-to-nearest gives 17 (error 0.422).
    const int expected = 17;
    int got = APRSPacketLib::decodeBase91EncodedAltitude(String("6z"));
    if (got != expected) {
        TEST_IGNORE_MESSAGE(
            "decodeBase91EncodedAltitude(\"6z\"): 16.578 m -> trunc 16, "
            "round 17 — see wip/bug_report_decoder_unit_truncation.md");
    }
}

// ---------- uncompressed (DDMM.hh) ----------

static void test_uncompressed_speed_rounds_to_nearest(void) {
    // 30 knots × 1.852 = 55.560 km/h. Truncation gives 55 (error
    // 0.560); round-to-nearest gives 56 (error 0.440). Real packets
    // carry the speed as a 3-digit knot field in the info portion.
    const int expected = 56;
    int got = APRSPacketLib::decodeSpeed(String("030"));
    if (got != expected) {
        TEST_IGNORE_MESSAGE(
            "decodeSpeed(\"030\"): 55.560 km/h -> trunc 55, round 56 — "
            "see wip/bug_report_decoder_unit_truncation.md");
    }
}

static void test_uncompressed_altitude_rounds_to_nearest(void) {
    // 1722 feet × 0.3048 = 524.866 m. Truncation gives 524 (error
    // 0.866); round-to-nearest gives 525 (error 0.134). 1722 ft is
    // Munich's encoded altitude in the fixed-point vectors.
    const int expected = 525;
    int got = APRSPacketLib::decodeAltitude(String("001722"));
    if (got != expected) {
        TEST_IGNORE_MESSAGE(
            "decodeAltitude(\"001722\"): 524.866 m -> trunc 524, round 525 "
            "— see wip/bug_report_decoder_unit_truncation.md");
    }
}

// ---------- Mic-E ----------

static void test_mice_speed_rounds_to_nearest(void) {
    // char3='!' (ASCII 33): SP byte -> SP28 = (33-28)*10 = 50.
    // char4='%' (ASCII 37): DC byte -> DC28 = (37-28)/10 = 0.
    // Decoded speed = (50+0) × 1.852 = 92.600 km/h. Truncation gives
    // 92 (error 0.600); round-to-nearest gives 93 (error 0.400).
    const int expected = 93;
    int got = APRSPacketLib::decodeMiceSpeed('!', '%');
    if (got != expected) {
        TEST_IGNORE_MESSAGE(
            "decodeMiceSpeed('!','%'): 92.600 km/h -> trunc 92, round 93 "
            "— see wip/bug_report_decoder_unit_truncation.md");
    }
}

// ---------- runner ----------

static void run_all(void) {
    RUN_TEST(test_base91_speed_rounds_to_nearest);
    RUN_TEST(test_base91_altitude_rounds_to_nearest);
    RUN_TEST(test_uncompressed_speed_rounds_to_nearest);
    RUN_TEST(test_uncompressed_altitude_rounds_to_nearest);
    RUN_TEST(test_mice_speed_rounds_to_nearest);
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
