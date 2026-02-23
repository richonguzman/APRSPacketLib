/*
    APRSPacketLib - Example 05
    Generate Status Packet
    -----------------------------------------------
    Shows how to use generateStatusPacket() to
    broadcast a station status text on the APRS network.

    Status packets are useful for:
        - Announcing firmware version or station info
        - Sharing a short message without GPS data
        - Indicating operating mode or availability

    The status text follows ":>" in the packet.

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
    Serial.println("  APRSPacketLib - Generate Status Packet");
    Serial.println("==============================================");

    // --- 1. Simple station status ---
    String packet = APRSPacketLib::generateStatusPacket(CALLSIGN, TOCALL, PATH, "LoRa APRS Tracker Online");
    Serial.println("\n[1] Simple status:");
    Serial.println(packet);

    // --- 2. Status with firmware version ---
    String packet = APRSPacketLib::generateStatusPacket(CALLSIGN, TOCALL, PATH, "LoRa_APRS_Tracker v2.1 / ESP32");
    Serial.println("\n[2] Firmware version status:");
    Serial.println(packet);

    // --- 3. Status announcing grid square (Maidenhead locator) ---
    String packet = APRSPacketLib::generateStatusPacket(CALLSIGN, TOCALL, PATH, "GF15wc / LoRa iGate 433.775 MHz");
    Serial.println("\n[3] Grid square + frequency:");
    Serial.println(packet);

    // --- 4. Status with QTH info ---
    String packet = APRSPacketLib::generateStatusPacket(CALLSIGN, TOCALL, PATH, "Santiago, Chile - 73!");
    Serial.println("\n[4] QTH info:");
    Serial.println(packet);

    // --- 5. Status with no path (direct, no digipeating) ---
    String packet = APRSPacketLib::generateStatusPacket(CALLSIGN, TOCALL, "", "Direct status - no WIDE path");
    Serial.println("\n[5] Direct status (no path):");
    Serial.println(packet);

    // --- 6. Build base packet manually, then append status text ---
    //  generateBasePacket() is the building block used internally.
    //  You can use it directly if you need to build custom packet types.
    String base   = APRSPacketLib::generateBasePacket(CALLSIGN, TOCALL, PATH);
    String packet = base + ":>Custom appended status text";
    Serial.println("\n[6] Manual base + status append:");
    Serial.println(packet);

    Serial.println("\n==============================================");
    Serial.println("Done.");
}

void loop() {
    // Nothing here — all encoding happens once in setup().
}
