#include "Button2.h"

#include "mqtt.h"

#define PIR_SENSOR 4
#define HALL_SENSOR 13

namespace buttons
{

    Button2 sensorPIR = Button2(PIR_SENSOR);
    Button2 sensorHall = Button2(HALL_SENSOR);

    void ButtonPressedHandler(Button2 &btn)
    {
        if (btn == sensorPIR)
        {
            mqtt::PublishData(((String)("PIR0")).c_str(), ((String)"off").c_str(), true);
        }
        else if (btn == sensorHall)
        {
            // Serial.println("sensorHall: Pressed");
        }
    }

    void ButtonReleasedHandler(Button2 &btn)
    {
        if (btn == sensorPIR)
        {
            mqtt::PublishData(((String)("PIR0")).c_str(), ((String)"on").c_str(), true);
        }
        else if (btn == sensorHall)
        {
            // Serial.print("sensorHall: Released after ");
            // Serial.println(btn.wasPressedFor());
        }
    }
    void setup()
    {
        sensorPIR.setPressedHandler(ButtonPressedHandler);
        sensorPIR.setReleasedHandler(ButtonReleasedHandler);

        sensorHall.setPressedHandler(ButtonPressedHandler);
        sensorHall.setReleasedHandler(ButtonReleasedHandler);
    }

    void loop()
    {
        sensorPIR.loop();
        sensorHall.loop();
    }

}