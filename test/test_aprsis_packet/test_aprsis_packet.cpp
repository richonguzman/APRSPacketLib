// Coverage for APRSPacketLib::generateAPRSISPacket() -- converts a station's
// own RF packet into its APRS-IS version: the RF path is replaced by exactly
// "TCPIP*" (aprs-is.net client rules), no q construct is added, and the
// payload after the first ':' must come out byte-for-byte unchanged.

#include <Arduino.h>
#include <unity.h>
#include <APRSPacketLib.h>

void setUp(void) {}
void tearDown(void) {}

static void test_rf_path_is_replaced_by_tcpip(void) {
    String got = APRSPacketLib::generateAPRSISPacket("CA2RXU-10>APLRG1,WIDE1-1:=L5Cr!Q>Wb# !GLoRa iGate");
    TEST_ASSERT_TRUE_MESSAGE(got == "CA2RXU-10>APLRG1,TCPIP*:=L5Cr!Q>Wb# !GLoRa iGate", "RF path must be replaced by TCPIP*");
}

static void test_multihop_path_is_fully_replaced(void) {
    String got = APRSPacketLib::generateAPRSISPacket("CA2RXU-10>APLRG1,WIDE1-1,WIDE2-1:>status text");
    TEST_ASSERT_TRUE_MESSAGE(got == "CA2RXU-10>APLRG1,TCPIP*:>status text", "every RF hop must be dropped, not only the first one");
}

static void test_packet_without_path_gets_tcpip(void) {
    String got = APRSPacketLib::generateAPRSISPacket("CA2RXU-10>APLRG1:>status text");
    TEST_ASSERT_TRUE_MESSAGE(got == "CA2RXU-10>APLRG1,TCPIP*:>status text", "packet without path must still get TCPIP*");
}

static void test_payload_with_colons_and_commas_is_untouched(void) {
    String got = APRSPacketLib::generateAPRSISPacket("CA2RXU-10>APLRG1,WIDE1-1::CA2RXU-10:EQNS.0,0.01,0,0,0.01,0");
    TEST_ASSERT_TRUE_MESSAGE(got == "CA2RXU-10>APLRG1,TCPIP*::CA2RXU-10:EQNS.0,0.01,0,0,0.01,0", "only the header may change, ':' and ',' inside the payload must survive");
}

static void test_base91_payload_with_colon_is_untouched(void) {
    String got = APRSPacketLib::generateAPRSISPacket("CA2RXU-10>APLRG1,WIDE1-1:=L5C:!Q,Wb# !G");
    TEST_ASSERT_TRUE_MESSAGE(got == "CA2RXU-10>APLRG1,TCPIP*:=L5C:!Q,Wb# !G", "Base91 bytes may contain ':' and ',' and must not be treated as header");
}

static void test_mice_packet_header(void) {
    String got = APRSPacketLib::generateAPRSISPacket("CA2RXU-7>T3PRVY,WIDE1-1:`(_fn\"Oj/]");
    TEST_ASSERT_TRUE_MESSAGE(got == "CA2RXU-7>T3PRVY,TCPIP*:`(_fn\"Oj/]", "Mic-E tocall (encoded latitude) must be kept as is");
}

static void test_input_without_colon_is_returned_unchanged(void) {
    String got = APRSPacketLib::generateAPRSISPacket("CA2RXU-10>APLRG1,WIDE1-1");
    TEST_ASSERT_TRUE_MESSAGE(got == "CA2RXU-10>APLRG1,WIDE1-1", "malformed input (no ':') must be returned unchanged");
}

static void test_status_and_beacon_generators_roundtrip(void) {
    String status = APRSPacketLib::generateStatusPacket("CA2RXU-10", "APLRG1", "WIDE1-1", "LoRa APRS iGate");
    TEST_ASSERT_TRUE_MESSAGE(APRSPacketLib::generateAPRSISPacket(status) == "CA2RXU-10>APLRG1,TCPIP*:>LoRa APRS iGate", "status built by the library must convert cleanly");
    String message = APRSPacketLib::generateMessagePacket("CA2RXU-10", "APLRG1", "WIDE1-1", "CA2RXU-10", "PARM.V_Batt");
    TEST_ASSERT_TRUE_MESSAGE(APRSPacketLib::generateAPRSISPacket(message) == "CA2RXU-10>APLRG1,TCPIP*::CA2RXU-10:PARM.V_Batt", "telemetry message built by the library must convert cleanly");
}

static void run_all(void) {
    RUN_TEST(test_rf_path_is_replaced_by_tcpip);
    RUN_TEST(test_multihop_path_is_fully_replaced);
    RUN_TEST(test_packet_without_path_gets_tcpip);
    RUN_TEST(test_payload_with_colons_and_commas_is_untouched);
    RUN_TEST(test_base91_payload_with_colon_is_untouched);
    RUN_TEST(test_mice_packet_header);
    RUN_TEST(test_input_without_colon_is_returned_unchanged);
    RUN_TEST(test_status_and_beacon_generators_roundtrip);
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
