#ifndef NATIVE_TEST_BUILD
#include <Arduino.h>
#include "unity_config.h"

extern "C" {
    void unity_output_char(int c) {
        Serial.write((uint8_t)c);
    }

    void unity_output_flush(void) {
        Serial.flush();
    }

    void unity_output_complete(void) {
        // We leave this empty to prevent Serial.end() 
        // and avoid the PermissionError(13)
        Serial.flush();
    }
}
#endif // NATIVE_TEST_BUILD