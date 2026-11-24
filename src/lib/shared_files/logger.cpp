/**
 * Logger implementation for the Miniware M2009 Modem Cartridge / Viditel Modem Emulator.
 * 
 * Author: Bart Eversdijk
 */
#include <Arduino.h>
#include "config.h"

void logInit() {
    Serial.begin(LOG_BAUDRATE);
}

void logFormat(const char *format, ...) {
#if DEBUG_IS_ON == 1
    char debugBuffer[128]; 
    va_list args; va_start(args, format);
    vsnprintf(debugBuffer, sizeof(debugBuffer), format, args);
    va_end(args);
    Serial.println(debugBuffer);
#endif
}

void logFormatTitle(const char * title, const char* format, ...) {
    #if DEBUG_IS_ON == 1
        char buffer[128]; 
        
        va_list args;
        va_start(args, format);
        vsnprintf(buffer, sizeof(buffer), format, args);
        va_end(args);
        
        Serial.print("[");
        Serial.print(title);
        Serial.print("] ");
        Serial.println(buffer);
    #endif
}
