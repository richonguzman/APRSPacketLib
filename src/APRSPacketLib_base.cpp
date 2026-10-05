/*_______________________________________________

          █████╗ ██████╗ ██████╗ ███████╗
         ██╔══██╗██╔══██╗██╔══██╗██╔════╝
         ███████║██████╔╝██████╔╝███████╗
         ██╔══██║██╔═══╝ ██╔══██╗╚════██║
         ██║  ██║██║     ██║  ██║███████║
         ╚═╝  ╚═╝╚═╝     ╚═╝  ╚═╝╚══════╝

██████╗  █████╗  ██████╗██╗  ██╗███████╗████████╗
██╔══██╗██╔══██╗██╔════╝██║ ██╔╝██╔════╝╚══██╔══╝
██████╔╝███████║██║     █████╔╝ █████╗     ██║
██╔═══╝ ██╔══██║██║     ██╔═██╗ ██╔══╝     ██║
██║     ██║  ██║╚██████╗██║  ██╗███████╗   ██║
╚═╝     ╚═╝  ╚═╝ ╚═════╝╚═╝  ╚═╝╚══════╝   ╚═╝

               ██╗     ██╗██████╗
               ██║     ██║██╔══██╗
               ██║     ██║██████╔╝
               ██║     ██║██╔══██╗
               ███████╗██║██████╔╝
               ╚══════╝╚═╝╚═════╝

             Ricardo Guzman - CA2RXU
https://github.com/richonguzman/LoRa_APRS_Tracker
   (donations : http://paypal.me/richonguzman)
_______________________________________________*/

// APRSPacketLib - packet header, path, APRS-IS conversion and general helpers

#include <APRSPacketLib.h>
#include "APRSPacketLib_internal.h"

namespace APRSPacketLib {

    bool checkNocall(const String& callsign) {
        return callsign.indexOf("NOCALL") != -1 || callsign.indexOf("N0CALL") != -1;
    }

    // Defensive trim: if the LoRa RF frame marker (\x3c\xff\x01) turns up
    // anywhere inside a payload/message (corruption, or a crafted string
    // trying to inject a fake "start of packet"), cut there instead of
    // passing it through.
    String checkForStartingBytes(const String& packet) {
        int index = packet.indexOf("\x3c\xff\x01");
        return (index != -1) ? packet.substring(0, index) : packet;
    }

    // Builds "CALL>TOCALL[,PATH]". The path is cleaned hop by hop: spaces and empty hops are dropped,
    // so "", "WIDE1-1,", "RFONLY," + "" or " WIDE1-1 , WIDE2-1" all give a valid header.
    String generateBasePacket(const String& callsign, const String& tocall, const String& path) {
        String packet = callsign + ">" + tocall;
        unsigned int start = 0;
        while (start < path.length()) {
            int end = path.indexOf(",", start);
            if (end == -1) end = path.length();
            String hop = path.substring(start, end);
            hop.trim();
            if (hop.length() > 0) {
                packet += ",";
                packet += hop;
            }
            start = end + 1;
        }
        return packet;
    }

    // APRS101 ch.14: the addressee is a fixed 9-character field, padded with spaces (object names use the same field, ch.11)
    String formatAddressee(const String& addressee) {
        String formattedAddressee = addressee.substring(0, 9);
        while (formattedAddressee.length() < 9) formattedAddressee += ' ';
        return formattedAddressee;
    }

    // Converts a station's own packet (built for RF) into the version it uploads to APRS-IS.
    // aprs-is.net: "Packets originating from the client should only have TCPIP* in the path, nothing more or less"
    // -- the RF path (WIDE1-1...) is dropped, and no q construct is added: the server appends ",qAC,SERVER" itself.
    // Only for the station's own packets, not for gating packets heard from others (those use qAR/qAO).
    String generateAPRSISPacket(const String& packet) {
        int colonIndex = packet.indexOf(":");               // first ':' always ends the header (callsigns/path never contain ':')
        if (colonIndex == -1) return packet;
        String header = packet.substring(0, colonIndex);
        int commaIndex = header.indexOf(",");
        if (commaIndex != -1) header = header.substring(0, commaIndex);     // keep only CALL>TOCALL
        return header + ",TCPIP*" + packet.substring(colonIndex);
    }

}
