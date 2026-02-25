/*
    APRSPacketLib - Example 03
    Encode Mic-E GPS Beacon
    -----------------------------------------------
    Shows how to use generateMiceGPSBeaconPacket() to
    build a Mic-E encoded APRS position beacon.

    Mic-E encodes position, course, speed, altitude
    and a message type in a very compact format.

    Mic-E message types (3-bit string):
        "111" = Emergency
        "110" = Priority
        "101" = Special
        "100" = Committed
        "011" = Returning
        "010" = In Service
        "001" = En Route
        "000" = Off Duty

    Use validateMicE() to check a type string is valid
    before encoding.

    Author  : Ricardo Guzman - CA2RXU
    License : GPL-3.0
*/

#include <APRSPacketLib.h>

// --- Station configuration ---
const String CALLSIGN = "AB1CDE-7";
const String PATH     = "WIDE1-1";

// APRS symbols:
//   ">" = car    overlay "/"
//   "[" = hiker  overlay "/"
//   "O" = balloon overlay "/"
//   "k" = truck  overlay "/"
const String SYMBOL  = ">";
const String OVERLAY = "/";

// --- GPS data (replace with real GPS values in production) ---
float latitude  =  33.7854;   //  N
float longitude = -70.6239;   //  W
float course    = 270.0;      // degrees
float speed     = 20.0;       // knots/h
int   altitude  = 750;        // feet

// -----------------------------------------------

// Helper: print Mic-E type description
String miceTypeLabel(const String& t) {
    if (t == "111") return "Emergency";
    if (t == "110") return "Priority";
    if (t == "101") return "Special";
    if (t == "100") return "Committed";
    if (t == "011") return "Returning";
    if (t == "010") return "In Service";
    if (t == "001") return "En Route";
    if (t == "000") return "Off Duty";
    return "Invalid";
}

void encodeMiceAndPrint(const String& msgType) {
    // Always validate the Mic-E type before using it
    if (!APRSPacketLib::validateMicE(msgType)) {
        Serial.println("  [ERROR] Invalid Mic-E type: " + msgType);
        return;
    }

    String packet = APRSPacketLib::generateMiceGPSBeaconPacket(
        msgType,
        CALLSIGN,
        SYMBOL,
        OVERLAY,
        PATH,
        latitude,
        longitude,
        course,
        speed,
        altitude
        // ambiguityLevel defaults to 0 (exact position)
    );

    Serial.println("\n[" + msgType + "] " + miceTypeLabel(msgType) + ":");
    Serial.println(packet);
}

// -----------------------------------------------

void setup() {
    Serial.begin(115200);
    delay(1000);
    Serial.println("==============================================");
    Serial.println("  APRSPacketLib - Encode Mic-E Beacon");
    Serial.println("==============================================");

    // --- Show all 8 Mic-E message types ---
    encodeMiceAndPrint("111");  // Emergency
    encodeMiceAndPrint("110");  // Priority
    encodeMiceAndPrint("101");  // Special
    encodeMiceAndPrint("100");  // Committed
    encodeMiceAndPrint("011");  // Returning
    encodeMiceAndPrint("010");  // In Service
    encodeMiceAndPrint("001");  // En Route
    encodeMiceAndPrint("000");  // Off Duty

    // --- With ambiguity (privacy / fuzzing) ---
    Serial.println("\n--- Ambiguity Level 1 (~110 m) ---");
    {
        String packet = APRSPacketLib::generateMiceGPSBeaconPacket(
        "001",         // En Route
        CALLSIGN,
        SYMBOL,
        OVERLAY,
        PATH,
        latitude,
        longitude,
        course,
        speed,
        altitude,
        1              // ambiguityLevel = 1
        );
        Serial.println(packet);
    }

    // --- No path (direct, no digipeating) ---
    Serial.println("\n--- No path (direct) ---");
    {
        String packet = APRSPacketLib::generateMiceGPSBeaconPacket(
        "000",
        CALLSIGN,
        SYMBOL,
        OVERLAY,
        "",            // empty path = no digipeating
        latitude,
        longitude,
        course,
        speed,
        altitude
        );
        Serial.println(packet);
    }

    // --- Validate an invalid type ---
    Serial.println("\n--- Validate invalid Mic-E type '999' ---");
    {
        bool valid = APRSPacketLib::validateMicE("999");
        Serial.println(valid ? "Valid" : "Invalid (as expected)");
    }

    Serial.println("\n==============================================");
    Serial.println("Done.");
}

void loop() {
    // Nothing here — all encoding happens once in setup().
}
