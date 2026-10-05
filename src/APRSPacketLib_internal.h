// APRSPacketLib - internal helpers shared between the library source files.
// Not part of the public API (not in include/APRSPacketLib.h); kept non-static so the tests can still reach them.

#ifndef APRSPACKETLIB_INTERNAL_H
#define APRSPACKETLIB_INTERNAL_H

#include <APRSPacketLib.h>

namespace APRSPacketLib {

    // APRSPacketLib_base.cpp
    String  formatAddressee(const String& addressee);

    // APRSPacketLib_base91.cpp
    float   applyAmbiguity(float coordinate, int ambiguityLevel);
    String  compressedSymbolTable(const String& overlay);
    int     decodeBase91EncodedCourse(const String& course);
    int     decodeBase91EncodedSpeed(const String& speed);
    int     decodeBase91EncodedAltitude(const String& altitude);

    // APRSPacketLib_coordinates.cpp
    float   decodeLatitude(const String& Latitude);
    float   decodeLongitude(const String& Longitude);
    int     decodeCourse(const String& course);
    int     decodeSpeed(const String& speed);
    int     decodeAltitude(const String& altitude);
    float   gpsDegreesToDecimalLatitude(const String& degreesLatitude);
    float   gpsDegreesToDecimalLongitude(const String& degreesLongitude);
    String  gpsDecimalToDegreesLatitude(double lat);
    String  gpsDecimalToDegreesLongitude(double lon);

    // APRSPacketLib_mice.cpp
    String  decodeMiceMsgType(const String& tocall);
    int     decodeMiceSpeed(char char3, char char4);
    int     decodeMiceCourse(char char4, char char5);
    int     decodeMiceAltitude(const String& informationField);
    float   decodeMiceLatitude(const String& destinationField);
    float   decodeMiceLongitude(const String& destinationField, const String& informationField);

}

#endif
