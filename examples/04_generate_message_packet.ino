/*
    APRSPacketLib - Example 04
    Generate Message Packet
    -----------------------------------------------
    Shows how to use generateMessagePacket() to send
    APRS text messages to other stations.

    APRS messages can be:
        - Plain text messages between stations
        - ACK (acknowledgement) responses
        - Commands to APRS services (SMSGTE, EMAIL-2, etc.)

    The addressee field is padded to 9 characters
    automatically by the library.

    Author  : Ricardo Guzman - CA2RXU
    License : GPL-3.0
*/

#include <APRSPacketLib.h>

// --- Station configuration ---
const String CALLSIGN = "AB1CDE-7";
const String TOCALL   = "APLRT1";
const String PATH     = "WIDE1-1";

// -----------------------------------------------

void setup() {
    Serial.begin(115200);
    delay(1000);
    Serial.println("==============================================");
    Serial.println("  APRSPacketLib - Generate Message Packet");
    Serial.println("==============================================");

    // --- 1. Simple message to another station ---
    {
        String packet = APRSPacketLib::generateMessagePacket(
        CALLSIGN,
        TOCALL,
        PATH,
        "XY9ZZZ-5",
        "Hello from AB1CDE-7! 73 de Chile"
        );
        Serial.println("\n[1] Simple message to XY9ZZZ-5:");
        Serial.println(packet);
        // Note: addressee is padded to 9 chars: "XY9ZZZ-5 "
    }

    // --- 2. Message with sequence number (for ACK tracking) ---
    //  Sequence numbers are appended manually to the message text
    //  following the APRS spec: message{001
    {
        String packet = APRSPacketLib::generateMessagePacket(
        CALLSIGN,
        TOCALL,
        PATH,
        "XY9ZZZ-5",
        "Are you receiving me?{001"
        );
        Serial.println("\n[2] Message with sequence number {001:");
        Serial.println(packet);
    }

    // --- 3. ACK response ---
    //  To ACK a received message, echo back the sequence number
    //  as: ack{NNN  (no space before ack)
    {
        String packet = APRSPacketLib::generateMessagePacket(
        CALLSIGN,
        TOCALL,
        PATH,
        "XY9ZZZ-5",
        "ack001"
        );
        Serial.println("\n[3] ACK response for message 001:");
        Serial.println(packet);
    }

    // --- 4. Message to SMSGTE (send SMS via APRS) ---
    {
        String packet = APRSPacketLib::generateMessagePacket(
        CALLSIGN,
        TOCALL,
        PATH,
        "SMSGTE",
        "@+1234567890 Hello from APRS!{010"
        );
        Serial.println("\n[4] SMS via SMSGTE:");
        Serial.println(packet);
    }

    // --- 5. Message to EMAIL-2 service ---
    {
        String packet = APRSPacketLib::generateMessagePacket(
        CALLSIGN,
        TOCALL,
        PATH,
        "EMAIL-2",
        "user@example.com Hello from AB1CDE-7 via APRS!{011"
        );
        Serial.println("\n[5] Email via EMAIL-2:");
        Serial.println(packet);
    }

    // --- 6. No path (direct message, no digipeating) ---
    {
        String packet = APRSPacketLib::generateMessagePacket(
        CALLSIGN,
        TOCALL,
        "",       // empty path = direct, no WIDE
        "XY9ZZZ-5",
        "Direct message, no digipeating{002"
        );
        Serial.println("\n[6] Direct message (no path):");
        Serial.println(packet);
    }

    // --- 7. Short callsign addressee (padded automatically) ---
    {
        String packet = APRSPacketLib::generateMessagePacket(
        CALLSIGN,
        TOCALL,
        PATH,
        "W1AW",      // 4 chars, will be padded to 9: "W1AW     "
        "Short callsign test{003"
        );
        Serial.println("\n[7] Short callsign (W1AW, auto-padded):");
        Serial.println(packet);
    }

    Serial.println("\n==============================================");
    Serial.println("Done.");
}

void loop() {
    // Nothing here — all encoding happens once in setup().
}
