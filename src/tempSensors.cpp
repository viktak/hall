#include <ArduinoOTA.h>

#include <DallasTemperature.h>

#include "tempSensors.h"
#include "settings.h"
#include "mqtt.h"

#define ONE_WIRE_GPIO 2
#define DS1820_RESOLUTION 12

namespace tempSensors
{
    uint8_t oneWireDevicesCount;
    OneWire oneWire(ONE_WIRE_GPIO);
    DallasTemperature sensors(&oneWire);
    thermometer thermometers[32];

    unsigned long oldTemperatureMillis = 0;

    String OneWireDeviceAddress2HEX(DeviceAddress deviceAddress, char Separator)
    {
        static const char *hexDigits = "0123456789ABCDEF";

        uint8_t h;
        String result = "";

        for (uint8_t i = 0; i < 8; i++)
        {
            h = deviceAddress[i] / 16;
            result += hexDigits[h];
            result += hexDigits[deviceAddress[i] % 16];
            if (i < 7)
                result += Separator;
        }
        return result;
    }

    void InitSensors()
    {
        Serial.print("Locating 1-wire devices...");
        sensors.begin();
        Serial.print("Found ");
        oneWireDevicesCount = sensors.getDeviceCount();
        Serial.print(oneWireDevicesCount, DEC);
        Serial.println(" device(s).");

        if (oneWireDevicesCount > 0)
        {
            for (size_t i = 0; i < oneWireDevicesCount; i++)
            {
                if (!sensors.getAddress(thermometers[i].deviceAddress, 0))
                    Serial.println("Unable to find address for Device " + (String)i);

                sensors.getAddress(thermometers[i].deviceAddress, i);
                Serial.print("Device ");
                Serial.print(i);
                Serial.print(":\t");
                Serial.print(OneWireDeviceAddress2HEX(thermometers[i].deviceAddress, ':'));
                Serial.println();
                thermometers[i].parasitePowered = sensors.isParasitePowerMode();
                thermometers[i].resolution = DS1820_RESOLUTION;
                sensors.setResolution(thermometers[i].deviceAddress, thermometers[i].resolution);
                thermometers[i].FriendlyName = OneWireDeviceAddress2HEX(thermometers[i].deviceAddress, ':');
            }
        }
    }

    void ReadTemperatures()
    {
        sensors.requestTemperatures(); // Send the command to get temperatures
        for (size_t i = 0; i < oneWireDevicesCount; i++)
        {
            thermometers[i].measuredTemperatureC = sensors.getTempC(thermometers[i].deviceAddress);
            if (thermometers[i].measuredTemperatureC != -127)
            {
                mqtt::PublishData(("thermometers/" + OneWireDeviceAddress2HEX(thermometers[i].deviceAddress, ':')).c_str(), (String(thermometers[i].measuredTemperatureC)).c_str(), false);
            }
        }
    }

    void setup()
    {
        InitSensors();
    }

    void loop()
    {
        if (millis() - oldTemperatureMillis > settings::temperatureRefreshInterval * 1000)
        {
            ReadTemperatures();
            oldTemperatureMillis = millis();
        }
    }

}