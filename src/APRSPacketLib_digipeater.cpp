// APRSPacketLib - digipeating

#include <APRSPacketLib.h>
#include "APRSPacketLib_internal.h"

namespace APRSPacketLib {

    // Exact-token match (comma-delimited), not a raw substring search -- "path" is
    // meant to match a whole hop like "WIDE1-1", and a substring search would also
    // match inside an already-consumed hop like "WIDE1-1*", corrupting it on replace.
    int pathTokenIndex(const String& fullPath, const String& token) {
        unsigned int start = 0;
        while (start < fullPath.length()) {
            int end = fullPath.indexOf(",", start);
            if (end == -1) end = fullPath.length();
            if (fullPath.substring(start, end) == token) return start;
            start = end + 1;
        }
        return -1;
    }

    String buildDigiPacket(const String& packet, const String& callsign, const String& path, const String& fullPath, bool thirdParty) {
        String packetToRepeat = packet.substring(0, packet.indexOf(",") + 1);
        int idx = pathTokenIndex(fullPath, path);
        String tempPath = fullPath.substring(0, idx) + callsign + "*" + fullPath.substring(idx + path.length());
        packetToRepeat += tempPath;
        packetToRepeat += packet.substring(packet.indexOf(thirdParty ? ":}" : ":"));
        return packetToRepeat;
    }

    String generateDigipeatedPacket(const String& packet, const String &callsign, const String& path) {
        bool thirdParty = false;

        int firstColonIndex = packet.indexOf(":");
        if (firstColonIndex > 5 && firstColonIndex < (packet.length() - 1) && packet[firstColonIndex + 1] == '}') thirdParty = true;

        String temp = packet.substring(packet.indexOf(">") + 1, packet.indexOf(":"));
        if (thirdParty) {               // only header is used and temp is replaced
            const String& header = packet.substring(3, packet.indexOf(":}"));
            temp = header.substring(header.indexOf(">") + 1);
        }
        if (temp.indexOf(",") > 2) {    // checks for path
            const String& completePath = temp.substring(temp.indexOf(",") + 1); // after tocall
            return (pathTokenIndex(completePath, path) != -1) ? buildDigiPacket(packet.substring(3), callsign, path, completePath, thirdParty) : "X";
        }
        return "X";
    }

}
