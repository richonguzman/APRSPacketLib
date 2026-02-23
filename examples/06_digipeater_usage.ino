/*
    APRSPacketLib - Example 06
    Digipeater Usage
    -----------------------------------------------
    Shows how to use generateDigipeatedPacket() to
    repeat/relay APRS packets as a digipeater.

    A digipeater receives an APRS packet and retransmits
    it, replacing the consumed path element with its own
    callsign + "*" to indicate the packet passed through.

    Return values:
        - Valid packet string = digipeat this packet
        - "X"                 = do NOT repeat (path doesn't match
                                or packet already digipeated)

    Common paths to listen for:
        "WIDE1-1"  = first hop (trackers)
        "WIDE2-1"  = second hop
        "WIDE2-2"  = two hops remaining

    Author  : Ricardo Guzman - CA2RXU
    License : GPL-3.0
*/

#include <APRSPacketLib.h>

// --- Digipeater station configuration ---
const String MY_CALLSIGN = "AB1CDE-7";
const String LISTEN_PATH = "WIDE1-1";   // path this digi handles

// -----------------------------------------------

// Simulate receiving a packet and decide whether to repeat it
void tryDigipeat(const String& receivedPacket, const String& label) {
    Serial.println("----------------------------------------------");
    Serial.println("Received [" + label + "]:");
    Serial.println("  " + receivedPacket);

    // checkNocall() is a safety guard: never digipeat NOCALL/N0CALL stations
    String sender = receivedPacket.substring(0, receivedPacket.indexOf(">"));
    if (APRSPacketLib::checkNocall(sender)) {
        Serial.println("  → Skipped (NOCALL callsign)");
        return;
    }

    String result = APRSPacketLib::generateDigipeatedPacket(
        receivedPacket,
        MY_CALLSIGN,
        LISTEN_PATH
    );

    if (result == "X") {
        Serial.println("  → NOT repeated (path not found or already used)");
    } else {
        Serial.println("  → REPEAT this packet:");
        Serial.println("    " + result);
    }
}

// -----------------------------------------------

void setup() {
    Serial.begin(115200);
    delay(1000);
    Serial.println("==============================================");
    Serial.println("  APRSPacketLib - Digipeater Usage");
    Serial.println("==============================================");
    Serial.println("Digi callsign : " + MY_CALLSIGN);
    Serial.println("Handles path  : " + LISTEN_PATH);

    // --- 1. Normal packet with matching WIDE1-1 path ---
    tryDigipeat("XY9ZZZ-5>APLRT1,WIDE1-1:=/5L!!<*e7>7P[/Mobile station", "Normal WIDE1-1");

    // --- 2. Two-hop packet: WIDE1-1,WIDE2-1 ---
    //  Digi consumes WIDE1-1, WIDE2-1 remains for the next digi
    tryDigipeat("XY9ZZZ-5>APLRT1,WIDE1-1,WIDE2-1:=/5L!!<*e7>7P[/Two-hop packet", "WIDE1-1,WIDE2-1");

    // --- 3. Packet already digipeated (has a * in path) ---
    //  Should NOT be repeated again by this digi
    tryDigipeat("XY9ZZZ-5>APLRT1,AB1CDE-7*,WIDE2-1:=/5L!!<*e7>7P[/Already digi'd", "Already digipeated");

    // --- 4. Packet with different path (WIDE2-2) - won't match WIDE1-1 ---
    tryDigipeat("XY9ZZZ-5>APLRT1,WIDE2-2:=/5L!!<*e7>7P[/WIDE2-2 only", "WIDE2-2 (no match)");

    // --- 5. Third-party packet (:} format) ---
    //  Some iGates re-inject packets as third-party frames
    tryDigipeat("AB1CDE-7>APLRT1,WIDE1-1:}XY9ZZZ-5>APRS,WIDE1-1:=/5L!!<*e7>7P[/", "Third-party packet");

    // --- 6. NOCALL station - should be skipped ---
    tryDigipeat("NOCALL-9>APLRT1,WIDE1-1:=/5L!!<*e7>7P[/Bad callsign", "NOCALL callsign");

    // --- 7. N0CALL variant - also skipped ---
    tryDigipeat("N0CALL-3>APLRT1,WIDE1-1:>/Standing by", "N0CALL callsign");

    Serial.println("==============================================");
    Serial.println("Done.");
}

void loop() {
    // In a real digipeater, received packets from LoRa would be
    // passed to generateDigipeatedPacket() here, and valid results
    // would be retransmitted via LoRa.
    //
    // Pseudocode:
    //
    //   if (LoRa.receive()) {
    //      String raw = LoRa.readString();
    //      String toRepeat = APRSPacketLib::generateDigipeatedPacket(raw, MY_CALLSIGN, LISTEN_PATH);
    //      if (toRepeat != "X") {
    //          LoRa.transmit(toRepeat);
    //      }
    //  }
}
