// APRSPacketLib - Mic-E encode / decode

#include <APRSPacketLib.h>
#include "APRSPacketLib_internal.h"

namespace APRSPacketLib {

    bool validateMicE(const String& micE) {
        return micE == "111" || micE == "110" || micE == "101" || micE == "100" || micE == "011" || micE == "010" || micE == "001" || micE == "000";
    }

    String decodeMiceMsgType(const String& tocall) {
        char miceType[4];
        for (int i = 0; i < 3; i++) {
            miceType[i] = (tocall[i] > '9') ? '1' : '0';
        }
        miceType[3] = '\0';
        return String(miceType);
    }

    int decodeMiceSpeed(char char3, char char4) {
        int temp = int(char3);
        if (temp > 107) {
            temp -= 80;
        }
        int SP28 = (temp - 28) * 10;
        int DC28 = (int(char4) - 28) / 10;
        return (SP28 + DC28) * 1.852;
    }

    int decodeMiceCourse(char char4, char char5) {
        int DC28 = (int(char4) - 28)/10;
        int temp = (int(char4) - 28) - (DC28 * 10);
        int SE28 = int(char5) - 28;
        int course = ((temp - 4) * 100) + SE28;
        return course;
    }

    int decodeMiceAltitude(const String& informationField) {
        int altitude    = 0;
        String temp     = "";
        int rightCurlyBraceIndex = informationField.indexOf("}");
        if (informationField.indexOf("`") == 8 && rightCurlyBraceIndex == 12) {
            temp = informationField.substring(9, 12);
        } else if (rightCurlyBraceIndex == 11) {
            temp = informationField.substring(8, 11);
        }
        if (temp.length() != 0) {
            int a = int(temp[0]) - 33;
            int b = int(temp[1]) - 33;
            int c = int(temp[2]) - 33;
            altitude = ((a * pow(91,2)) + (b * 91) + c) - 10000;
        }
        return altitude;
    }

    float decodeMiceLatitude(const String& destinationField) {
        String gpsLat;
        String northSouth = "S";
        for (int i = 0; i < 6; i++) {
            char currentChar = destinationField[i];
            if (currentChar > '9') {
                gpsLat += char(int(currentChar) - 32);
            } else {
                gpsLat += char(int(currentChar));
            }
            if (i == 3) {
                gpsLat += ".";
                if (currentChar > '9') northSouth = "N";        // ???? para todos?
            }
        }
        gpsLat += northSouth;
        return gpsDegreesToDecimalLatitude(gpsLat);
    }

    float decodeMiceLongitude(const String& destinationField, const String& informationField) {
        bool offset = false;
        String westEast = "E";
        if (destinationField[4] > '9') offset = true;
        if (destinationField[5] > '9') westEast = "W";

        String temp;
        int d28 = (int)informationField[0] - 28;
        if (offset) d28 += 100;
        // APRS101 §10 (Mic-E Longitude Degrees Encoding): the +100 offset
        // above can push the encoded degree into two reserved 10-degree
        // bands that must be folded back down -- 180..189 actually means
        // 100..109, and 190..199 actually means 0..9. Without this, e.g. a
        // station at 1.37E decodes as 191.37E instead.
        if (d28 >= 180 && d28 <= 189) {
            d28 -= 80;
        } else if (d28 >= 190 && d28 <= 199) {
            d28 -= 190;
        }
        temp = String(d28);
        for (int i = temp.length(); i < 3; i++) {
            temp = '0' + temp;
        }
        String longitudeString = temp;

        int m28 = (int)informationField[1] - 28;
        if (m28 >= 60) m28 -= 60;
        temp = String(m28);
        for (int i = temp.length(); i < 2; i++) {
            temp = '0' + temp;
        }
        longitudeString += temp;
        longitudeString += ".";

        int h28 = (int)informationField[2] - 28;
        temp = String(h28);
        for (int i = temp.length(); i < 2; i++) {
            temp = '0' + temp;
        }
        longitudeString += temp;
        longitudeString += westEast;
        return gpsDegreesToDecimalLongitude(longitudeString) ;
    }

    void encodeMiceAltitude(uint8_t *buf, uint32_t alt_m) {
        if (alt_m > 40000) alt_m = 0;
        uint32_t altoff = alt_m + 10000;
        buf[0] = (altoff/8281) + 33;
        altoff = altoff%8281;
        buf[1] = (altoff/91) + 33;
        buf[2] = (altoff%91) + 33;
        buf[3] = '}';
    }

    void encodeMiceCourseSpeed(uint8_t *buf, uint32_t speed_kt, uint32_t course_deg) {
        uint32_t DC28, SE28; //three bytes are output

        uint32_t SP28 = 107;
        uint32_t ten = speed_kt / 10;
        if (ten <= 19) {
            SP28 = ten + 108;
        } else if (ten <= 79) {
            SP28 = ten + 28;
        }
        buf[0] = SP28;

        if (course_deg == 0) {
            course_deg = 360;
        } else if (course_deg >= 360) {
            course_deg = 0;
        }
        uint32_t course_hun = course_deg/100;
        DC28    = (speed_kt-ten * 10) * 10 + course_hun + 32;
        buf[1]  = DC28;

        SE28    = (course_deg - course_hun * 100) + 28;
        buf[2]  = SE28;
    }

    void encodeMiceLongitude(uint8_t *buf, gpsLongitudeStruct *lon) {
        uint32_t deg = lon->degrees;
        uint32_t d28 = 28 + (deg - 100);    // degrees
        if (deg <= 9) {
            d28 = 118 + deg;
        } else if (deg <= 99) {
            d28 = 28 + deg;
        } else if (deg <= 109) {
            d28 = 8 + deg;
        }
        buf[0] = d28;

        uint32_t min = lon->minutes;
        uint32_t m28 = 28 + min;            // minutes
        if (min <= 9) m28 = 88 + min;
        buf[1] = m28;

        uint32_t h28 = 28 + lon->minuteHundredths;
        buf[2] = h28;
    }

    void encodeMiceDestinationField(const String& msgType, uint8_t *buf, const gpsLatitudeStruct *lat, const gpsLongitudeStruct *lon) {
        uint32_t temp;
        temp = lat->degrees / 10;             // degrees
        buf[0] = (temp + 0x30);
        if (msgType[0] == '1') buf[0] = buf[0] + 0x20;
        buf[1] = (lat->degrees - temp * 10 + 0x30);
        if (msgType[1] == '1') buf[1] = buf[1] + 0x20;

        temp = lat->minutes/10;             // minutes
        buf[2] = (temp + 0x30);
        if (msgType[2] == '1') buf[2] = buf[2] + 0x20;
        buf[3] = (lat->minutes - temp * 10 + 0x30) + (lat->north ? 0x20 : 0);               // North validation

        temp   = lat->minuteHundredths/10;  // minute hundredths
        buf[4] = (temp + 0x30) + ((lon->degrees >= 100 || lon->degrees <= 9) ? 0x20 : 0);   // Longitude Offset
        buf[5] = (lat->minuteHundredths - temp * 10 + 0x30) + (!lon->east ? 0x20 : 0);      // West validation
    }

    gpsLatitudeStruct gpsDecimalToDegreesMiceLatitude(float latitude) {
        gpsLatitudeStruct miceLatitudeStruct;
        String lat = gpsDecimalToDegreesLatitude(latitude);
        char latitudeArray[10];
        strncpy(latitudeArray, lat.c_str(), 8);
        miceLatitudeStruct.degrees          = 10 * (latitudeArray[0] - '0') + latitudeArray[1] - '0';
        miceLatitudeStruct.minutes          = 10 * (latitudeArray[2] - '0') + latitudeArray[3] - '0';
        miceLatitudeStruct.minuteHundredths = 10 * (latitudeArray[5] - '0') + latitudeArray[6] - '0';
        miceLatitudeStruct.north = 0;
        if (latitudeArray[7] == 'N') miceLatitudeStruct.north = 1;
        return miceLatitudeStruct;
    }

    gpsLongitudeStruct gpsDecimalToDegreesMiceLongitude(float longitude) {
        gpsLongitudeStruct miceLongitudeStruct;
        String lng = gpsDecimalToDegreesLongitude(longitude);
        char longitudeArray[10];
        strncpy(longitudeArray,lng.c_str(), 9);
        miceLongitudeStruct.degrees             = 100 * (longitudeArray[0] - '0') + 10 * (longitudeArray[1] - '0') + longitudeArray[2] - '0';
        miceLongitudeStruct.minutes             = 10  * (longitudeArray[3] - '0') + longitudeArray[4] - '0';
        miceLongitudeStruct.minuteHundredths    = 10  * (longitudeArray[6] - '0') + longitudeArray[7] - '0';
        miceLongitudeStruct.east = 0;
        if (longitudeArray[8] == 'E') miceLongitudeStruct.east = 1;
        return miceLongitudeStruct;
    }

    String generateMiceGPSBeaconPacket(const String& miceMsgType, const String& callsign, const String& symbol, const String& overlay, const String& path, float latitude, float longitude, float course, float speed, int altitude, int ambiguityLevel) {
        if (ambiguityLevel > 0) {
            latitude = applyAmbiguity(latitude, ambiguityLevel);
            longitude = applyAmbiguity(longitude, ambiguityLevel);
        }
        gpsLatitudeStruct latitudeStruct    = gpsDecimalToDegreesMiceLatitude(latitude);
        gpsLongitudeStruct longitudeStruct  = gpsDecimalToDegreesMiceLongitude(longitude);

        uint8_t miceDestinationArray[7];
        encodeMiceDestinationField(miceMsgType, &miceDestinationArray[0], &latitudeStruct, &longitudeStruct);
        miceDestinationArray[6] = 0x00;     // por repetidor?
        String miceDestination = (char*)miceDestinationArray;

        uint8_t miceInfoFieldArray[14];
        miceInfoFieldArray[0] = 0x60;   //  0x60 for ` and 0x27 for '
        encodeMiceLongitude(&miceInfoFieldArray[1], &longitudeStruct);
        encodeMiceCourseSpeed(&miceInfoFieldArray[4], (uint32_t)speed, (uint32_t)course); //speed= gps.speed.knots(), course = gps.course.deg());

        char symbolOverlayArray[1];
        strncpy(symbolOverlayArray,symbol.c_str(),1);
        miceInfoFieldArray[7] = symbolOverlayArray[0];
        strncpy(symbolOverlayArray,overlay.c_str(),1);
        miceInfoFieldArray[8] = symbolOverlayArray[0];

        encodeMiceAltitude(&miceInfoFieldArray[9], (uint32_t)altitude); // altitude = gps.altitude.meters()
        miceInfoFieldArray[13] = 0x00;      // por repetidor?
        String miceInformationField = (char*)miceInfoFieldArray;

        String miceAPRSPacket = callsign;
        miceAPRSPacket += ">";
        miceAPRSPacket += miceDestination;
        if (path != "") {
            miceAPRSPacket += ",";
            miceAPRSPacket += path;
        }
        miceAPRSPacket += ":";
        miceAPRSPacket += miceInformationField;
        return miceAPRSPacket;
    }

}
