// APRSPacketLib - uncompressed DDMM.hh <-> decimal coordinate conversion

#include <APRSPacketLib.h>
#include "APRSPacketLib_internal.h"

namespace APRSPacketLib {

    float decodeLatitude(const String& Latitude) {
        int latitudeDotIndex        = Latitude.indexOf(".");
        float convertedLatitude     = Latitude.substring(0,2).toFloat();
        convertedLatitude += Latitude.substring(2,4).toFloat() / 60;
        convertedLatitude += Latitude.substring(latitudeDotIndex + 1, latitudeDotIndex + 3).toFloat() / (60*100);
        if (Latitude.endsWith("S")) return - convertedLatitude;
        return convertedLatitude;
    }

    float decodeLongitude(const String& Longitude) {
        int longitudeDotIndex       = Longitude.indexOf(".");
        float convertedLongitude    = Longitude.substring(0,3).toFloat();
        convertedLongitude += Longitude.substring(3,5).toFloat() / 60;
        convertedLongitude += Longitude.substring(longitudeDotIndex + 1, longitudeDotIndex + 3).toFloat() / (60*100);
        if (Longitude.endsWith("W")) return -convertedLongitude;
        return convertedLongitude;
    }

    int decodeCourse(const String& course) {
        if (course == "..." || course == "000") return 0;
        return course.toInt();
    }

    int decodeSpeed(const String& speed) {
        return speed.toInt() * 1.852;
    }

    int decodeAltitude(const String& altitude) {
        return altitude.toInt() * 0.3048;
    }

    float gpsDegreesToDecimalLatitude(const String& degreesLatitude) {
        int degrees             = degreesLatitude.substring(0,2).toInt();
        int minute              = degreesLatitude.substring(2,4).toInt();
        int minuteHundredths    = degreesLatitude.substring(5,7).toInt();
        float decimalLatitude   = degrees + (minute/60.0) + (minuteHundredths/6000.0);
        return (degreesLatitude[7] == 'N') ? decimalLatitude : -decimalLatitude;
    }

    float gpsDegreesToDecimalLongitude(const String& degreesLongitude) {
        int degrees             = degreesLongitude.substring(0,3).toInt();
        int minute              = degreesLongitude.substring(3,5).toInt();
        int minuteHundredths    = degreesLongitude.substring(6,8).toInt();
        float decimalLongitude  = degrees + (minute/60.0) + (minuteHundredths/6000.0);
        return (degreesLongitude[8] == 'W') ? -decimalLongitude : decimalLongitude;
    }

    String doubleToString(double n, int ndec) {
        String r = "";
        if (n > -1 && n < 0) r = "-";
        int v = n;
        r += v;
        r += '.';
        for (int i = 0; i < ndec; i++) {
            n -= v;
            n = 10 * abs(n);
            v = n;
            r += v;
        }
        return r;
    }

    String gpsDecimalToDegreesLatitude(double lat) {
        String degrees = doubleToString(lat, 6);
        String latitude, convDeg3;
        float convDeg, convDeg2;
        String north_south = "N";
        if (abs(degrees.toFloat()) < 10) latitude += "0";
        if (degrees.indexOf("-") == 0) {
            north_south = "S";
            latitude += degrees.substring(1, degrees.indexOf("."));
        } else {
            latitude += degrees.substring(0, degrees.indexOf("."));
        }
        convDeg  = abs(degrees.toFloat()) - abs(int(degrees.toFloat()));
        convDeg2 = (convDeg * 60)/100;
        convDeg3 = String(convDeg2,6);

        int dotIndex = convDeg3.indexOf(".");
        latitude += convDeg3.substring(dotIndex + 1, dotIndex + 3);
        latitude += ".";
        latitude += convDeg3.substring(dotIndex + 3, dotIndex + 5);
        latitude += north_south;
        return latitude;
    }

    String gpsDecimalToDegreesLongitude(double lon) {
        String degrees = doubleToString(lon,6);
        String longitude, convDeg3;
        float convDeg, convDeg2;
        String east_west = "E";
        if (abs(degrees.toFloat()) < 100) longitude += "0";
        if (abs(degrees.toFloat()) < 10)  longitude += "0";
        if (degrees.indexOf("-") == 0) {
            east_west = "W";
            longitude += degrees.substring(1, degrees.indexOf("."));
        } else {
            longitude += degrees.substring(0, degrees.indexOf("."));
        }
        convDeg  = abs(degrees.toFloat()) - abs(int(degrees.toFloat()));
        convDeg2 = (convDeg * 60)/100;
        convDeg3 = String(convDeg2,6);

        int dotIndex = convDeg3.indexOf(".");
        longitude += convDeg3.substring(dotIndex + 1, dotIndex + 3);
        longitude += ".";
        longitude += convDeg3.substring(dotIndex + 3, dotIndex + 5);
        longitude += east_west;
        return longitude;
    }

}
