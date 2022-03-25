#ifndef TEMP_SENSORS_H
#define TEMP_SENSORS_H

#include <OneWire.h>
#include <DallasTemperature.h>

struct thermometer
{
    DeviceAddress deviceAddress;
    float measuredTemperatureC;
    String FriendlyName;
    uint8_t resolution;
    bool parasitePowered;
};

namespace tempSensors
{
    extern uint8_t oneWireDevicesCount;
    extern thermometer thermometers[32];

    extern String OneWireDeviceAddress2HEX(DeviceAddress deviceAddress, char Separator);
    extern void setup();
    extern void loop();
}

#endif