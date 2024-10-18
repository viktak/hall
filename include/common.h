#ifndef COMMON_H
#define COMMON_H

#include <Arduino.h>
#include <os_type.h>

namespace common
{
    static const int32_t DEBUG_SPEED = 115200;
    static const String HARDWARE_ID = "hall sensors";
    static const String HARDWARE_VERSION = "1.0";
    static const String FIRMWARE_ID = "hall";

    extern String GetDeviceMAC();
    extern char *GetFullDateTime(const char *formattingString, size_t size);
    extern void DateTimeToString(char *dest, time_t localTime);
    extern String TimeIntervalToString(const time_t time);
    extern String GetDeviceMAC();

    extern void setup();
}

#endif