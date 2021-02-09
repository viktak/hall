#define __debugSettings
#include "includes.h"

//  Web server
ESP8266WebServer server(80);

//  Initialize Wifi
WiFiClient wclient;
PubSubClient PSclient(wclient);

//  Timers and their flags
os_timer_t heartbeatTimer;
os_timer_t temperatureTimer;
os_timer_t accessPointTimer;

//  Flags
bool needsHeartbeat = false;
bool needsTemperature = false;

//  Other global variables
config appConfig;
bool isAccessPoint = false;
bool isAccessPointCreated = false;
TimeChangeRule *tcr;        // Pointer to the time change rule

char16_t buttonMillis;

bool ntpInitialized = false;
uint8_t oneWireDevicesCount;
enum CONNECTION_STATE connectionState;

thermometer thermometers[32];

digitalInput digitalInputs[4] = {
  { 12, 0},
  { 13, 0},
  { 14, 0},
  { 16, 0}
};

PIRInput pirInputs[1] = {
  { 4, 1, false }
};


//  Initialize 1-wire
OneWire oneWire(ONE_WIRE_BUS);
DallasTemperature sensors(&oneWire);

WiFiUDP Udp;

void LogEvent(int Category, int ID, String Title, String Data){
  if (PSclient.connected()){

    String msg = "{";

    msg += "\"Node\":" + (String)ESP.getChipId() + ",";
    msg += "\"Category\":" + (String)Category + ",";
    msg += "\"ID\":" + (String)ID + ",";
    msg += "\"Title\":\"" + Title + "\",";
    msg += "\"Data\":\"" + Data + "\"}";

    Serial.println(msg);

    PSclient.publish((MQTT_CUSTOMER + String("/") + MQTT_PROJECT + String("/") + appConfig.mqttTopic + "/log").c_str(), msg.c_str(), false);
  }
}

void SetRandomSeed(){
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

void accessPointTimerCallback(void *pArg) {
  ESP.reset();
}

void heartbeatTimerCallback(void *pArg) {
  needsHeartbeat = true;
}

void temperatureTimerCallback(void *pArg) {
  needsTemperature = true;
}

bool loadSettings(config& data) {
  File configFile = LittleFS.open("/config.json", "r");
  if (!configFile) {
    Serial.println("Failed to open config file");
    LogEvent(EVENTCATEGORIES::System, 1, "FS failure", "Failed to open config file.");
    return false;
  }

  size_t size = configFile.size();
  if (size > 1024) {
    Serial.println("Config file size is too large");
    LogEvent(EVENTCATEGORIES::System, 2, "FS failure", "Config file size is too large.");
    return false;
  }

  // Allocate a buffer to store contents of the file.
  std::unique_ptr<char[]> buf(new char[size]);

  // We don't use String here because ArduinoJson library requires the input
  // buffer to be mutable. If you don't use ArduinoJson, you may as well
  // use configFile.readString instead.
  configFile.readBytes(buf.get(), size);
  configFile.close();

  StaticJsonDocument<JSON_SETTINGS_SIZE> doc;
  DeserializationError error = deserializeJson(doc, buf.get());

  if (error) {
    Serial.println("Failed to parse config file");
    LogEvent(EVENTCATEGORIES::System, 3, "FS failure", "Failed to parse config file.");
    Serial.println(error.c_str());
    return false;
  }

  #ifdef __debugSettings
  serializeJsonPretty(doc,Serial);
  Serial.println();
  #endif

  if (doc["ssid"]){
    strcpy(appConfig.ssid, doc["ssid"]);
  }
  else
  {
    strcpy(appConfig.ssid, defaultSSID);
  }
  
  if (doc["password"]){
    strcpy(appConfig.password, doc["password"]);
  }
  else
  {
    strcpy(appConfig.password, DEFAULT_PASSWORD);
  }
  
  if (doc["mqttServer"]){
    strcpy(appConfig.mqttServer, doc["mqttServer"]);
  }
  else
  {
    strcpy(appConfig.mqttServer, DEFAULT_MQTT_SERVER);
  }
  
  if (doc["mqttPort"]){
    appConfig.mqttPort = doc["mqttPort"];
  }
  else
  {
    appConfig.mqttPort = DEFAULT_MQTT_PORT;
  }
  
  if (doc["mqttTopic"]){
    strcpy(appConfig.mqttTopic, doc["mqttTopic"]);
  }
  else
  {
    sprintf(appConfig.mqttTopic, "%s-%u", DEFAULT_MQTT_TOPIC, ESP.getChipId());
  }
  
  if (doc["friendlyName"]){
    strcpy(appConfig.friendlyName, doc["friendlyName"]);
  }
  else
  {
    strcpy(appConfig.friendlyName, NODE_DEFAULT_FRIENDLY_NAME);
  }
  
  if (doc["timezone"]){
    appConfig.timeZone = doc["timezone"];
  }
  else
  {
    appConfig.timeZone = 0;
  }
  
  if (doc["heartbeatInterval"]){
    appConfig.heartbeatInterval = doc["heartbeatInterval"];
  }
  else
  {
    appConfig.heartbeatInterval = DEFAULT_HEARTBEAT_INTERVAL;
  }
  
  if (doc["temperatureRefreshInterval"]){
    appConfig.temperatureRefreshInterval = doc["temperatureRefreshInterval"];
  }
  else
  {
    appConfig.temperatureRefreshInterval = DEFAULT_TEMPERATURE_REFRESH_INTERVAL;
  }

  String ma = WiFi.macAddress();
  ma.replace(":","");
  sprintf(defaultSSID, "%s-%s", appConfig.mqttTopic, ma.substring(6, 12).c_str());

  return true;
}

bool saveSettings() {
  StaticJsonDocument<1024> doc;

  doc["ssid"] = appConfig.ssid;
  doc["password"] = appConfig.password;

  doc["heartbeatInterval"] = appConfig.heartbeatInterval;

  doc["timezone"] = appConfig.timeZone;

  doc["mqttServer"] = appConfig.mqttServer;
  doc["mqttPort"] = appConfig.mqttPort;
  doc["mqttTopic"] = appConfig.mqttTopic;

  doc["friendlyName"] = appConfig.friendlyName;

  doc["temperatureRefreshInterval"] = appConfig.temperatureRefreshInterval;

  #ifdef __debugSettings
  serializeJsonPretty(doc,Serial);
  Serial.println();
  #endif

  File configFile = LittleFS.open("/config.json", "w");
  if (!configFile) {
    Serial.println("Failed to open config file for writing");
    LogEvent(System, 4, "FS failure", "Failed to open config file for writing.");
    return false;
  }
  serializeJson(doc, configFile);
  configFile.close();

  return true;
}

void defaultSettings(){
  #ifdef __debugSettings
  strcpy(appConfig.ssid, DEBUG_WIFI_SSID);
  strcpy(appConfig.password, DEBUG_WIFI_PASSWORD);
  strcpy(appConfig.mqttServer, DEBUG_MQTT_SERVER);
  #else
  strcpy(appConfig.ssid, "ESP");
  strcpy(appConfig.password, "password");
  strcpy(appConfig.mqttServer, "test.mosquitto.org");
  #endif

  appConfig.mqttPort = DEFAULT_MQTT_PORT;
 
  sprintf(defaultSSID, "%s-%u", DEFAULT_MQTT_TOPIC, ESP.getChipId());
  strcpy(appConfig.mqttTopic, defaultSSID);

  appConfig.timeZone = 2;

  appConfig.temperatureRefreshInterval = DEFAULT_TEMPERATURE_REFRESH_INTERVAL;
  strcpy(appConfig.friendlyName, NODE_DEFAULT_FRIENDLY_NAME);
  appConfig.heartbeatInterval = DEFAULT_HEARTBEAT_INTERVAL;


  if (!saveSettings()) {
    Serial.println("Failed to save config");
  } else {
    Serial.println("Config saved");
  }
}

String DateTimeToString(time_t time){

  String myTime = "";
  char s[2];

  //  years
  itoa(year(time), s, DEC);
  myTime+= s;
  myTime+="-";


  //  months
  itoa(month(time), s, DEC);
  myTime+= s;
  myTime+="-";

  //  days
  itoa(day(time), s, DEC);
  myTime+= s;

  myTime+=" ";

  //  hours
  itoa(hour(time), s, DEC);
  myTime+= s;
  myTime+=":";

  //  minutes
  if(minute(time) <10)
    myTime+="0";

  itoa(minute(time), s, DEC);
  myTime+= s;
  myTime+=":";

  //  seconds
  if(second(time) <10)
    myTime+="0";

  itoa(second(time), s, DEC);
  myTime+= s;

  return myTime;
}

String TimeIntervalToString(time_t time){

  String myTime = "";
  char s[2];

  //  hours
  itoa((time/3600), s, DEC);
  myTime+= s;
  myTime+=":";

  //  minutes
  if(minute(time) <10)
    myTime+="0";

  itoa(minute(time), s, DEC);
  myTime+= s;
  myTime+=":";

  //  seconds
  if(second(time) <10)
    myTime+="0";

  itoa(second(time), s, DEC);
  myTime+= s;
  return myTime;
}

String OneWireDeviceAddress2HEX(DeviceAddress deviceAddress, char Separator){
  static const char* hexDigits = "0123456789ABCDEF";

  uint8_t h;
  String result = "";

  for (uint8_t i = 0; i < 8; i++)
  {
    h = deviceAddress[i] / 16;
    result+=hexDigits[h];
    result+=hexDigits[deviceAddress[i] % 16];
    if (i<7) result+=Separator;
  }
  return result;
}

bool is_authenticated(){
  #ifdef __debugSettings
  return true;
  #endif
  if (server.hasHeader("Cookie")){
    String cookie = server.header("Cookie");
    if (cookie.indexOf("EspAuth=1") != -1) {
      LogEvent(EVENTCATEGORIES::Authentication, 1, "Success", "");
      return true;
    }
  }
  LogEvent(EVENTCATEGORIES::Authentication, 2, "Failure", "");
  return false;
}

void handleLogin(){
  String msg = "";
  if (server.hasHeader("Cookie")){
    String cookie = server.header("Cookie");
  }
  if (server.hasArg("DISCONNECT")){
    String header = "HTTP/1.1 301 OK\r\nSet-Cookie: EspAuth=0\r\nLocation: /login.html\r\nCache-Control: no-cache\r\n\r\n";
    server.sendContent(header);
    LogEvent(EVENTCATEGORIES::Login, 1, "Logout", "");
    return;
  }
  if (server.hasArg("username") && server.hasArg("password")){
    if (server.arg("username") == ADMIN_USERNAME &&  server.arg("password") == ADMIN_PASSWORD ){
      String header = "HTTP/1.1 301 OK\r\nSet-Cookie: EspAuth=1\r\nLocation: /status.html\r\nCache-Control: no-cache\r\n\r\n";
      server.sendContent(header);
      LogEvent(EVENTCATEGORIES::Login, 2, "Success", "User name: " + server.arg("username"));
      return;
    }
    msg = "<div class=\"alert alert-danger\"><strong>Error!</strong> Wrong user name and/or password specified.<a href=\"#\" class=\"close\" data-dismiss=\"alert\" aria-label=\"close\">&times;</a></div>";
    LogEvent(EVENTCATEGORIES::Login, 2, "Failure", "User name: " + server.arg("username") + " - Password: " + server.arg("password"));
  }

  File f = LittleFS.open("/pageheader.html", "r");
  String headerString;
  if (f.available()) headerString = f.readString();
  f.close();

  time_t localTime = timezones[appConfig.timeZone]->toLocal(now(), &tcr);

  f = LittleFS.open("/login.html", "r");

  String s, htmlString;

  while (f.available()){
    s = f.readStringUntil('\n');

    if (s.indexOf("%pageheader%")>-1) s.replace("%pageheader%", headerString);
    if (s.indexOf("%year%")>-1) s.replace("%year%", (String)year(localTime));
    if (s.indexOf("%alert%")>-1) s.replace("%alert%", msg);

    htmlString+=s;
  }
  f.close();
  server.send(200, "text/html", htmlString);
  LogEvent(PageHandler, 2, "Page served", "/");
}

void handleRoot() {
  LogEvent(EVENTCATEGORIES::PageHandler, 1, "Page requested", "/");

  if (!is_authenticated()){
    String header = "HTTP/1.1 301 OK\r\nLocation: /login.html\r\nCache-Control: no-cache\r\n\r\n";
    server.sendContent(header);
    return;
  }

  File f = LittleFS.open("/pageheader.html", "r");
  String headerString;
  if (f.available()) headerString = f.readString();
  f.close();

  time_t localTime = timezones[appConfig.timeZone]->toLocal(now(), &tcr);

  f = LittleFS.open("/index.html", "r");

  String FirmwareVersionString = String(FIRMWARE_VERSION) + " @ " + String(__TIME__) + " - " + String(__DATE__);

  String s, htmlString;

  while (f.available()){
    s = f.readStringUntil('\n');

    if (s.indexOf("%pageheader%")>-1) s.replace("%pageheader%", headerString);
    if (s.indexOf("%year%")>-1) s.replace("%year%", (String)year(localTime));
    if (s.indexOf("%espid%")>-1) s.replace("%espid%", (String)ESP.getChipId());
    if (s.indexOf("%hardwareid%")>-1) s.replace("%hardwareid%", HARDWARE_ID);
    if (s.indexOf("%hardwareversion%")>-1) s.replace("%hardwareversion%", HARDWARE_VERSION);
    if (s.indexOf("%softwareid%")>-1) s.replace("%softwareid%", FIRMWARE_ID);
    if (s.indexOf("%firmwareversion%")>-1) s.replace("%firmwareversion%", FirmwareVersionString);

    htmlString+=s;
  }
  f.close();
  server.send(200, "text/html", htmlString);
  LogEvent(EVENTCATEGORIES::PageHandler, 2, "Page served", "/");
}

void handleStatus() {

  LogEvent(EVENTCATEGORIES::PageHandler, 1, "Page requested", "status.html");
  if (!is_authenticated()){
     String header = "HTTP/1.1 301 OK\r\nLocation: /login.html\r\nCache-Control: no-cache\r\n\r\n";
     server.sendContent(header);
     return;
  }

  File f = LittleFS.open("/pageheader.html", "r");
  String headerString;
  if (f.available()) headerString = f.readString();
  f.close();

  time_t localTime = timezones[appConfig.timeZone]->toLocal(now(), &tcr);
  
  String FirmwareVersionString = String(FIRMWARE_VERSION);
  String s;

  f = LittleFS.open("/status.html", "r");

  String htmlString, ds18b20list;

  while (f.available()){
    s = f.readStringUntil('\n');

    //  System information
    if (s.indexOf("%pageheader%")>-1) s.replace("%pageheader%", headerString);
    if (s.indexOf("%year%")>-1) s.replace("%year%", (String)year(localTime));
    if (s.indexOf("%chipid%")>-1) s.replace("%chipid%", (String)ESP.getChipId());
    if (s.indexOf("%hardwareid%")>-1) s.replace("%hardwareid%", HARDWARE_ID);
    if (s.indexOf("%hardwareversion%")>-1) s.replace("%hardwareversion%", HARDWARE_VERSION);
    if (s.indexOf("%firmwareid%")>-1) s.replace("%firmwareid%", FIRMWARE_ID);
    if (s.indexOf("%firmwareversion%")>-1) s.replace("%firmwareversion%", FirmwareVersionString);
    if (s.indexOf("%uptime%")>-1) s.replace("%uptime%", TimeIntervalToString(millis()/1000));
    if (s.indexOf("%currenttime%")>-1) s.replace("%currenttime%", DateTimeToString(localTime));
    if (s.indexOf("%lastresetreason%")>-1) s.replace("%lastresetreason%", ESP.getResetReason());
    if (s.indexOf("%flashchipsize%")>-1) s.replace("%flashchipsize%",String(ESP.getFlashChipSize()));
    if (s.indexOf("%flashchipspeed%")>-1) s.replace("%flashchipspeed%",String(ESP.getFlashChipSpeed()));
    if (s.indexOf("%freeheapsize%")>-1) s.replace("%freeheapsize%",String(ESP.getFreeHeap()));
    if (s.indexOf("%freesketchspace%")>-1) s.replace("%freesketchspace%",String(ESP.getFreeSketchSpace()));
    if (s.indexOf("%friendlyname%")>-1) s.replace("%friendlyname%",appConfig.friendlyName);
    if (s.indexOf("%mqtt-topic%")>-1) s.replace("%mqtt-topic%",appConfig.mqttTopic);

    //  Network settings
    switch (WiFi.getMode()) {
      case WIFI_AP:
        if (s.indexOf("%wifimode%")>-1) s.replace("%wifimode%", "Access Point");
        if (s.indexOf("%macaddress%")>-1) s.replace("%macaddress%",String(WiFi.softAPmacAddress()));
        if (s.indexOf("%networkaddress%")>-1) s.replace("%networkaddress%",WiFi.softAPIP().toString());
        if (s.indexOf("%ssid%")>-1) s.replace("%ssid%",String(WiFi.SSID()));
        if (s.indexOf("%subnetmask%")>-1) s.replace("%subnetmask%","n/a");
        if (s.indexOf("%gateway%")>-1) s.replace("%gateway%","n/a");
        break;
      case WIFI_STA:
        if (s.indexOf("%wifimode%")>-1) s.replace("%wifimode%", "Station");
        if (s.indexOf("%macaddress%")>-1) s.replace("%macaddress%",String(WiFi.macAddress()));
        if (s.indexOf("%networkaddress%")>-1) s.replace("%networkaddress%",WiFi.localIP().toString());
        if (s.indexOf("%ssid%")>-1) s.replace("%ssid%",String(WiFi.SSID()));
        if (s.indexOf("%subnetmask%")>-1) s.replace("%subnetmask%",WiFi.subnetMask().toString());
        if (s.indexOf("%gateway%")>-1) s.replace("%gateway%",WiFi.gatewayIP().toString());
        break;
      default:
        //  This should not happen...
        break;
    }

      htmlString+=s;
    }
    f.close();
  server.send(200, "text/html", htmlString);
  LogEvent(EVENTCATEGORIES::PageHandler, 2, "Page served", "status.html");
}

void handleSensors() {
  LogEvent(EVENTCATEGORIES::PageHandler, 1, "Page requested", "sensors.html");

  if (!is_authenticated()){
     String header = "HTTP/1.1 301 OK\r\nLocation: /login.html\r\nCache-Control: no-cache\r\n\r\n";
     server.sendContent(header);
     return;
   }

   File f = LittleFS.open("/pageheader.html", "r");
   String headerString;
   if (f.available()) headerString = f.readString();
   f.close();

  time_t localTime = timezones[appConfig.timeZone]->toLocal(now(), &tcr);

   f = LittleFS.open("/sensors.html", "r");

  String s, htmlString, ds18b20list, analogSensorlist, digitalinputlist;

  //  Digital inputs
  digitalinputlist = "";
  for (size_t i = 0; i < sizeof(digitalInputs)/sizeof(digitalInputs[0]); i++) {
    digitalinputlist+="<tr><td>";
    digitalinputlist+="</td><td>";
    //digitalinputlist+=digitalRead(digitalInputs[i].gpio)==1?"Closed":"Open";

    switch (digitalInputs[i].buttonState) {
      case BUTTON_NOT_PRESSED:
        digitalinputlist+="Not pressed";
        break;
      case BUTTON_PRESSED:
        digitalinputlist+="Pressed";
        break;
      case BUTTON_LONG_PRESSED:
        digitalinputlist+="Long pressed";
        break;
      case BUTTON_BOUNCING:
        digitalinputlist+="Bouncing";
        break;
      default:
        digitalinputlist+="Unknown";
    }

    digitalinputlist+="</td></tr>";
  }
  //  Analog inputs
  analogSensorlist = analogRead(A0);

  //  DS1820
  for (size_t i = 0; i < oneWireDevicesCount; i++){
    String addr = "";

    ds18b20list+= "<div class=\"panel panel-default\"><div class=\"panel-heading\">DS-18B20</div>";
    ds18b20list+= "<div class=\"panel-body\"><table class=\"table table-hover\">";
    ds18b20list+= "<thead><tr><th>Name</th><th>Value</th></tr></thead><tbody><tr><td>Device ID</td><td>";
    ds18b20list+= OneWireDeviceAddress2HEX(thermometers[i].deviceAddress,':');
    ds18b20list+= "</td></tr><tr><td>Power mode</td><td>";

    if (sensors.isParasitePowerMode())
      ds18b20list+= "Parasite";
      else
      ds18b20list+= "Powered";

    ds18b20list+= "</td></tr><tr><td>Resolution</td><td>";
    ds18b20list+= String(sensors.getResolution(thermometers[i].deviceAddress));
    ds18b20list+= " bits</td></tr><tr><td>Measurements are taken</td><td>Every ";
    ds18b20list+= String(appConfig.temperatureRefreshInterval);
    ds18b20list+= " seconds</td></tr><tr><td>Last measured temperature</td><td>";
    ds18b20list+= String(thermometers[i].measuredTemperatureC);
    ds18b20list+= " °C</td></tr></tbody></table></div></div>";
  }

  while (f.available()){
    s = f.readStringUntil('\n');

    if (s.indexOf("%pageheader%")>-1) s.replace("%pageheader%", headerString);
    if (s.indexOf("%year%")>-1) s.replace("%year%", (String)year(localTime));

    //  DS1820
    if (s.indexOf("%ds18b20list%")>-1) s.replace("%ds18b20list%",ds18b20list);

    //  Analog sensors
    if (s.indexOf("%analoginputlist%")>-1) s.replace("%analoginputlist%",analogSensorlist);

    //  Digital inputs
    if (s.indexOf("%digitalinputlist%")>-1) s.replace("%digitalinputlist%",digitalinputlist);

    htmlString+=s;
    }
    f.close();
  server.send(200, "text/html", htmlString);
  LogEvent(EVENTCATEGORIES::PageHandler, 2, "Page served", "sensors.html");
}

void handleGeneralSettings() {
  LogEvent(EVENTCATEGORIES::PageHandler, 1, "Page requested", "generalsettings.html");

  if (!is_authenticated()){
     String header = "HTTP/1.1 301 OK\r\nLocation: /login.html\r\nCache-Control: no-cache\r\n\r\n";
     server.sendContent(header);
     return;
   }

  if (server.method() == HTTP_POST){  //  POST
    bool mqttDirty = false;

    if (server.hasArg("timezoneselector")){
      signed char oldTimeZone = appConfig.timeZone;
      appConfig.timeZone = atoi(server.arg("timezoneselector").c_str());

      adjustTime((appConfig.timeZone - oldTimeZone) * SECS_PER_HOUR);

      LogEvent(EVENTCATEGORIES::TimeZoneChange, 1, "New time zone", "UTC " + server.arg("timezoneselector"));
    }

    if (server.hasArg("friendlyname")){
      strcpy(appConfig.friendlyName, server.arg("friendlyname").c_str());
      LogEvent(EVENTCATEGORIES::FriendlyNameChange, 1, "New friendly name", appConfig.friendlyName);
    }

    if (server.hasArg("heartbeatinterval")){
      os_timer_disarm(&heartbeatTimer);
      appConfig.heartbeatInterval = server.arg("heartbeatinterval").toInt();
      LogEvent(EVENTCATEGORIES::HeartbeatIntervalChange, 1, "New Heartbeat interval", (String)appConfig.heartbeatInterval);
      os_timer_arm(&heartbeatTimer, appConfig.heartbeatInterval * 1000, true);
    }

    //  MQTT settings
    if (server.hasArg("mqttbroker")){
      if ((String)appConfig.mqttServer != server.arg("mqttbroker"))
        mqttDirty = true;
        sprintf(appConfig.mqttServer, "%s", server.arg("mqttbroker").c_str());
        LogEvent(EVENTCATEGORIES::MqttParamChange, 1, "New MQTT broker", appConfig.mqttServer);
    }

    if (server.hasArg("mqttport")){
      if (appConfig.mqttPort != atoi(server.arg("mqttport").c_str()))
        mqttDirty = true;
      appConfig.mqttPort = atoi(server.arg("mqttport").c_str());
      LogEvent(EVENTCATEGORIES::MqttParamChange, 2, "New MQTT port", server.arg("mqttport").c_str());
    }

    if (server.hasArg("mqtttopic")){
      if ((String)appConfig.mqttTopic != server.arg("mqtttopic"))
        mqttDirty = true;
        sprintf(appConfig.mqttTopic, "%s", server.arg("mqtttopic").c_str());
        LogEvent(EVENTCATEGORIES::MqttParamChange, 1, "New MQTT topic", appConfig.mqttTopic);
    }

    if (server.hasArg("temperatureRefreshInterval")){
      os_timer_disarm(&temperatureTimer);
      appConfig.temperatureRefreshInterval = atoi(server.arg("temperatureRefreshInterval").c_str());
      os_timer_arm(&temperatureTimer, appConfig.temperatureRefreshInterval * 1000, true);
      LogEvent(EVENTCATEGORIES::TemperatureInterval, 3, "New temperature refresh interval", server.arg("temperatureRefreshInterval").c_str());
    }    

    if (mqttDirty)
      PSclient.disconnect();

    saveSettings();
    ESP.reset();

  }

  File f = LittleFS.open("/pageheader.html", "r");
  String headerString;
  if (f.available()) headerString = f.readString();
  f.close();

  time_t localTime = timezones[appConfig.timeZone]->toLocal(now(), &tcr);

  f = LittleFS.open("/generalsettings.html", "r");

  String s, htmlString, timezoneslist;

  char ss[2];

  for (signed char i = 0; i < sizeof(tzDescriptions)/sizeof(tzDescriptions[0]); i++) {
    itoa(i, ss, DEC);
    timezoneslist+="<option ";
    if (appConfig.timeZone == i){
      timezoneslist+= "selected ";
    }
    timezoneslist+= "value=\"";
    timezoneslist+=ss;
    timezoneslist+="\">";

    timezoneslist+= tzDescriptions[i];

    timezoneslist+="</option>";
    timezoneslist+="\n";
  }

  while (f.available()){
    s = f.readStringUntil('\n');

    if (s.indexOf("%pageheader%")>-1) s.replace("%pageheader%", headerString);
    if (s.indexOf("%year%")>-1) s.replace("%year%", (String)year(localTime));
    if (s.indexOf("%mqtt-servername%")>-1) s.replace("%mqtt-servername%", appConfig.mqttServer);
    if (s.indexOf("%mqtt-port%")>-1) s.replace("%mqtt-port%", String(appConfig.mqttPort));
    if (s.indexOf("%mqtt-topic%")>-1) s.replace("%mqtt-topic%", appConfig.mqttTopic);
    if (s.indexOf("%timezoneslist%")>-1) s.replace("%timezoneslist%", timezoneslist);
    if (s.indexOf("%friendlyname%")>-1) s.replace("%friendlyname%", appConfig.friendlyName);
    if (s.indexOf("%heartbeatinterval%")>-1) s.replace("%heartbeatinterval%", (String)appConfig.heartbeatInterval);

    String searchString = "value=\"" + (String)appConfig.temperatureRefreshInterval + "\"";

    if(s.indexOf(searchString)>-1) s.replace(searchString, searchString + " selected");

    htmlString+=s;
  }
  f.close();
  server.send(200, "text/html", htmlString);

  LogEvent(EVENTCATEGORIES::PageHandler, 2, "Page served", "generalsettings.html");
}

void handleNetworkSettings() {
  LogEvent(EVENTCATEGORIES::PageHandler, 1, "Page requested", "networksettings.html");

  if (!is_authenticated()){
     String header = "HTTP/1.1 301 OK\r\nLocation: /login.html\r\nCache-Control: no-cache\r\n\r\n";
     server.sendContent(header);
     return;
   }

  if (server.method() == HTTP_POST){  //  POST
    if (server.hasArg("ssid")){
      strcpy(appConfig.ssid, server.arg("ssid").c_str());
      strcpy(appConfig.password, server.arg("password").c_str());
      saveSettings();

      isAccessPoint=false;
      connectionState = STATE_CHECK_WIFI_CONNECTION;
      WiFi.disconnect(false);

      ESP.reset();
    }
  }

  File f = LittleFS.open("/pageheader.html", "r");

  String headerString;

  if (f.available()) headerString = f.readString();
  f.close();

  time_t localTime = timezones[appConfig.timeZone]->toLocal(now(), &tcr);

  f = LittleFS.open("/networksettings.html", "r");
  String s, htmlString, wifiList;

  byte numberOfNetworks = WiFi.scanNetworks();
  for (size_t i = 0; i < numberOfNetworks; i++) {
    wifiList+="<div class=\"radio\"><label><input ";
    if (i==0) wifiList+="id=\"ssid\" ";

    wifiList+="type=\"radio\" name=\"ssid\" value=\"" + WiFi.SSID(i) + "\">" + WiFi.SSID(i) + "</label></div>";
  }

  while (f.available()){
    s = f.readStringUntil('\n');

    if (s.indexOf("%pageheader%")>-1) s.replace("%pageheader%", headerString);
    if (s.indexOf("%year%")>-1) s.replace("%year%", (String)year(localTime));
    if (s.indexOf("%wifilist%")>-1) s.replace("%wifilist%", wifiList);
      htmlString+=s;
    }
    f.close();
  server.send(200, "text/html", htmlString);

  LogEvent(EVENTCATEGORIES::PageHandler, 2, "Page served", "networksettings.html");
}

void handleTools() {
  LogEvent(EVENTCATEGORIES::PageHandler, 1, "Page requested", "tools.html");

  if (!is_authenticated()){
     String header = "HTTP/1.1 301 OK\r\nLocation: /login.html\r\nCache-Control: no-cache\r\n\r\n";
     server.sendContent(header);
     return;
   }

  if (server.method() == HTTP_POST){  //  POST

    if (server.hasArg("reset")){
      LogEvent(EVENTCATEGORIES::Reboot, 1, "Reset", "");
      defaultSettings();
      ESP.reset();
    }

    if (server.hasArg("restart")){
      LogEvent(EVENTCATEGORIES::Reboot, 2, "Restart", "");
      ESP.reset();
    }
  }

  File f = LittleFS.open("/pageheader.html", "r");
  String headerString;
  if (f.available()) headerString = f.readString();
  f.close();

  time_t localTime = timezones[appConfig.timeZone]->toLocal(now(), &tcr);

  f = LittleFS.open("/tools.html", "r");

  String s, htmlString;

  while (f.available()){
    s = f.readStringUntil('\n');

    if (s.indexOf("%pageheader%")>-1) s.replace("%pageheader%", headerString);
    if (s.indexOf("%year%")>-1) s.replace("%year%", (String)year(localTime));

      htmlString+=s;
    }
    f.close();
  server.send(200, "text/html", htmlString);

  LogEvent(EVENTCATEGORIES::PageHandler, 2, "Page served", "tools.html");
}

/*
    for (size_t i = 0; i < server.args(); i++) {
      Serial.print(server.argName(i));
      Serial.print(": ");
      Serial.println(server.arg(i));
    }
*/

void handleNotFound(){
  String message = "File Not Found\n\n";
  message += "URI: ";
  message += server.uri();
  message += "\nMethod: ";
  message += (server.method() == HTTP_GET)?"GET":"POST";
  message += "\nArguments: ";
  message += server.args();
  message += "\n";
  for (uint8_t i=0; i<server.args(); i++){
    message += " " + server.argName(i) + ": " + server.arg(i) + "\n";
  }
  server.send(404, "text/plain", message);
}

void SendHeartbeat(){
  if (PSclient.connected()){

    time_t localTime = timezones[appConfig.timeZone]->toLocal(now(), &tcr);

    const size_t capacity = JSON_OBJECT_SIZE(3) + JSON_OBJECT_SIZE(6) + 180;
    StaticJsonDocument<capacity> doc;

    doc["Time"] = DateTimeToString(localTime);
    doc["Node"] = ESP.getChipId();
    doc["Freeheap"] = ESP.getFreeHeap();
    doc["FriendlyName"] = appConfig.friendlyName;
    doc["HeartbeatInterval"] = appConfig.heartbeatInterval;

    JsonObject wifiDetails = doc.createNestedObject("Wifi");
    wifiDetails["SSId"] = String(WiFi.SSID());
    wifiDetails["MACAddress"] = String(WiFi.macAddress());
    wifiDetails["IPAddress"] = WiFi.localIP().toString();

    #ifdef __debugSettings
    serializeJsonPretty(doc,Serial);
    Serial.println();
    #endif

    String myJsonString;

    serializeJson(doc, myJsonString);

    PSclient.publish((MQTT_CUSTOMER + String("/") + MQTT_PROJECT + "/" + appConfig.mqttTopic + "/HEARTBEAT").c_str(), myJsonString.c_str(), false);
  }

  needsHeartbeat = false;
}

void ReadTemperatures(){
    sensors.requestTemperatures(); // Send the command to get temperatures

    for (size_t i = 0; i < oneWireDevicesCount; i++) {
      thermometers[i].measuredTemperatureC = sensors.getTempC(thermometers[i].deviceAddress);
      if (thermometers[i].measuredTemperatureC!=-127){
        PSclient.publish((MQTT_CUSTOMER + String("/") + MQTT_PROJECT + String("/") + appConfig.mqttTopic + "/thermometers/" + OneWireDeviceAddress2HEX(thermometers[i].deviceAddress, ':')).c_str(), (String(thermometers[i].measuredTemperatureC)).c_str(), false );
        LogEvent(ReadTemp, 1, "Measurement", "1Wire: " + thermometers[i].FriendlyName + ": " + String(thermometers[i].measuredTemperatureC));
      }
    }

}

void mqtt_callback(char* topic, byte* payload, unsigned int length) {

  Serial.print("Topic:\t\t");
  Serial.println(topic);

  Serial.print("Payload:\t");
  for (unsigned int i = 0; i < length; i++) {
    Serial.print((char)payload[i]);
  }
  Serial.println();

  StaticJsonDocument<JSON_MQTT_COMMAND_SIZE> doc;
  DeserializationError error = deserializeJson(doc, payload);

  if (error) {
    Serial.println("Failed to parse incoming string.");
    Serial.println(error.c_str());
    for (size_t i = 0; i < 10; i++) {
      digitalWrite(CONNECTION_STATUS_LED_GPIO, !digitalRead(CONNECTION_STATUS_LED_GPIO));
      delay(50);
    }
  }
  else{
    //  It IS a JSON string

    #ifdef __debugSettings
    serializeJsonPretty(doc,Serial);
    Serial.println();
    #endif

    //  reset
    if (doc.containsKey("reset")){
      LogEvent(EVENTCATEGORIES::MqttMsg, 1, "Reset", "");
      defaultSettings();
      ESP.reset();
    }

    //  restart
    if (doc.containsKey("restart")){
      LogEvent(EVENTCATEGORIES::MqttMsg, 2, "Restart", "");
      ESP.reset();
    }
  }

}

ICACHE_RAM_ATTR void PIR_Interrupt_Handler(){
  pirInputs[0].changed = true;
  pirInputs[0].value = digitalRead(pirInputs[0].gpio) == 1;
}

void setup() {
  delay(1); //  Needed for PlatformIO serial monitor
  Serial.begin(DEBUG_SPEED);
  Serial.setDebugOutput(false);
  Serial.print("\n\n\n\rBooting node:     ");
  Serial.print(ESP.getChipId());
  Serial.println("...");

  String FirmwareVersionString = String(FIRMWARE_VERSION) + " @ " + String(__TIME__) + " - " + String(__DATE__);

  Serial.println("Hardware ID:      " + (String)HARDWARE_ID);
  Serial.println("Hardware version: " + (String)HARDWARE_VERSION);
  Serial.println("Software ID:      " + (String)FIRMWARE_ID);
  Serial.println("Software version: " + FirmwareVersionString);
  Serial.println();

  //  File system
  if (!LittleFS.begin()){
    Serial.println("Error: Failed to initialize the filesystem!");
  }

  if (!loadSettings(appConfig)) {
    Serial.println("Failed to load config, creating default settings...");
    defaultSettings();
  } else {
    Serial.println("Config loaded.");
  }

  WiFi.hostname(defaultSSID);

  //  Digital inputs
  for (size_t i = 0; i < sizeof(digitalInputs)/sizeof(digitalInputs[0]); i++) pinMode(digitalInputs[i].gpio, INPUT_PULLUP);

  //  PIR sensors
  attachInterrupt(digitalPinToInterrupt(pirInputs[0].gpio), PIR_Interrupt_Handler, CHANGE );
  
  //  OneWire
  Serial.print("Locating 1-wire devices...");
  sensors.begin();
  Serial.print("Found ");
  oneWireDevicesCount = sensors.getDeviceCount();
  Serial.print(oneWireDevicesCount, DEC);
  Serial.println(" device(s).");

  if (oneWireDevicesCount > 0){
    for (size_t i = 0; i < oneWireDevicesCount; i++) {
      if (!sensors.getAddress(thermometers[i].deviceAddress, 0)) Serial.println("Unable to find address for Device " + (String)i);

      sensors.getAddress(thermometers[i].deviceAddress, i);
      Serial.print("Device ");
      Serial.print(i);
      Serial.print(":\t");
      Serial.print(OneWireDeviceAddress2HEX(thermometers[i].deviceAddress,':'));
      Serial.println();
      sensors.setResolution(thermometers[i].deviceAddress, DS1820_RESOLUTION);
      thermometers[i].FriendlyName = OneWireDeviceAddress2HEX(thermometers[i].deviceAddress, ':');
    }
  }

  //  GPIO
  pinMode(CONNECTION_STATUS_LED_GPIO, OUTPUT);
  digitalWrite(CONNECTION_STATUS_LED_GPIO, HIGH);

  //  OTA
  ArduinoOTA.onStart([]() {
    Serial.println("OTA started.");
  });

  ArduinoOTA.onEnd([]() {
    Serial.println("\nOTA finished.");
  });

  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
    Serial.printf("Progress: %u%%\r", (progress / (total / 100)));
    if (progress % OTA_BLINKING_RATE == 0){
      if (digitalRead(CONNECTION_STATUS_LED_GPIO)==HIGH)
        digitalWrite(CONNECTION_STATUS_LED_GPIO, LOW);
        else
        digitalWrite(CONNECTION_STATUS_LED_GPIO, HIGH);
    }
  });

  ArduinoOTA.onError([](ota_error_t error) {
    Serial.printf("Error[%u]: ", error);
    if (error == OTA_AUTH_ERROR) Serial.println("Authentication failed.");
    else if (error == OTA_BEGIN_ERROR) Serial.println("Begin failed.");
    else if (error == OTA_CONNECT_ERROR) Serial.println("Connect failed.");
    else if (error == OTA_RECEIVE_ERROR) Serial.println("Receive failed.");
    else if (error == OTA_END_ERROR) Serial.println("End failed.");
  });

  ArduinoOTA.begin();

  Serial.println();

  server.on("/", handleStatus);
  server.on("/status.html", handleStatus);
  server.on("/generalsettings.html", handleGeneralSettings);
  server.on("/networksettings.html", handleNetworkSettings);
  server.on("/sensors.html", handleSensors);
  server.on("/tools.html", handleTools);
  server.on("/login.html", handleLogin);

  server.onNotFound(handleNotFound);

  //  Web server
  if (MDNS.begin("esp8266")) {
    Serial.println("MDNS responder started.");
  }

  //  Start HTTP (web) server
  server.begin();
  Serial.println("HTTP server started.");

  //  Authenticate HTTP requests
  const char * headerkeys[] = {"User-Agent","Cookie"} ;
  size_t headerkeyssize = sizeof(headerkeys)/sizeof(char*);
  server.collectHeaders(headerkeys, headerkeyssize );

  //  Timers
  os_timer_setfn(&heartbeatTimer, heartbeatTimerCallback, NULL);
  os_timer_setfn(&temperatureTimer, temperatureTimerCallback, NULL);

  os_timer_arm(&heartbeatTimer, appConfig.heartbeatInterval * 1000, true);
  os_timer_arm(&temperatureTimer, appConfig.temperatureRefreshInterval * 1000, true);

  //  Randomizer
  SetRandomSeed();

  // Set the initial connection state
  connectionState = STATE_CHECK_WIFI_CONNECTION;

  //  Read temperatures as soon as we have connection
  needsTemperature = true;

}

void loop(){

  if (isAccessPoint){
    if (!isAccessPointCreated){
      Serial.print("Could not connect to ");
      Serial.print(appConfig.ssid);
      Serial.println("\r\nReverting to Access Point mode.");

      delay(500);

      WiFi.mode(WiFiMode::WIFI_AP);
      WiFi.softAP(defaultSSID, DEFAULT_PASSWORD);

      IPAddress myIP;
      myIP = WiFi.softAPIP();
      isAccessPointCreated = true;

      Serial.println("Access point created. Use the following information to connect to the ESP device, then follow the on-screen instructions to connect to a different wifi network:");

      Serial.print("SSID:\t\t\t");
      Serial.println(defaultSSID);

      Serial.print("Password:\t\t");
      Serial.println(DEFAULT_PASSWORD);

      Serial.print("Access point address:\t");
      Serial.println(myIP);

      Serial.println();
      Serial.println("Note: The device will reset in 5 minutes.");


      os_timer_setfn(&accessPointTimer, accessPointTimerCallback, NULL);
      os_timer_arm(&accessPointTimer, ACCESS_POINT_TIMEOUT, true);
      os_timer_disarm(&heartbeatTimer);
    }
    server.handleClient();
  }
  else{
    switch (connectionState) {

      // Check the WiFi connection
      case STATE_CHECK_WIFI_CONNECTION:

        // Are we connected ?
        if (WiFi.status() != WL_CONNECTED) {
          // Wifi is NOT connected
          digitalWrite(CONNECTION_STATUS_LED_GPIO, HIGH);
          connectionState = STATE_WIFI_CONNECT;
        } else  {
          // Wifi is connected so check Internet
          digitalWrite(CONNECTION_STATUS_LED_GPIO, LOW);
          connectionState = STATE_CHECK_INTERNET_CONNECTION;
          
          server.handleClient();
        }
        break;

      // No Wifi so attempt WiFi connection
      case STATE_WIFI_CONNECT:
        {
          // Indicate NTP no yet initialized
          ntpInitialized = false;

          digitalWrite(CONNECTION_STATUS_LED_GPIO, HIGH);
          Serial.printf("Trying to connect to WIFI network: %s", appConfig.ssid);

          // Set station mode
          WiFi.mode(WIFI_STA);

          // Start connection process
          WiFi.begin(appConfig.ssid, appConfig.password);

          // Initialize iteration counter
          uint8_t attempt = 0;

          while ((WiFi.status() != WL_CONNECTED) && (attempt++ < WIFI_CONNECTION_TIMEOUT)) {
            digitalWrite(CONNECTION_STATUS_LED_GPIO, LOW);
            Serial.print(".");
            delay(50);
            digitalWrite(CONNECTION_STATUS_LED_GPIO, HIGH);
            delay(950);
          }
          if (attempt >= WIFI_CONNECTION_TIMEOUT) {
            Serial.println();
            Serial.println("Could not connect to WiFi");
            delay(100);

            isAccessPoint=true;

            break;
          }
          digitalWrite(CONNECTION_STATUS_LED_GPIO, LOW);
          Serial.println(" Success!");
          Serial.print("IP address: ");
          Serial.println(WiFi.localIP());
          connectionState = STATE_CHECK_INTERNET_CONNECTION;
        }
        break;

      case STATE_CHECK_INTERNET_CONNECTION:

        // Do we have a connection to the Internet ?
        if (checkInternetConnection()) {
          // We have an Internet connection

          if (!ntpInitialized) {
            // We are connected to the Internet for the first time so set NTP provider
            initNTP();

            ntpInitialized = true;

            Serial.println("Connected to the Internet.");
          }

          connectionState = STATE_INTERNET_CONNECTED;
        } else  {
          connectionState = STATE_CHECK_WIFI_CONNECTION;
        }
        break;

      case STATE_INTERNET_CONNECTED:

        ArduinoOTA.handle();

        if (!PSclient.connected()) {
          PSclient.setServer(appConfig.mqttServer, appConfig.mqttPort);
          if (PSclient.connect(defaultSSID, (MQTT_CUSTOMER + String("/") + MQTT_PROJECT + String("/") + appConfig.mqttTopic + "/STATE").c_str(), 0, true, "offline" )){
            PSclient.setCallback(mqtt_callback);

            PSclient.subscribe((MQTT_CUSTOMER + String("/") + MQTT_PROJECT + String("/") + appConfig.mqttTopic + "/cmnd").c_str(), 0);

            PSclient.publish((MQTT_CUSTOMER + String("/") + MQTT_PROJECT + String("/") + appConfig.mqttTopic + "/STATE").c_str(), "online", true);
            LogEvent(EVENTCATEGORIES::Conn, 1, "Node online", WiFi.localIP().toString());
          }
        }

        //  Button(s)
        for (size_t i = 0; i < sizeof(digitalInputs)/sizeof(digitalInputs[0]); i++){
          digitalInputs[i].buttonPattern = digitalInputs[i].buttonPattern<<1;
          if(digitalRead(digitalInputs[i].gpio) == 1) digitalInputs[i].buttonPattern = digitalInputs[i].buttonPattern | 1;

          if(digitalInputs[i].buttonPattern==0){
            if ((digitalInputs[i].buttonState != BUTTON_PRESSED) && (digitalInputs[i].buttonState != BUTTON_LONG_PRESSED)) buttonMillis =  millis() % 65536;
            if (millis() % 65536 - buttonMillis > BUTTON_LONG_PRESS_TRESHOLD) digitalInputs[i].buttonState = BUTTON_LONG_PRESSED;
              else digitalInputs[i].buttonState = BUTTON_PRESSED;
          }
          else if(digitalInputs[i].buttonPattern==0b11111111111111111111111111111111){
            digitalInputs[i].buttonState = BUTTON_NOT_PRESSED;
          }else{
            digitalInputs[i].buttonState = BUTTON_BOUNCING;
          }
        }

        //  PIR sensors
        for (size_t i = 0; i < sizeof(pirInputs)/sizeof(pirInputs[0]); i++){
          if (pirInputs[i].changed){
            if (PSclient.connected()){
              String payload;
              if ( pirInputs[i].value == 1 ) payload = "on";
              else payload = "off";
              PSclient.publish((MQTT_CUSTOMER + String("/") + MQTT_PROJECT + "/" + appConfig.mqttTopic + "/PIR0").c_str(), payload.c_str(), false);
              pirInputs[i].changed = false;
            }
          }
        }

        if (PSclient.connected()){
          PSclient.loop();
        }

        if (needsHeartbeat){
          SendHeartbeat();
          needsHeartbeat = false;
        }

        if (needsTemperature){
          ReadTemperatures();
          needsTemperature = false;
        }

        // Set next connection state
        connectionState = STATE_CHECK_WIFI_CONNECTION;
        break;
    }

  }
}
