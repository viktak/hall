#include <os_type.h>
#include <ESP8266WiFi.h>

#include "common.h"
#include "timechangerules.h"
#include "settings.h"

namespace common
{
    TimeChangeRule *tcr;

    //   !  For "strftime" to work delete Time.h file in TimeLib library  !!!
    char *GetFullDateTime(const char *formattingString, size_t size)
    {
        time_t localTime = timechangerules::timezones[settings::timeZone]->toLocal(now(), &tcr);
        struct tm *now = localtime(&localTime);
        char *buf = new char[20];
        strftime(buf, 20, formattingString, now);
        return buf;
    }

    void SetRandomSeed()
    {
        uint32_t seed;

        // random works best with a seed that can use 31 bits
        // analogRead on a unconnected pin tends toward less than four bits
        seed = analogRead(0);
        delay(1);

        for (int shifts = 3; shifts < 31; shifts += 3)
        {
            seed ^= analogRead(0) << shifts;
            delay(1);
        }

        randomSeed(seed);
    }

    void DateTimeToString(char *dest, time_t localTime)
    {
        sprintf(dest, "%u-%02u-%02u %02u:%02u:%02u", year(localTime), month(localTime), day(localTime), hour(localTime), minute(localTime), second(localTime));
    }


    String TimeIntervalToString(const time_t time)
    {
        char tmp[10];
    
        sprintf(tmp, "%02u:%02u:%02u", time / 3600, minute(time), second(time));
    
        return (String)tmp;
    }

    String GetDeviceMAC()
    {
        String s = WiFi.macAddress();

        for (size_t i = 0; i < 5; i++)
            s.remove(14 - i * 3, 1);

        return s;
    }

    void setup()
    {
        SetRandomSeed();
    }
}
