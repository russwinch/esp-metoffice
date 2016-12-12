/*
  Retrieving weather forecast from the UK's Met Office: http://www.metoffice.gov.uk/
  Russ Winch - December 2016
  https://github.com/russwinch/esp-metoffice.git

  - Based on the Weather Underground sketch by bbx10: https://gist.github.com/bbx10/149bba466b1e2cd887bf
  - Using the excellent and well documented Arduino JSON library: https://github.com/bblanchon/ArduinoJson
  - Met Office code definitions for weather types: http://www.metoffice.gov.uk/datapoint/support/documentation/code-definitions
  - Tested on Wemos D1 V2

  ***TODO***
  display mechanism for forecast (OLED?)
  retrieve time and use this to decide which forecast to show: day/night/next day
  add option to retrieve current observation
  ability to determine / set the current location (by IP?)
   
*/

#include <ESP8266WiFi.h>
#include <ArduinoJson.h>

//Wifi settings
const char ssid[]     = "aaaaaaaaaaa";
const char password[] = "bbbbbbbbbbb";

//Met Office settings
//register for Met Office Datapoint here: http://www.metoffice.gov.uk/datapoint
#define METOFFICE "datapoint.metoffice.gov.uk"
#define MO_API_KEY "cccccccccccccccc"
#define MO_SITEID "351713" //Hackney
//To obtain a siteID, call the following to return JSON of all sites:
//http://datapoint.metoffice.gov.uk/public/data/val/wxfcs/all/json/sitelist?key=<YourAPIKey>

/*  Met Office fair use policy:
    You may make no more than 5000 data requests per day; and
    You may make no more than 100 data requests per minute.*/
// *****1 minute between update checks.
#define DELAY_NORMAL    (1*60*1000)
// *****2 minute delay between updates after an error
#define DELAY_ERROR     (2*60*1000)

char *weatherTypes[] = {"Clear night", "Sunny day", "Partly cloudy (night)", "Partly cloudy (day)", "Not used", "Mist", "Fog", "Cloudy", "Overcast", "Light rain shower (night)", "Light rain shower (day)", "Drizzle", "Light rain", "Heavy rain shower (night)", "Heavy rain shower (day)", "Heavy rain", "Sleet shower (night)", "Sleet shower (day)", "Sleet", "Hail shower (night)", "Hail shower (day)", "Hail", "Light snow shower (night)", "Light snow shower (day)", "Light snow", "Heavy snow shower (night)", "Heavy snow shower (day)", "Heavy snow", "Thunder shower (night)", "Thunder shower (day)", "Thunder"};

// HTTP request
const char METOFFICE_REQ[] =
  "GET /public/data/val/wxfcs/all/json/" MO_SITEID "?res=daily&key=" MO_API_KEY " HTTP/1.1\r\n"
//  "GET /public/data/val/wxfcs/all/json/" MO_SITEID "?res=3hourly&key=" MO_API_KEY " HTTP/1.1\r\n" ///testing 3 hourly
  "User-Agent: ESP8266/0.1\r\n"
  "Accept: */*\r\n"
  "Host: " METOFFICE "\r\n"
  "Connection: close\r\n"
  "\r\n";

//response buffer for the JSON
static char respBuf[4096];

//current date & time from http request
String timeStamp;

void setup() {
  Serial.begin(115200);

  // Connect to WiFi network
  Serial.println();
  Serial.print(F("Connecting to "));
  Serial.println(ssid);

  WiFi.begin(ssid, password);

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(F("."));
  }
  Serial.println();
  Serial.println(F("WiFi connected"));
  Serial.println(F("IP address: "));
  Serial.println(WiFi.localIP());
}

void loop() {
  // Open socket to server port 80
  Serial.println();
  Serial.print(F("Connecting to "));
  Serial.println(METOFFICE);

  // Use WiFiClient class to create TCP connections
  WiFiClient httpclient;
  const int httpPort = 80;
  if (!httpclient.connect(METOFFICE, httpPort)) {
    Serial.println(F("connection failed"));
    delay(DELAY_ERROR);
    return;
  }

  // This will send the http request to the server
  Serial.print(METOFFICE_REQ);
  httpclient.print(METOFFICE_REQ);
  httpclient.flush();

  // Collect http response headers and content from the Met Office
  // HTTP headers are (mostly) discarded.
  // The content is formatted in JSON and is left in respBuf.
  int respLen = 0;
  bool skip_headers = true;
  while (httpclient.connected() || httpclient.available()) {
    if (skip_headers) {
      String aLine = httpclient.readStringUntil('\n');
      Serial.println(aLine);
      if (aLine.substring(0,4) == "Date") timeStamp = aLine.substring(6); //looking for Date at the start of the line
      if (aLine.length() <= 1) { // Blank line denotes end of headers
        skip_headers = false;
      }
    }
    else {
      int bytesIn;
      bytesIn = httpclient.read((uint8_t *)&respBuf[respLen], sizeof(respBuf) - respLen);
      Serial.print(F("bytesIn ")); Serial.println(bytesIn);
      if (bytesIn > 0) {
        respLen += bytesIn;
        if (respLen > sizeof(respBuf)) respLen = sizeof(respBuf);
      }
      else if (bytesIn < 0) {
        Serial.print(F("read error "));
        Serial.println(bytesIn);
      }
    }
    delay(1);
  }
  httpclient.stop();

  if (respLen >= sizeof(respBuf)) {
    Serial.print(F("respBuf overflow "));
    Serial.println(respLen);
    delay(DELAY_ERROR);
    return;
  }
  // Terminate the C string
  respBuf[respLen++] = '\0';
  Serial.print(F("respLen "));
  Serial.println(respLen);
  //Serial.println(respBuf);

  if (showWeather(respBuf)) {
    delay(DELAY_NORMAL);
  }
  else {
    delay(DELAY_ERROR);
  }
}

bool showWeather(char *json)
{
  //  StaticJsonBuffer<3*1024> jsonBuffer;
  DynamicJsonBuffer jsonBuffer; //not recommended but seems to work

  // Skip characters until first '{' found
  // Ignore chunked length, if present
  char *jsonstart = strchr(json, '{');
  Serial.print(F("jsonstart :")); Serial.println(jsonstart);
  if (jsonstart == NULL) {
    Serial.println(F("JSON data missing"));
    return false;
  }
  json = jsonstart;

  // Parse JSON
  JsonObject& root = jsonBuffer.parseObject(json);
  if (!root.success()) {
    Serial.println(F("jsonBuffer.parseObject() failed"));
    return false;
  }
  Serial.println("parsed successfully");
  Serial.println();

  // Extract weather info from parsed JSON
  JsonObject& DV = root["SiteRep"]["DV"];
  JsonObject& location = root["SiteRep"]["DV"]["Location"];
  JsonObject& todayDay = root["SiteRep"]["DV"]["Location"]["Period"][0]["Rep"][0];
  JsonObject& todayNight = root["SiteRep"]["DV"]["Location"]["Period"][0]["Rep"][1];

  Serial.println(timeStamp); //current date and time
  const char *siteName = location["name"];
  Serial.print("Location: "); Serial.println(siteName);
  String dataTimeStamp = DV["dataDate"];
//  Serial.print("Data timestamp: "); Serial.println(dataTimeStamp);
  Serial.print("Data date: "); Serial.println(dataTimeStamp.substring(0,10));
  Serial.print("Data time: "); Serial.println(dataTimeStamp.substring(11,16));
  const int dayMaxTemp = todayDay["Dm"];
  Serial.print("Max temp: "); Serial.print(dayMaxTemp); Serial.println(" deg C");
  const int nightMinTemp = todayNight["Nm"];
  Serial.print("Min temp: "); Serial.print(nightMinTemp); Serial.println(" deg C");
  const int dayType = todayDay["W"];
  Serial.print("Day weather type: "); Serial.println(weatherTypes[dayType]);
  const int nightType = todayNight["W"];
  Serial.print("Night weather type: "); Serial.println(weatherTypes[nightType]);
  const int dayPProb = todayDay["PPd"];
  Serial.print("Day precipitation probability: "); Serial.print(dayPProb); Serial.println("%");
  const int nightPProb = todayNight["PPn"];
  Serial.print("Night precipitation probability: "); Serial.print(nightPProb); Serial.println("%");

  return true;
}
