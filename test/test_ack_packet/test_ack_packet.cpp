// Coverage for APRSPacketLib::generateAckMessage() and generateAckPacket().
// APRS101 ch.14: an ack copies the received message ID ("{12" -> "ack12"),
// and messages without a message ID are not to be acknowledged. APRS 1.2c
// ch.14 (reply-acks, p.73): for "text{MM}AA" the ack is the exact copy
// "ackMM}AA".

#include <Arduino.h>
#include <unity.h>
#include <APRSPacketLib.h>

void setUp(void) {}
void tearDown(void) {}

static void test_ack_copies_message_id(void) {
    TEST_ASSERT_TRUE_MESSAGE(APRSPacketLib::generateAckMessage("hola{12") == "ack12", "ack must copy the message ID");
}

static void test_reply_ack_is_copied_exactly(void) {
    TEST_ASSERT_TRUE_MESSAGE(APRSPacketLib::generateAckMessage("hola{MM}AA") == "ackMM}AA", "reply-ack ID must be copied exactly (APRS 1.2c)");
}

static void test_no_message_id_gives_empty(void) {
    TEST_ASSERT_TRUE_MESSAGE(APRSPacketLib::generateAckMessage("hola") == "", "message without ID must not be acknowledged");
}

static void test_empty_message_id_gives_empty(void) {
    TEST_ASSERT_TRUE_MESSAGE(APRSPacketLib::generateAckMessage("hola{") == "", "'{' with no ID after it must not be acknowledged");
    TEST_ASSERT_TRUE_MESSAGE(APRSPacketLib::generateAckMessage("hola{   ") == "", "'{' followed only by spaces must not be acknowledged");
}

static void test_trailing_spaces_in_id_are_trimmed(void) {
    TEST_ASSERT_TRUE_MESSAGE(APRSPacketLib::generateAckMessage("hola{12  ") == "ack12", "trailing spaces after the ID must be trimmed");
}

static void test_addressee_and_message_input(void) {
    TEST_ASSERT_TRUE_MESSAGE(APRSPacketLib::generateAckMessage("CA2RXU-10:hola{12") == "ack12", "input with 'ADDRESSEE:' prefix (as iGate/digis pass it) must work");
}

static void test_last_curly_brace_is_used(void) {
    TEST_ASSERT_TRUE_MESSAGE(APRSPacketLib::generateAckMessage("a{b text{34") == "ack34", "the ID is what follows the last '{'");
}

static void test_ack_packet_with_rfonly_path(void) {
    String got = APRSPacketLib::generateAckPacket("CA2RXU-10", "APLRG1", "RFONLY,WIDE1-1", "CA2RXU-7", "CA2RXU-10:hola{12");
    TEST_ASSERT_TRUE_MESSAGE(got == "CA2RXU-10>APLRG1,RFONLY,WIDE1-1::CA2RXU-7 :ack12", "complete ack packet over RF with RFONLY");
}

static void test_ack_packet_for_aprsis(void) {
    String got = APRSPacketLib::generateAPRSISPacket(APRSPacketLib::generateAckPacket("CA2RXU-10", "APLRG1", "", "CA2RXU-7", "hola{12"));
    TEST_ASSERT_TRUE_MESSAGE(got == "CA2RXU-10>APLRG1,TCPIP*::CA2RXU-7 :ack12", "ack packet converted for APRS-IS");
}

static void test_ack_packet_without_id_gives_empty(void) {
    TEST_ASSERT_TRUE_MESSAGE(APRSPacketLib::generateAckPacket("CA2RXU-10", "APLRG1", "WIDE1-1", "CA2RXU-7", "hola") == "", "no ack packet when no ack was requested");
}

static void run_all(void) {
    RUN_TEST(test_ack_copies_message_id);
    RUN_TEST(test_reply_ack_is_copied_exactly);
    RUN_TEST(test_no_message_id_gives_empty);
    RUN_TEST(test_empty_message_id_gives_empty);
    RUN_TEST(test_trailing_spaces_in_id_are_trimmed);
    RUN_TEST(test_addressee_and_message_input);
    RUN_TEST(test_last_curly_brace_is_used);
    RUN_TEST(test_ack_packet_with_rfonly_path);
    RUN_TEST(test_ack_packet_for_aprsis);
    RUN_TEST(test_ack_packet_without_id_gives_empty);
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
