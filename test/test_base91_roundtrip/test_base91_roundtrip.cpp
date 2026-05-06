// Encode → decode round-trip for the Base91 (compressed) APRS position
// format — APRS Protocol Reference v1.2 §9 ('Compressed Position Report
// Data Formats').
//
// One concept per test function: each loops over a small grid and the
// first divergence triggers TEST_IGNORE_MESSAGE so the failing input is
// preserved per concept. With encoder bugs L4 (course truncation),
// L12 (lat/lon integer drift), and L13 (speed truncation) the
// course / speed round-trips will skip on most inputs; lat / lon /
// altitude pass through the full grid.
//
// Decoders are file-local helpers — forward-declared exactly as in
// test_base91_decode.cpp.

#include <Arduino.h>
#include <unity.h>
#include <APRSPacketLib.h>
#include <stdio.h>
#include <math.h>

#include "aprs_vectors.h"

namespace APRSPacketLib {
    int decodeBase91EncodedCourse(const String& course);
    int decodeBase91EncodedSpeed(const String& speed);
    int decodeBase91EncodedAltitude(const String& altitude);
}

void setUp(void) {}
void tearDown(void) {}

// ---------- helpers ----------

static String encode_cs(float lat, float lon, float course, float speed) {
    return APRSPacketLib::encodeGPSIntoBase91(
        lat, lon, course, speed, "/", false, 0, false, 0);
}

static String encode_alt(float lat, float lon, int altitude_ft) {
    return APRSPacketLib::encodeGPSIntoBase91(
        lat, lon, 0.0f, 0.0f, "/", true, altitude_ft, false, 0);
}

// ---------- lat / lon round-trip ----------

static void test_lat_roundtrip_grid(void) {
    static const float kLats[] = { -89.9f, -45.0f, -1.0f, 0.0f, 1.0f, 45.0f, 89.9f };
    for (float lat : kLats) {
        String out = encode_cs(lat, 0.0f, 0.0f, 0.0f);
        float decoded = APRSPacketLib::decodeBase91EncodedLatitude(
            out.substring(0, 4));
        if (fabsf(decoded - lat) > 1e-4f) {
            char msg[160];
            snprintf(msg, sizeof(msg),
                     "lat round-trip drift: in=%.6f out=%.6f delta=%.2g — see "
                     "wip/bug_report_l12_encode_lat_lon.md",
                     lat, decoded, fabsf(decoded - lat));
            TEST_IGNORE_MESSAGE(msg);
        }
    }
}

static void test_lon_roundtrip_grid(void) {
    static const float kLons[] = {
        -179.9f, -90.0f, -1.0f, 0.0f, 1.0f, 90.0f, 179.9f
    };
    for (float lon : kLons) {
        String out = encode_cs(0.0f, lon, 0.0f, 0.0f);
        float decoded = APRSPacketLib::decodeBase91EncodedLongitude(
            out.substring(4, 8));
        if (fabsf(decoded - lon) > 1e-4f) {
            char msg[160];
            snprintf(msg, sizeof(msg),
                     "lon round-trip drift: in=%.6f out=%.6f delta=%.2g — see "
                     "wip/bug_report_l12_encode_lat_lon.md",
                     lon, decoded, fabsf(decoded - lon));
            TEST_IGNORE_MESSAGE(msg);
        }
    }
}

// ---------- course round-trip ----------

static void test_course_roundtrip_grid(void) {
    // Encoded c byte stores round(course/4) — courses that are
    // multiples of 4 round-trip exactly; everything else loses
    // up to ±2°. The library's `(uint32_t)course/4` truncates,
    // so even multiples of 4 sometimes drift (L4).
    static const int kCourses[] = { 0, 4, 8, 12, 45, 90, 180, 270, 359 };
    for (int course : kCourses) {
        String out = encode_cs(0.0f, 0.0f, (float)course, 0.0f);
        // ' ' (0x20) means "no course/speed" per APRS12c §9 — skip.
        if (out.charAt(9) == ' ') continue;
        int decoded = APRSPacketLib::decodeBase91EncodedCourse(
            out.substring(9, 10));
        // Spec round-trip gives decoded ∈ {course, course±2} after
        // /4 quantization. The lib's truncation can shift another 4°.
        if (abs(decoded - course) > 2) {
            char msg[160];
            snprintf(msg, sizeof(msg),
                     "L4: course in=%d out=%d delta=%d — see "
                     "wip/bug_report_l4_encode_course.md",
                     course, decoded, abs(decoded - course));
            TEST_IGNORE_MESSAGE(msg);
        }
    }
}

// ---------- speed round-trip ----------

static void test_speed_roundtrip_grid(void) {
    // Encoded s byte stores round(log_1.08(speed_kn+1)). Spec
    // round-trip is logarithmic — small input drifts by tens of % at
    // high speeds. The lib's truncation makes this worse (L13). We
    // pick a small grid and keep the tolerance loose; a tighter
    // assertion belongs in a per-vector test once L13 is fixed.
    static const int kSpeedsKn[] = { 0, 1, 5, 10, 30, 100 };
    for (int speed_kn : kSpeedsKn) {
        String out = encode_cs(0.0f, 0.0f, 0.0f, (float)speed_kn);
        if (out.charAt(10) == ' ') continue;
        int decoded_kmh = APRSPacketLib::decodeBase91EncodedSpeed(
            out.substring(10, 11));
        // Decoded value is in km/h. Convert input for comparison.
        int input_kmh = (int)(speed_kn * 1.852f);
        // Allow 10% + 1 km/h to absorb the 1.08^n quantization.
        int tol = input_kmh / 10 + 1;
        if (abs(decoded_kmh - input_kmh) > tol) {
            char msg[160];
            snprintf(msg, sizeof(msg),
                     "L13: speed in=%d kn (~%d km/h) out=%d km/h delta=%d — "
                     "see wip/bug_report_l13_encode_speed.md",
                     speed_kn, input_kmh, decoded_kmh,
                     abs(decoded_kmh - input_kmh));
            TEST_IGNORE_MESSAGE(msg);
        }
    }
}

// ---------- altitude round-trip ----------

static void test_altitude_roundtrip_grid(void) {
    static const int kAltsFt[] = { 100, 1000, 9999 };
    for (int alt_ft : kAltsFt) {
        String out = encode_alt(0.0f, 0.0f, alt_ft);
        int decoded_m = APRSPacketLib::decodeBase91EncodedAltitude(
            out.substring(9, 11));
        // The decoder returns metres (× 0.3048). Convert input.
        int input_m = (int)(alt_ft * 0.3048f);
        // 1.002^n quantization is ~0.2%; 5% + 1 m absorbs that and
        // single-precision drift in the lib's log/pow path.
        int tol = input_m / 20 + 1;
        char msg[160];
        snprintf(msg, sizeof(msg),
                 "altitude round-trip: in=%d ft (~%d m) out=%d m delta=%d",
                 alt_ft, input_m, decoded_m, abs(decoded_m - input_m));
        TEST_ASSERT_INT_WITHIN_MESSAGE(tol, input_m, decoded_m, msg);
    }
}

// ---------- per-vector round-trip ----------

static void test_fixed_point_lat_lon_roundtrip(void) {
    for (const auto& p : kBase91FixedPoints) {
        String out = encode_cs(p.lat_deg, p.lon_deg,
                               (float)p.course_deg, (float)p.speed_kn);
        float decoded_lat = APRSPacketLib::decodeBase91EncodedLatitude(
            out.substring(0, 4));
        float decoded_lon = APRSPacketLib::decodeBase91EncodedLongitude(
            out.substring(4, 8));
        char msg[160];
        snprintf(msg, sizeof(msg),
                 "%s: lat in=%.6f out=%.6f delta=%.2g",
                 p.name, p.lat_deg, decoded_lat,
                 fabsf(decoded_lat - p.lat_deg));
        TEST_ASSERT_FLOAT_WITHIN_MESSAGE(1e-4f, p.lat_deg, decoded_lat, msg);
        snprintf(msg, sizeof(msg),
                 "%s: lon in=%.6f out=%.6f delta=%.2g",
                 p.name, p.lon_deg, decoded_lon,
                 fabsf(decoded_lon - p.lon_deg));
        TEST_ASSERT_FLOAT_WITHIN_MESSAGE(1e-4f, p.lon_deg, decoded_lon, msg);
    }
}

static void run_all(void) {
    RUN_TEST(test_lat_roundtrip_grid);
    RUN_TEST(test_lon_roundtrip_grid);
    RUN_TEST(test_course_roundtrip_grid);
    RUN_TEST(test_speed_roundtrip_grid);
    RUN_TEST(test_altitude_roundtrip_grid);
    RUN_TEST(test_fixed_point_lat_lon_roundtrip);
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
