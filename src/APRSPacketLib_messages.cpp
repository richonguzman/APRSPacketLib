// APRSPacketLib - status, messages and acks

#include <APRSPacketLib.h>
#include "APRSPacketLib_internal.h"

namespace APRSPacketLib {

    String generateStatusPacket(const String& callsign, const String& tocall, const String& path, const String& status) {
        return generateBasePacket(callsign, tocall, path) + ":>"  + status;
    }

    String generateMessagePacket(const String& callsign, const String& tocall, const String& path, const String& addressee, const String& message) {
        String processedMessage = message;
        processedMessage.trim();
        return generateBasePacket(callsign, tocall, path) + "::" + formatAddressee(addressee) + ":" + processedMessage;
    }

    // Builds the ack text for a received message: "hola{12" -> "ack12", "hola{MM}AA" -> "ackMM}AA" (APRS 1.2c reply-acks).
    // Returns "" when the message has no message ID (no ack requested, APRS101 ch.14).
    String generateAckMessage(const String& receivedMessage) {
        int leftCurlyBraceIndex = receivedMessage.lastIndexOf('{');
        if (leftCurlyBraceIndex == -1) return "";
        String messageId = receivedMessage.substring(leftCurlyBraceIndex + 1);
        messageId.trim();
        if (messageId.length() == 0) return "";
        return "ack" + messageId;
    }

    // Builds the complete ack packet, ready to transmit ("" when no ack was requested)
    String generateAckPacket(const String& callsign, const String& tocall, const String& path, const String& addressee, const String& receivedMessage) {
        String ackMessage = generateAckMessage(receivedMessage);
        if (ackMessage == "") return "";
        return generateMessagePacket(callsign, tocall, path, addressee, ackMessage);
    }

}
