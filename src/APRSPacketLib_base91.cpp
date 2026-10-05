// APRSPacketLib - compressed (Base91) position encode / decode

#include <APRSPacketLib.h>
#include "APRSPacketLib_internal.h"

namespace APRSPacketLib {

    char *ax25_base91enc(char *s, uint8_t n, uint32_t v) {
        for (s += n, *s = '\0'; n; n--) {
            *(--s) = v % 91 + 33;
            v /= 91;
        }
        return(s);
    }

    float applyAmbiguity(float coordinate, int ambiguityLevel) {
        if (ambiguityLevel <= 0) return coordinate;

        int decimals;
        switch (ambiguityLevel) {
            case 1:
                decimals = 3;  // ~110 m
                break;
            case 2:
                decimals = 2;  // ~1.1 km
                break;
            case 3:
                decimals = 1;  // ~11 km
                break;
            default:
                return coordinate; // comportamiento seguro
        }
        float factor = pow(10, decimals);
        return round(coordinate * factor) / factor;
    }

    String encodeGPSIntoBase91(float latitude, float longitude, float course, float speed, const String& symbol, bool sendAltitude, int altitude, bool sendStandingUpdate, int ambiguityLevel) {
        if (ambiguityLevel > 0) {
            latitude = applyAmbiguity(latitude, ambiguityLevel);
            longitude = applyAmbiguity(longitude, ambiguityLevel);
        }
        String encodedData;
        uint32_t aprs_lat, aprs_lon;
        aprs_lat = 900000000 - latitude * 10000000;
        aprs_lat = aprs_lat / 26 - aprs_lat / 2710 + aprs_lat / 15384615;
        aprs_lon = 900000000 + longitude * 10000000 / 2;
        aprs_lon = aprs_lon / 26 - aprs_lon / 2710 + aprs_lon / 15384615;

        String Ns, Ew, helper;
        if (latitude < 0) { Ns = "S"; } else { Ns = "N"; }
        if (latitude < 0) { latitude = -latitude; }

        if (longitude < 0) { Ew = "W"; } else { Ew = "E"; }
        if (longitude < 0) { longitude = -longitude; }

        char helper_base91[] = {"0000\0"};
        int i;
        ax25_base91enc(helper_base91, 4, aprs_lat);
        for (i = 0; i < 4; i++) {
            encodedData += helper_base91[i];
        }
        ax25_base91enc(helper_base91, 4, aprs_lon);
        for (i = 0; i < 4; i++) {
            encodedData += helper_base91[i];
        }

        encodedData += symbol;

        if (sendAltitude) {           // Send Altitude or... (APRS calculates Speed also)
            int Alt1, Alt2;
            if (altitude > 0) {
                double ALT = log(altitude)/log(1.002);
                Alt1 = int(ALT/91);
                Alt2 =(int)ALT%91;
            } else {
                Alt1 = 0;
                Alt2 = 0;
            }
            encodedData += char(Alt1 + 33);
            encodedData += char(Alt2 + 33);
            encodedData += char(0x30 + 33);
        } else {                      // ... just send Course and Speed
            // Wrap to [0, 360) so a caller passing 360 (or drift past it)
            // encodes as 0; APRS12c §9 caps the c byte at numeric 89
            // (encoded course 356), so the modulus on the encoded byte is
            // 90, not 91 — base-91 has 91 digits, but only 90 of them are
            // legal course values.
            float c_wrapped = course - 360.0f * floorf(course / 360.0f);
            ax25_base91enc(helper_base91, 1, (uint32_t)lroundf(c_wrapped / 4.0f) % 90);
            if (sendStandingUpdate) {
                encodedData += " ";
            } else {
                encodedData += helper_base91[0];
            }
            ax25_base91enc(helper_base91, 1, (uint32_t) (log1p(speed)/0.07696));
            encodedData += helper_base91[0];
            encodedData += "\x47";
        }
        return encodedData;
    }

    // APRS101 ch.9: in compressed format the symbol table / overlay is the first byte of the position, and a
    // digit there would be read as the start of an uncompressed latitude -- so overlay digits 0-9 are sent as a-j.
    String compressedSymbolTable(const String& overlay) {
        String symbolTable = overlay;
        if (symbolTable.length() == 1 && symbolTable[0] >= '0' && symbolTable[0] <= '9') symbolTable.setCharAt(0, symbolTable[0] - '0' + 'a');
        return symbolTable;
    }

    String generateBase91GPSBeaconPacket(const String& callsign, const String& tocall, const String& path, const String& overlay, const String& gps) {
        return generateBasePacket(callsign, tocall, path) + ":=" + compressedSymbolTable(overlay) + gps;
    }

    float decodeBase91EncodedLatitude(const String& encodedLatitude) {
        int Y1 = encodedLatitude[0] - 33;
        int Y2 = encodedLatitude[1] - 33;
        int Y3 = encodedLatitude[2] - 33;
        int Y4 = encodedLatitude[3] - 33;
        return (90.0 - (((Y1 * pow(91,3)) + (Y2 * pow(91,2)) + (Y3 * 91) + Y4) / 380926.0));
    }

    float decodeBase91EncodedLongitude(const String& encodedLongitude) {
        int X1 = encodedLongitude[0] - 33;
        int X2 = encodedLongitude[1] - 33;
        int X3 = encodedLongitude[2] - 33;
        int X4 = encodedLongitude[3] - 33;
        return (-180.0 + (((X1 * pow(91,3)) + (X2 * pow(91,2)) + (X3 * 91) + X4) / 190463.0));
    }

    int decodeBase91EncodedCourse(const String& course) {
        return ((int)course[0] - 33) * 4;
    }

    int decodeBase91EncodedSpeed(const String& speed) {
        return (pow(1.08,((int)speed[0] - 33)) - 1) * 1.852;
    }

    int decodeBase91EncodedAltitude(const String& altitude) {
        char cLetter = altitude[0];
        char sLetter = altitude[1];
        int c = static_cast<int>(cLetter);
        int s = static_cast<int>(sLetter);
        return pow(1.002,((c - 33) * 91) + (s-33)) * 0.3048;
    }

}
