/*
    APRSPacketLib - Example 02
    Encode GPS Beacon (Base91)
    -----------------------------------------------
    Shows how to use encodeGPSIntoBase91() and
    generateBase91GPSBeaconPacket() to build a
    complete APRS position beacon packet.

    Covers:
        - Basic beacon with course and speed
        - Beacon with altitude instead of course/speed
        - Standing update (no movement)
        - Ambiguity levels (privacy / position fuzzing)

    Author  : Ricardo Guzman - CA2RXU
    License : GPL-3.0
*/

#include <APRSPacketLib.h>

// --- Station configuration ---
const String CALLSIGN = "AB1CDE-7";
const String TOCALL   = "APLRT1";
const String PATH     = "WIDE1-1";
const String OVERLAY  = "/";    // '/' = standard table, '\' = alternate table

// APRS symbols (see APRS symbol reference):
//   '>' = car
//   '[' = jogger / hiker
//   '-' = house
//   'O' = balloon
//   'k' = truck
const String SYMBOL   = ">";    // car

// --- GPS data (replace with real GPS values in production) ---
float latitude  =  33.7854;   //  33.7854 N
float longitude = -70.6239;   // -70.6239 W (negative = West)
float course    = 180.0;      // degrees (0-359)
float speed     = 10.0;       // knots/hr
int   altitude  = 520;        // feet

// -----------------------------------------------

void setup() {
    Serial.begin(115200);
    delay(1000);
    Serial.println("==============================================");
    Serial.println("  APRSPacketLib - Encode GPS Beacon Base91");
    Serial.println("==============================================");

    // --- 1. Basic beacon: course + speed ---
    {
        String gpsData = APRSPacketLib::encodeGPSIntoBase91(
        latitude, longitude,
        course, speed,
        SYMBOL,
        false,   // sendAltitude = false  → send course+speed
        0,       // altitude (unused)
        false,   // sendStandingUpdate
        0        // ambiguityLevel (0 = exact position)
        );
        String packet = APRSPacketLib::generateBase91GPSBeaconPacket(
        CALLSIGN, TOCALL, PATH, OVERLAY, gpsData
        );
        Serial.println("\n[1] Course + Speed beacon:");
        Serial.println(packet);
    }

    // --- 2. Beacon with altitude ---
    {
        String gpsData = APRSPacketLib::encodeGPSIntoBase91(
        latitude, longitude,
        0, 0,
        SYMBOL,
        true,      // sendAltitude = true
        altitude,  // altitude in feet
        false,
        0
        );
        String packet = APRSPacketLib::generateBase91GPSBeaconPacket(
        CALLSIGN, TOCALL, PATH, OVERLAY, gpsData
        );
        Serial.println("\n[2] Altitude beacon:");
        Serial.println(packet);
    }

    // --- 3. Standing update (no movement, course/speed = 0) ---
    {
        String gpsData = APRSPacketLib::encodeGPSIntoBase91(
        latitude, longitude,
        0, 0,
        SYMBOL,
        false,
        0,
        true,    // sendStandingUpdate = true → encodes a space for course byte
        0
        );
        String packet = APRSPacketLib::generateBase91GPSBeaconPacket(
        CALLSIGN, TOCALL, PATH, OVERLAY, gpsData
        );
        Serial.println("\n[3] Standing update (no movement):");
        Serial.println(packet);
    }

    // --- 4. Ambiguity level 1 (~110 m precision) ---
    {
        String gpsData = APRSPacketLib::encodeGPSIntoBase91(
        latitude, longitude,
        course, speed,
        SYMBOL,
        false, 0, false,
        1   // ambiguityLevel 1 = ~110 m
        );
        String packet = APRSPacketLib::generateBase91GPSBeaconPacket(
        CALLSIGN, TOCALL, PATH, OVERLAY, gpsData
        );
        Serial.println("\n[4] Ambiguity level 1 (~110 m):");
        Serial.println(packet);
    }

    // --- 5. Ambiguity level 2 (~1.1 km precision) ---
    {
        String gpsData = APRSPacketLib::encodeGPSIntoBase91(
        latitude, longitude,
        course, speed,
        SYMBOL,
        false, 0, false,
        2   // ambiguityLevel 2 = ~1.1 km
        );
        String packet = APRSPacketLib::generateBase91GPSBeaconPacket(
        CALLSIGN, TOCALL, PATH, OVERLAY, gpsData
        );
        Serial.println("\n[5] Ambiguity level 2 (~1.1 km):");
        Serial.println(packet);
    }

    // --- 6. Ambiguity level 3 (~11 km precision) ---
    {
        String gpsData = APRSPacketLib::encodeGPSIntoBase91(
        latitude, longitude,
        course, speed,
        SYMBOL,
        false, 0, false,
        3   // ambiguityLevel 3 = ~11 km
        );
        String packet = APRSPacketLib::generateBase91GPSBeaconPacket(
        CALLSIGN, TOCALL, PATH, OVERLAY, gpsData
        );
        Serial.println("\n[6] Ambiguity level 3 (~11 km):");
        Serial.println(packet);
    }

    // --- 7. No path (direct, no digipeating) ---
    {
        String gpsData = APRSPacketLib::encodeGPSIntoBase91(
        latitude, longitude,
        course, speed,
        SYMBOL
        );
        String packet = APRSPacketLib::generateBase91GPSBeaconPacket(
        CALLSIGN, TOCALL,
        "",     // empty path = no digipeating
        OVERLAY, gpsData
        );
        Serial.println("\n[7] No path (direct):");
        Serial.println(packet);
    }

    Serial.println("\n==============================================");
    Serial.println("Done.");
}

void loop() {
    // Nothing here — all encoding happens once in setup().
}
