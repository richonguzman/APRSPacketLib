#include <Arduino.h>
#include <unity.h>
#include <APRSPacketLib.h>

void setUp(void) {}
void tearDown(void) {}

// APRS Protocol Reference v1.0.1, Appendix 1 (compressed position c/s slot):
//   c = round(course_deg / 4) + 33
//   s = round(log(speed_kn + 1) / log(1.08)) + 33
//
// course = 20°, speed = 10 kn → c = 5+33 = '&', s = 31+33 = '@'.
// Both inputs sit inside the (narrow) zone where the lib's
// (uint32_t)-truncation matches the spec's round(): 20 is a multiple of 4
// (so trunc == round) and log(11)/0.07696 ≈ 31.158 (frac < .5, so trunc ==
// round). Other inputs (course=359°, speed=99 kn, …) diverge — see plan §5
// for that follow-up work.
//
// Slice [9..10] avoids asserting on the lat/lon prefix, which goes through
// an integer-arithmetic pipeline that is off-by-one from
// round(380926·(90−lat)) for many inputs (a separate finding).
static void test_compressed_cs_slot_known_vector(void) {
    String out = APRSPacketLib::encodeGPSIntoBase91(
        /*latitude=*/48.13863f, /*longitude=*/11.57341f,
        /*course=*/20.0f, /*speed=*/10.0f,
        /*symbol=*/"/",
        /*sendAltitude=*/false, /*altitude=*/0,
        /*sendStandingUpdate=*/false,
        /*ambiguityLevel=*/0);

    TEST_ASSERT_GREATER_OR_EQUAL(11, out.length());
    TEST_ASSERT_EQUAL_CHAR('&', out.charAt(9));
    TEST_ASSERT_EQUAL_CHAR('@', out.charAt(10));
}

#ifdef NATIVE_TEST_BUILD
int main(int /*argc*/, char ** /*argv*/) {
    UNITY_BEGIN();
    RUN_TEST(test_compressed_cs_slot_known_vector);
    return UNITY_END();
}
#else
void setup(void) {
    Serial.begin(115200);
    delay(2000);
    UNITY_BEGIN();
    RUN_TEST(test_compressed_cs_slot_known_vector);
    UNITY_END();
}

void loop(void) {
    // Yield so the USB-Serial/JTAG CDC endpoint stays engaged long enough
    // for `pio test` to capture Unity output and disconnect cleanly. An
    // empty loop on this S3 board lets CDC drop before the runner finishes.
    delay(1000);
}
#endif
