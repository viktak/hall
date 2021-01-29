struct config{
  char ssid[32];
  char password[32];

  char friendlyName[30];
  uint heartbeatInterval;

  uint8_t timeZone;

  char mqttServer[64];
  int mqttPort;
  char mqttTopic[32];

  bool dst;

  int temperatureRefreshInterval;
};

struct sunData_t{
  time_t Sunrise;
  time_t Sunset;
};

struct thermometer{
  DeviceAddress deviceAddress;
  float measuredTemperatureC;
  String FriendlyName;
};

struct digitalInput{
  char gpio;
  char32_t buttonPattern;
  BUTTON_STATES buttonState;
};

struct PIRInput{
  char gpio;
  bool value;
  bool changed;
};

