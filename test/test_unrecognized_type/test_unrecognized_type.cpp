// Regression coverage for the type=6 catch-all in processReceivedPacket()
// (src/APRSPacketLib.cpp). Before this, a packet matching none of the
// known DTI patterns left aprsPacket.type unassigned -- undefined
// behavior, since it's a plain int with no default. A caller checking
// e.g. "type == 0" could match by pure coincidence of leftover stack
// memory. Now every packet gets an explicit type (0-6), and unrecognized
// ones carry the full, uncut original packet in .payload.

#include <Arduino.h>
#include <unity.h>
#include <APRSPacketLib.h>

void setUp(void) {}
void tearDown(void) {}

static void test_unrecognized_packet_gets_type_6(void) {
    // No '!','=','@','::','>',':T#','`',''',';' anywhere in the info field.
    String packet = "N0CALL>APRS,WIDE1-1:this is not a recognized DTI";
    APRSPacket got = APRSPacketLib::processReceivedPacket(packet, 0, 0, 0);
    TEST_ASSERT_EQUAL_INT_MESSAGE(6, got.type, "unrecognized packet should get type 6");
}

static void test_unrecognized_packet_payload_is_full_uncut_packet(void) {
    String packet = "N0CALL>APRS,WIDE1-1:this is not a recognized DTI";
    APRSPacket got = APRSPacketLib::processReceivedPacket(packet, 0, 0, 0);
    TEST_ASSERT_TRUE_MESSAGE(got.payload == packet, "payload should be the complete, uncut original packet");
}

static void test_unrecognized_packet_position_fields_are_reset(void) {
    String packet = "N0CALL>APRS,WIDE1-1:this is not a recognized DTI";
    APRSPacket got = APRSPacketLib::processReceivedPacket(packet, 0, 0, 0);
    TEST_ASSERT_EQUAL_FLOAT_MESSAGE(0.0f, got.latitude, "latitude should be reset for an unrecognized packet");
    TEST_ASSERT_EQUAL_FLOAT_MESSAGE(0.0f, got.longitude, "longitude should be reset for an unrecognized packet");
}

static void test_recognized_types_still_unaffected(void) {
    // Sanity: a normal status packet must still resolve to type 2, not 6.
    String packet = "N0CALL>APRS,WIDE1-1:>Test status";
    APRSPacket got = APRSPacketLib::processReceivedPacket(packet, 0, 0, 0);
    TEST_ASSERT_EQUAL_INT_MESSAGE(2, got.type, "a recognized status packet must not fall into the catch-all");
}

static void run_all(void) {
    RUN_TEST(test_unrecognized_packet_gets_type_6);
    RUN_TEST(test_unrecognized_packet_payload_is_full_uncut_packet);
    RUN_TEST(test_unrecognized_packet_position_fields_are_reset);
    RUN_TEST(test_recognized_types_still_unaffected);
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
