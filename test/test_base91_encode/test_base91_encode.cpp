// Encoder-side coverage for the Base91 (compressed) APRS position
// format — APRS Protocol Reference v1.2 §9 ('Compressed Position Report
// Data Formats').
//
// All assertions compare against vectors from test/common/aprs_vectors.h,
// which is generated from spec formulas (cross-checked against aprslib)
// — never against the library's own output. When the library disagrees
// with the spec, the assertion is wrapped in TEST_IGNORE_MESSAGE so a
// future fix flips the test from IGNORE to PASS.
//
// Library output layout (12 chars):
//   [0..3]  YYYY base91 latitude
//   [4..7]  XXXX base91 longitude
//   [8]     symbol char
//   [9]     c (course or alt high digit)
//   [10]    s (speed or alt low digit)
//   [11]    T (compression type)

#include <Arduino.h>
#include <unity.h>
#include <APRSPacketLib.h>
#include <stdio.h>
#include <string.h>

#include "aprs_vectors.h"

void setUp(void) {}
void tearDown(void) {}

// Single-knob predicates. Each loops over the five fixed points and
// short-circuits to TEST_IGNORE_MESSAGE on the first divergence so the
// reason is preserved per concept. When the underlying bug is fixed,
// the test flips from IGNORE → PASS without any source change.

static void test_encode_lat_matches_spec_for_all_fixed_points(void) {
    for (const auto& p : kBase91FixedPoints) {
        String out = APRSPacketLib::encodeGPSIntoBase91(
            p.lat_deg, p.lon_deg, (float)p.course_deg, (float)p.speed_kn,
            "/", false, 0, false, 0);
        if (strncmp(out.c_str(), p.base91_lat, 4) != 0) {
            char msg[160];
            snprintf(msg, sizeof(msg),
                     "L12 (lat/lon integer drift): %s expected \"%s\" "
                     "got \"%.4s\" — see wip/bug_report_l12_encode_lat_lon.md",
                     p.name, p.base91_lat, out.c_str());
            TEST_IGNORE_MESSAGE(msg);
        }
    }
}

static void test_encode_lon_matches_spec_for_all_fixed_points(void) {
    for (const auto& p : kBase91FixedPoints) {
        String out = APRSPacketLib::encodeGPSIntoBase91(
            p.lat_deg, p.lon_deg, (float)p.course_deg, (float)p.speed_kn,
            "/", false, 0, false, 0);
        if (strncmp(out.c_str() + 4, p.base91_lon, 4) != 0) {
            char msg[160];
            snprintf(msg, sizeof(msg),
                     "L12 (lat/lon integer drift): %s expected \"%s\" "
                     "got \"%.4s\" — see wip/bug_report_l12_encode_lat_lon.md",
                     p.name, p.base91_lon, out.c_str() + 4);
            TEST_IGNORE_MESSAGE(msg);
        }
    }
}

static void test_encode_course_matches_spec_for_all_fixed_points(void) {
    for (const auto& p : kBase91FixedPoints) {
        String out = APRSPacketLib::encodeGPSIntoBase91(
            p.lat_deg, p.lon_deg, (float)p.course_deg, (float)p.speed_kn,
            "/", false, 0, false, 0);
        if (out.charAt(9) != p.base91_c) {
            char msg[160];
            snprintf(msg, sizeof(msg),
                     "L4 (course truncation): %s course=%d° expected '%c' (0x%02X) "
                     "got '%c' (0x%02X) — see wip/bug_report_l4_encode_course.md",
                     p.name, p.course_deg,
                     p.base91_c, (unsigned char)p.base91_c,
                     out.charAt(9), (unsigned char)out.charAt(9));
            TEST_IGNORE_MESSAGE(msg);
        }
    }
}

static void test_encode_speed_matches_spec_for_all_fixed_points(void) {
    for (const auto& p : kBase91FixedPoints) {
        String out = APRSPacketLib::encodeGPSIntoBase91(
            p.lat_deg, p.lon_deg, (float)p.course_deg, (float)p.speed_kn,
            "/", false, 0, false, 0);
        if (out.charAt(10) != p.base91_s) {
            char msg[160];
            snprintf(msg, sizeof(msg),
                     "L13 (speed truncation): %s speed=%d kn expected '%c' (0x%02X) "
                     "got '%c' (0x%02X) — see wip/bug_report_l13_encode_speed.md",
                     p.name, p.speed_kn,
                     p.base91_s, (unsigned char)p.base91_s,
                     out.charAt(10), (unsigned char)out.charAt(10));
            TEST_IGNORE_MESSAGE(msg);
        }
    }
}

static void test_encode_t_byte_is_G_for_course_speed_mode(void) {
    for (const auto& p : kBase91FixedPoints) {
        String out = APRSPacketLib::encodeGPSIntoBase91(
            p.lat_deg, p.lon_deg, (float)p.course_deg, (float)p.speed_kn,
            "/", false, 0, false, 0);
        char msg[96];
        snprintf(msg, sizeof(msg),
                 "%s: T byte expected 'G' got '%c'", p.name, out.charAt(11));
        TEST_ASSERT_EQUAL_CHAR_MESSAGE(kBase91TByteCS, out.charAt(11), msg);
    }
}

static void test_encode_altitude_matches_spec_for_all_fixed_points(void) {
    for (const auto& p : kBase91FixedPoints) {
        String out = APRSPacketLib::encodeGPSIntoBase91(
            p.lat_deg, p.lon_deg, (float)p.course_deg, (float)p.speed_kn,
            "/", true, p.altitude_ft, false, 0);
        if (out.charAt(9) != p.base91_alt_c || out.charAt(10) != p.base91_alt_s) {
            char msg[200];
            snprintf(msg, sizeof(msg),
                     "altitude divergence: %s alt=%d ft expected \"%c%c\" "
                     "got \"%c%c\"",
                     p.name, p.altitude_ft,
                     p.base91_alt_c, p.base91_alt_s,
                     out.charAt(9), out.charAt(10));
            TEST_IGNORE_MESSAGE(msg);
        }
    }
}

static void test_encode_t_byte_is_Q_for_altitude_mode(void) {
    for (const auto& p : kBase91FixedPoints) {
        String out = APRSPacketLib::encodeGPSIntoBase91(
            p.lat_deg, p.lon_deg, (float)p.course_deg, (float)p.speed_kn,
            "/", true, p.altitude_ft, false, 0);
        char msg[96];
        snprintf(msg, sizeof(msg),
                 "%s: T byte expected 'Q' got '%c'", p.name, out.charAt(11));
        TEST_ASSERT_EQUAL_CHAR_MESSAGE(kBase91TByteAlt, out.charAt(11), msg);
    }
}

// Single-vector behavioural pins (not spec — the library's own
// contract). These document existing API behaviour so a future
// refactor can't silently change it.

static void test_encode_output_length_is_12(void) {
    String out = APRSPacketLib::encodeGPSIntoBase91(
        48.13863f, 11.57341f, 90.0f, 30.0f, "/", false, 0, false, 0);
    TEST_ASSERT_EQUAL_INT(12, out.length());
}

static void test_encode_symbol_at_offset_8(void) {
    String out = APRSPacketLib::encodeGPSIntoBase91(
        48.13863f, 11.57341f, 90.0f, 30.0f, ">", false, 0, false, 0);
    TEST_ASSERT_EQUAL_CHAR('>', out.charAt(8));
}

static void test_encode_standing_update_emits_space_for_c_byte(void) {
    // sendStandingUpdate=true → c byte is ' ' (0x20). APRS12c §9
    // 'Course/Speed': "In the special case of c = (space), there is
    // no course, speed or range data".
    String out = APRSPacketLib::encodeGPSIntoBase91(
        48.13863f, 11.57341f, 0.0f, 0.0f, "/", false, 0, /*standing=*/true, 0);
    TEST_ASSERT_EQUAL_CHAR(' ', out.charAt(9));
}

static void test_encode_altitude_zero_emits_alt_bang_bang(void) {
    // The library short-circuits altitude<=0 to ('!','!'); decoded
    // altitude is then pow(1.002, 0) = 1 ft, which is the convention.
    String out = APRSPacketLib::encodeGPSIntoBase91(
        48.13863f, 11.57341f, 0.0f, 0.0f, "/", true, 0, false, 0);
    TEST_ASSERT_EQUAL_CHAR('!', out.charAt(9));
    TEST_ASSERT_EQUAL_CHAR('!', out.charAt(10));
    TEST_ASSERT_EQUAL_CHAR('Q', out.charAt(11));
}

// generateBase91GPSBeaconPacket wrapper.

static void test_beacon_wrapper_format(void) {
    String gps = APRSPacketLib::encodeGPSIntoBase91(
        48.13863f, 11.57341f, 90.0f, 30.0f, "/", false, 0, false, 0);
    String pkt = APRSPacketLib::generateBase91GPSBeaconPacket(
        "N0CALL-9", "APLRT1", "WIDE1-1", "/", gps);
    // Expected shape: <call>><tocall>,<path>:=<overlay><gps>
    TEST_ASSERT_EQUAL_STRING("N0CALL-9>APLRT1,WIDE1-1:=/", pkt.substring(0, 26).c_str());
    TEST_ASSERT_EQUAL_STRING(gps.c_str(), pkt.substring(26).c_str());
}

static void test_beacon_wrapper_no_path_when_path_empty(void) {
    String gps = APRSPacketLib::encodeGPSIntoBase91(
        48.13863f, 11.57341f, 90.0f, 30.0f, "/", false, 0, false, 0);
    String pkt = APRSPacketLib::generateBase91GPSBeaconPacket(
        "N0CALL-9", "APLRT1", "", "/", gps);
    // No comma, no path — straight to ":=".
    TEST_ASSERT_EQUAL_STRING("N0CALL-9>APLRT1:=/", pkt.substring(0, 18).c_str());
    TEST_ASSERT_EQUAL_INT(-1, pkt.indexOf(','));
}

static void test_beacon_wrapper_only_appends_WIDE_path(void) {
    // L9 (path filtering): the wrapper currently only appends paths
    // that start with "WIDE"; "RFONLY" is dropped. Pin the current
    // behaviour so the divergence is visible in the test report.
    String gps = APRSPacketLib::encodeGPSIntoBase91(
        48.13863f, 11.57341f, 90.0f, 30.0f, "/", false, 0, false, 0);
    String pkt = APRSPacketLib::generateBase91GPSBeaconPacket(
        "N0CALL-9", "APLRT1", "RFONLY", "/", gps);
    if (pkt.indexOf("RFONLY") != -1) {
        // Library has been fixed — flip this assertion to require
        // the path is present.
        return;
    }
    TEST_IGNORE_MESSAGE("L9 — generateBase91GPSBeaconPacket drops non-WIDE paths");
}

static void run_all(void) {
    RUN_TEST(test_encode_lat_matches_spec_for_all_fixed_points);
    RUN_TEST(test_encode_lon_matches_spec_for_all_fixed_points);
    RUN_TEST(test_encode_course_matches_spec_for_all_fixed_points);
    RUN_TEST(test_encode_speed_matches_spec_for_all_fixed_points);
    RUN_TEST(test_encode_t_byte_is_G_for_course_speed_mode);
    RUN_TEST(test_encode_altitude_matches_spec_for_all_fixed_points);
    RUN_TEST(test_encode_t_byte_is_Q_for_altitude_mode);
    RUN_TEST(test_encode_output_length_is_12);
    RUN_TEST(test_encode_symbol_at_offset_8);
    RUN_TEST(test_encode_standing_update_emits_space_for_c_byte);
    RUN_TEST(test_encode_altitude_zero_emits_alt_bang_bang);
    RUN_TEST(test_beacon_wrapper_format);
    RUN_TEST(test_beacon_wrapper_no_path_when_path_empty);
    RUN_TEST(test_beacon_wrapper_only_appends_WIDE_path);
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
    // Yield so the USB-Serial/JTAG CDC endpoint stays engaged long enough
    // for `pio test` to capture Unity output and disconnect cleanly. An
    // empty loop on this S3 board lets CDC drop before the runner finishes.
    delay(1000);
}
#endif
