// APRSPacketLib - received packet decoder (processReceivedPacket)

#include <APRSPacketLib.h>
#include "APRSPacketLib_internal.h"

namespace APRSPacketLib {

    /********** MAIN PROCESS**********/

    APRSPacket processReceivedPacket(const String& receivedPacket, int rssi, float snr, int freqError) {
        /*  Packet type:
            gps             = 0
            message         = 1
            status          = 2
            telemetry       = 3
            mic-e           = 4
            object          = 5
            unrecognized    = 6   */
        APRSPacket aprsPacket;

        aprsPacket.header = "";
        String temp0 = receivedPacket;
        int firstColonIndex = receivedPacket.indexOf(":");
        if (firstColonIndex > 0 && firstColonIndex < receivedPacket.length() && receivedPacket[firstColonIndex + 1] == '}') {   // 3rd Party
            aprsPacket.header = receivedPacket.substring(receivedPacket.indexOf(":}"));
            temp0 = receivedPacket.substring(receivedPacket.indexOf(":}") + 2);
        }

        aprsPacket.sender   = temp0.substring(0, temp0.indexOf(">"));

        String temp1 = temp0.substring(temp0.indexOf(">") + 1, temp0.indexOf(":"));
        aprsPacket.tocall   = temp1;
        aprsPacket.path     = "";
        if (temp1.indexOf(",") > 2) {
            aprsPacket.tocall   = temp1.substring(0, temp1.indexOf(","));
            aprsPacket.path     = temp1.substring(temp1.indexOf(",") + 1, temp1.indexOf(":"));
        }

        if (temp0.indexOf(":=") > 10 || temp0.indexOf(":!") > 10 || temp0.indexOf(":@") > 10 ) {
            aprsPacket.type = 0;
            String gpsChars = ":=";
            int gpsCharsOffset = 2;
            if (temp0.indexOf(":!") > 10) gpsChars = ":!";
            if (temp0.indexOf(":@") > 10) {
                gpsChars = ":@";
                gpsCharsOffset = 9;
            }
            int gpsCharsIndex       = temp0.indexOf(gpsChars);
            int payloadOffset       = gpsCharsIndex + gpsCharsOffset;

            // Spec-based: the symbol table id (byte 0 of the position data) is the
            // only reliable discriminator between compressed and uncompressed
            // formats. An uncompressed position always starts with a latitude
            // digit; a compressed one always starts with '/', '\', or an overlay
            // 'A'-'Z' / 'a'-'j'. The old check instead whitelisted a handful of
            // observed values of the compression-type byte (offset+12), which is
            // a free-form base91 byte with ~90 valid values -- so plenty of
            // conformant compressed packets were falling through to "Degrees and
            // Decimal Minutes" and getting mis-decoded.
            char symbolTableChar = temp0[payloadOffset];
            bool isCompressed = (symbolTableChar == '/' || symbolTableChar == '\\' ||
                                  (symbolTableChar >= 'A' && symbolTableChar <= 'Z') ||
                                  (symbolTableChar >= 'a' && symbolTableChar <= 'j'));

            // payload ends up holding just the free-text comment -- the fixed-width
            // position/symbol/course/speed/altitude fields are already extracted
            // below from temp0 directly, so they don't need to survive in payload.
            // Compressed consumes 13 bytes from payloadOffset (1 symbol table +
            // 4 lat + 4 lon + 1 symbol + 2 course/speed + 1 compression-type byte).
            // Uncompressed consumes 19 bytes (8 lat + 1 symbol table + 9 lon + 1 symbol).
            aprsPacket.payload = temp0.substring(payloadOffset + (isCompressed ? 13 : 19));

            if (isCompressed) {   //  Base91 Encoding
                int encodedBytePosition = payloadOffset + 12;
                char currentChar        = temp0[encodedBytePosition];   // still needed below: altitude vs course/speed csT
                aprsPacket.latitude     = decodeBase91EncodedLatitude(temp0.substring(payloadOffset + 1, payloadOffset + 5));
                aprsPacket.longitude    = decodeBase91EncodedLongitude(temp0.substring(payloadOffset + 5, payloadOffset + 9));
                aprsPacket.symbol       = temp0.substring(payloadOffset + 9, payloadOffset + 10);
                aprsPacket.overlay      = temp0.substring(payloadOffset, payloadOffset + 1);

                if ((currentChar == 'T' || currentChar == 'Q') && temp0.substring(payloadOffset + 10, payloadOffset + 11) == " ") {
                    aprsPacket.course   = 0;
                    aprsPacket.speed    = 0;
                    aprsPacket.altitude = 0;
                } else {
                    if (currentChar == 'Q') { // altitude csT
                        aprsPacket.altitude = decodeBase91EncodedAltitude(temp0.substring(payloadOffset + 10, payloadOffset + 12));
                        aprsPacket.course   = 0;
                        aprsPacket.speed    = 0;
                    } else { // normal csT ('G' or '[')
                        aprsPacket.course   = decodeBase91EncodedCourse(temp0.substring(payloadOffset + 10, payloadOffset + 11));
                        aprsPacket.speed    = decodeBase91EncodedSpeed(temp0.substring(payloadOffset + 11, payloadOffset + 12));
                        aprsPacket.altitude = 0;
                    }
                }
            } else {    //  Degrees and Decimal Minutes
                aprsPacket.latitude     = decodeLatitude(temp0.substring(payloadOffset, payloadOffset + 8));
                aprsPacket.longitude    = decodeLongitude(temp0.substring(payloadOffset + 9, payloadOffset + 18));
                aprsPacket.symbol       = temp0.substring(payloadOffset + 18, payloadOffset+ 19);
                aprsPacket.overlay      = temp0.substring(payloadOffset + 8, payloadOffset + 9);
                if (temp0.substring(payloadOffset + 22, payloadOffset + 23) == "/") {
                    aprsPacket.course   = decodeCourse(temp0.substring(payloadOffset + 19, payloadOffset + 22));
                    aprsPacket.speed    = decodeSpeed(temp0.substring(payloadOffset + 23, payloadOffset + 26));
                } else {
                    aprsPacket.course   = 0;
                    aprsPacket.speed    = 0;
                }
                int altitudeIndex = temp0.indexOf("/A=");
                if (altitudeIndex > 0 && (altitudeIndex + 9 <= temp0.length())) {
                    aprsPacket.altitude = decodeAltitude(temp0.substring(altitudeIndex + 3, altitudeIndex + 9));
                } else {
                    aprsPacket.altitude = 0;
                }
            }
        } else if (temp0.indexOf("::") > 10) {
            aprsPacket.type = 1;
            int doubleColonIndex = temp0.indexOf("::");
            String temp1 = temp0.substring(doubleColonIndex + 2, doubleColonIndex + 11);
            temp1.trim();
            aprsPacket.addressee    = temp1;
            aprsPacket.payload      = temp0.substring(doubleColonIndex + 12);
            aprsPacket.latitude     = 0;
            aprsPacket.longitude    = 0;
        } else if (temp0.indexOf(":>") > 10) {
            aprsPacket.type = 2;
            aprsPacket.payload = temp0.substring(temp0.indexOf(":>") + 2);
        } else if (temp0.indexOf(":T#") >= 10 && temp0.indexOf(":=/") == -1) {
            aprsPacket.type = 3;
            aprsPacket.payload = temp0.substring(temp0.indexOf(":T#") + 3);
        } else if (temp0.indexOf(":`") > 10 || temp0.indexOf(":'") > 10) {
            aprsPacket.type = 4;
            if (temp0.indexOf(":`") > 10) {
                aprsPacket.payload  = temp0.substring(temp0.indexOf(":`") + 2);
            } else {
                aprsPacket.payload  = temp0.substring(temp0.indexOf(":'") + 2);
            }
            aprsPacket.miceType     = decodeMiceMsgType(aprsPacket.tocall.substring(0,3));
            aprsPacket.symbol       = aprsPacket.payload.substring(6,7);
            aprsPacket.overlay      = aprsPacket.payload.substring(7,8);
            aprsPacket.latitude     = decodeMiceLatitude(aprsPacket.tocall);
            aprsPacket.longitude    = decodeMiceLongitude(aprsPacket.tocall, aprsPacket.payload);
            aprsPacket.speed        = decodeMiceSpeed(aprsPacket.payload[3], aprsPacket.payload[4]);
            aprsPacket.course       = decodeMiceCourse(aprsPacket.payload[4], aprsPacket.payload[5]);
            aprsPacket.altitude     = decodeMiceAltitude(aprsPacket.payload);

            // payload ends up holding just the free-text comment. The fixed
            // 8-byte block (3 longitude + 3 speed/course + symbol + overlay)
            // is always present; an optional altitude block (`xxx}, 5 bytes)
            // follows it when decodeMiceAltitude()'s own marker check matches.
            int miceCommentStart = 8;
            if (aprsPacket.payload.indexOf("`") == 8 && aprsPacket.payload.indexOf("}") == 12) {
                miceCommentStart = 13;
            }
            aprsPacket.payload = aprsPacket.payload.substring(miceCommentStart);
        } else if (temp0.indexOf(":;") > 10) {
            aprsPacket.type = 5;
            aprsPacket.payload = temp0.substring(temp0.indexOf(":;") + 2);
        } else {
            // Doesn't match any known DTI/pattern above. Previously left
            // aprsPacket.type unassigned (undefined behavior -- callers
            // checking e.g. "type == 0" could match by coincidence of
            // leftover stack memory). Now explicit, and payload carries the
            // full, uncut original packet so callers have something useful
            // to show without needing the raw input separately.
            aprsPacket.type     = 6;
            aprsPacket.payload  = receivedPacket;
        }

        if (aprsPacket.type != 1) aprsPacket.addressee = "";

        if (aprsPacket.type != 0 && aprsPacket.type != 4) {
            aprsPacket.symbol       = "";
            aprsPacket.overlay      = "";
            aprsPacket.latitude     = 0;
            aprsPacket.longitude    = 0;
            aprsPacket.course       = 0;
            aprsPacket.speed        = 0;
            aprsPacket.altitude     = 0;
        }

        if (aprsPacket.type != 4) aprsPacket.miceType = "";

        aprsPacket.rssi             = rssi;
        aprsPacket.snr              = snr;
        aprsPacket.freqError        = freqError;

        return aprsPacket;
    }

}
