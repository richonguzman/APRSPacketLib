/*
    APRSPacketLib - Example 01
    Decode Received Packet
    -----------------------------------------------
    Shows how to use processReceivedPacket() to decode
    any incoming APRS packet and read all its fields.

    Packet types decoded:
        0 = GPS position
        1 = Message
        2 = Status
        3 = Telemetry
        4 = Mic-E
        5 = Object

    Author  : Ricardo Guzman - CA2RXU
    License : GPL-3.0
*/

#include <APRSPacketLib.h>

// --- Sample raw APRS packets to decode ---

// GPS position (Base91 encoded)
const String gpsBase91Packet = "AB1CDE-7>APLRT1,WIDE1-1:=/5L!!<*e7>7P[/Testing Base91 beacon";

// GPS position (Degrees and Decimal Minutes)
const String gpsDDMPacket = "AB1CDE-7>APLRT1,WIDE1-1:=3347.12N/07024.56W>180/045/A=000120 Testing DDM";

// Message packet
const String messagePacket = "AB1CDE-7>APLRT1,WIDE1-1:::XY9ZZZ-5 :Hello from AB1CDE!{001";

// Status packet
const String statusPacket = "AB1CDE-7>APLRT1,WIDE1-1:>Running APRSPacketLib v1.0";

// Telemetry packet
const String telemetryPacket = "AB1CDE-7>APLRT1,WIDE1-1:T#123,045,030,000,000,000,00000000";

// Object packet
const String objectPacket = "AB1CDE-7>APLRT1,WIDE1-1:;IGATE    *111111z3347.12N/07024.56WrIGate object";

// Mic-E packet
const String micePacket = "AB1CDE-7>S3PUUX,WIDE1-1:`(7(l ,/`Testing Mic-E";

// --- Helper: print packet type as string ---
String packetTypeName(int type) {
    switch (type) {
        case 0: return "GPS Position";
        case 1: return "Message";
        case 2: return "Status";
        case 3: return "Telemetry";
        case 4: return "Mic-E";
        case 5: return "Object";
        default: return "Unknown";
    }
}

// --- Helper: decode and print a single packet ---
void decodeAndPrint(const String& raw, const String& label) {
    Serial.println("----------------------------------------------");
    Serial.println("Decoding: " + label);
    Serial.println("Raw: " + raw);

    // rssi, snr, freqError are normally provided by your LoRa radio.
    // Using mock values here for demonstration.
    APRSPacket p = APRSPacketLib::processReceivedPacket(raw, -85, 7.5, 120);

    Serial.println("  Type      : " + packetTypeName(p.type) + " (" + String(p.type) + ")");
    Serial.println("  Sender    : " + p.sender);
    Serial.println("  Tocall    : " + p.tocall);
    Serial.println("  Path      : " + p.path);
    Serial.println("  Payload   : " + p.payload);

    if (p.type == 0 || p.type == 4) {
        Serial.println("  Latitude  : " + String(p.latitude, 6));
        Serial.println("  Longitude : " + String(p.longitude, 6));
        Serial.println("  Course    : " + String(p.course) + " deg");
        Serial.println("  Speed     : " + String(p.speed) + " km/h");
        Serial.println("  Altitude  : " + String(p.altitude) + " m");
        Serial.println("  Symbol    : " + p.symbol);
        Serial.println("  Overlay   : " + p.overlay);
    }

    if (p.type == 1) {
        Serial.println("  Addressee : " + p.addressee);
    }

    if (p.type == 4) {
        Serial.println("  Mic-E type: " + p.miceType);
    }

    if (p.header != "") {
        Serial.println("  3rd-party header: " + p.header);
    }

    Serial.println("  RSSI      : " + String(p.rssi) + " dBm");
    Serial.println("  SNR       : " + String(p.snr) + " dB");
    Serial.println("  FreqError : " + String(p.freqError) + " Hz");
}

// -----------------------------------------------

void setup() {
    Serial.begin(115200);
    delay(1000);
    Serial.println("==============================================");
    Serial.println("  APRSPacketLib - Decode Received Packet");
    Serial.println("==============================================");

    decodeAndPrint(gpsBase91Packet, "GPS Base91");
    decodeAndPrint(gpsDDMPacket,    "GPS DDM");
    decodeAndPrint(messagePacket,   "Message");
    decodeAndPrint(statusPacket,    "Status");
    decodeAndPrint(telemetryPacket, "Telemetry");
    decodeAndPrint(objectPacket,    "Object");
    decodeAndPrint(micePacket,      "Mic-E");

    Serial.println("==============================================");
    Serial.println("Done.");
}

void loop() {
    // Nothing here — all decoding happens once in setup().
}
