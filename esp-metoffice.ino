/*
  Retrieving weather forecast from the UK's Met Office: http://www.metoffice.gov.uk/
  Russ Winch - December 2016
  https://github.com/russwinch/esp-metoffice.git

  - Based on the Weather Underground sketch by bbx10: https://gist.github.com/bbx10/149bba466b1e2cd887bf
  - Using the excellent and well documented Arduino JSON library: https://github.com/bblanchon/ArduinoJson
  - Met Office code definitions for weather types: http://www.metoffice.gov.uk/datapoint/support/documentation/code-definitions
  - Tested on Wemos D1 V2 with oled sheild
  - Using the modified version of the adafruit OLED driver by mcauser: https://github.com/mcauser/Adafruit_SSD1306/tree/esp8266-64x48
  - Using the fantastic WiFiManager to connect wifi, present AP and save settings to FS: https://github.com/tzapu/WiFiManager

  /*********************************************************************
  This is an example for our Monochrome OLEDs based on SSD1306 drivers

  Pick one up today in the adafruit shop!
  ------> http://www.adafruit.com/category/63_98

  This example is for a 128x64 size display using I2C to communicate
  3 pins are required to interface (2 I2C and one reset)

  Adafruit invests time and resources providing this open source code,
  please support Adafruit and open-source hardware by purchasing
  products from Adafruit!

  Written by Limor Fried/Ladyada  for Adafruit Industries.
  BSD license, check license.txt for more information
  All text above, and the splash screen must be included in any redistribution
*********************************************************************/

/****TO DO****
  - decide which forecast to show : day / night / next day
  - produce more icons - find a set and batch all when size determined
  - implement reading from ldr to adjust led brightness
  - implement timeout to turn the oled off after a delay
  - add mode to show last data dates and times
  - animate icons
  - add more logging to oled screen and implement logging bool
  - led feedback on updates (switchable)

  - ability to determine / set the current location (by IP ? )
  - implement weather warnings
  - postcode to coordinate search with postcodes.io
  - look up nearest sites using siteslist and pythag
*/

#include <ESP8266WiFi.h>
#include <ArduinoJson.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

// checking the right version of the OLED library is included, should support 64x48
//#if (SSD1306_LCDHEIGHT != 48)
//#error("Height incorrect, please fix Adafruit_SSD1306.h!");
//#endif

//***now 128x64!

#include "credentials.h" //credentials file with wifi settings and Met Office API key
#include "bitmaps.h" //bitmaps file

//enable logging
bool logging = true;
//bool logging = false;

// pin definitions
const int redPin = D7;
const int greenPin = D6;
const int bluePin = D5;
const int switchPin = D8; //built in pull-down
const int ldrPin = A0;
// SCL GPIO5 (D1)
// SDA GPIO4 (D2)
#define OLED_RESET D3  // GPIO0

Adafruit_SSD1306 display(OLED_RESET);

//define your default values here, if there are different values in config.json, they are overwritten.
char forecast_id[8]; //forecast site ID
char observation_id[6]; //observation site ID
char api_key[40]; //api key

//wifi management
#include "wifiManagement.h"

/* Met Office fair use policy:
  You may make no more than 5000 data requests per day; and
  You may make no more than 100 data requests per minute. */

// 1 minute between update checks.
long updateDelay = (1 * 60000);

// string variables
const char *weatherTypes[] = {"Clear night", "Sunny day", "Partly cloudy (night)", "Partly cloudy (day)", "Not used", "Mist", "Fog", "Cloudy", "Overcast", "Light rain shower (night)", "Light rain shower (day)", "Drizzle", "Light rain", "Heavy rain shower (night)", "Heavy rain shower (day)", "Heavy rain", "Sleet shower (night)", "Sleet shower (day)", "Sleet", "Hail shower (night)", "Hail shower (day)", "Hail", "Light snow shower (night)", "Light snow shower (day)", "Light snow", "Heavy snow shower (night)", "Heavy snow shower (day)", "Heavy snow", "Thunder shower (night)", "Thunder shower (day)", "Thunder"};
const char *compass[] = {"E", "ESE", "SE", "SSE", "S", "SSW", "SW", "WSW", "W", "WNW", "NW", "NNW", "N", "NNE", "NE", "ENE"};
const char *months[] = { "Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec" };

// Met Office settings
#define METOFFICE "datapoint.metoffice.gov.uk"

// current date & time from http request
char timeStamp[36];// = "Date: Tue, 27 Dec 2016 18:39:36 GMT";

// observation values
int obvTime; //time of observation
int currentTemp; //current temperature deg C
int currentType; //current weather type
int currentWindSpeed;
char currentWindDir[4];

// forecast values
int dayMaxTemp;
int nightMinTemp;
int dayType;
int nightType;
int dayPProb;
int nightPProb;
int rainTime;
long rainDate;
int rainType;
int rainProb;
int rainGust;

// millis() of last update attempt
unsigned long lastUpdateAttempt;

// modes
int currentMode = 0;
int totalModes = 3;

// switch
bool switchReading;
bool lastSwitchReading;
unsigned long lastDebounceTime = 0;
unsigned long debounceDelay = 50;

// leds
int RGB;
//int targetRGB;

void setup() {
  //inputs
  //  pinMode (ldrPin, INPUT);
  pinMode (switchPin, INPUT);

  //outputs
  pinMode (redPin, OUTPUT);
  pinMode (greenPin, OUTPUT);
  pinMode (bluePin, OUTPUT);
  pinMode(LED_BUILTIN, OUTPUT);

  //turn leds off
  analogWrite(LED_BUILTIN, 1023);
  updateRGB(0);

  Serial.begin(115200);

  // by default, we'll generate the high voltage from the 3.3v line internally! (neat!)
  display.begin(SSD1306_SWITCHCAPVCC, 0x3C);  // initialize with the I2C addr 0x3C (for the 64x48)

  // Show image buffer on the display hardware.
  // Since the buffer is intialized with an Adafruit splashscreen
  // internally, this will display the splashscreen.
  display.display();
  delay(500);
  display.clearDisplay(); // Clear the buffer.
  display.setTextSize(1);
  display.setTextColor(WHITE);
  display.setCursor(0, 0);
  display.display();

  if (manageWifi()); //launch wifi using WifiManager

  //*****testing JSON read
  //  Serial.println("values from JSON");
  //  Serial.print("forecast ID: "); Serial.println(forecast_id); //forecast site ID
  //  Serial.print("observation ID: "); Serial.println(observation_id); //observation site ID
  //  Serial.print("API key: "); Serial.println(api_key); //api key

  if (updateData()) updateDisplay(0);
}

void loop() {
  if (readSwitch() == HIGH) {
    currentMode++;
    if (currentMode > (totalModes - 1)) currentMode = 0;
    updateDisplay(currentMode);
  }
  if (millis() - lastUpdateAttempt > updateDelay) {
    if (updateData()) updateDisplay(currentMode);
    lastUpdateAttempt = millis();
  }
}

bool updateData() {
  // original requests - Strings >:o[
  //  String OBSERVATION_REQ =
  //    "GET /public/data/val/wxobs/all/json/" + String(observation_id) + "?res=hourly&key=" + String(api_key) + " HTTP/1.1\r\n"
  //    "User-Agent: ESP8266/0.1\r\n"
  //    "Accept: */*\r\n"
  //    "Host: " METOFFICE "\r\n"
  //    "Connection: close\r\n"
  //    "\r\n";
  //
  //  String FORECAST_REQ =
  //    "GET /public/data/val/wxfcs/all/json/" + String(forecast_id) + "?res=daily&key=" + String(api_key) + " HTTP/1.1\r\n"
  //    "User-Agent: ESP8266/0.1\r\n"
  //    "Accept: */*\r\n"
  //    "Host: " METOFFICE "\r\n"
  //    "Connection: close\r\n"
  //    "\r\n";
  //
  //  String HOUR_FORECAST_REQ =
  //    "GET /public/data/val/wxfcs/all/json/" + String(forecast_id) + "?res=3hourly&key=" + String(api_key) + " HTTP/1.1\r\n"
  //    "User-Agent: ESP8266/0.1\r\n"
  //    "Accept: */*\r\n"
  //    "Host: " METOFFICE "\r\n"
  //    "Connection: close\r\n"
  //    "\r\n";


  //forecast http GET request components
  const char getFore[] = "GET /public/data/val/wxfcs/all/json/";
  const char getObv[] = "GET /public/data/val/wxobs/all/json/";
  const char threeHourly[] = "?res=3hourly&key=";
  const char daily[] = "?res=daily&key=";
  const char hourly[] = "?res=hourly&key=";
  const char getReq[] = " HTTP/1.1\r\n"
                        "User-Agent: ESP8266/0.1\r\n"
                        "Accept: */*\r\n"
                        "Host: " METOFFICE "\r\n"
                        "Connection: close\r\n"
                        "\r\n";

  //build 3hourly forecast request
  char hourly_req[200] = "";
  strcat (hourly_req, getFore);
  strcat (hourly_req, forecast_id);
  strcat (hourly_req, threeHourly);
  strcat (hourly_req, api_key);
  strcat (hourly_req, getReq);

  //build daily forecast request
  char daily_req[200] = "";
  strcat (daily_req, getFore);
  strcat (daily_req, forecast_id);
  strcat (daily_req, daily);
  strcat (daily_req, api_key);
  strcat (daily_req, getReq);

  //build observation request
  char obv_req[200] = "";
  strcat (obv_req, getObv);
  strcat (obv_req, observation_id);
  strcat (obv_req, hourly);
  strcat (obv_req, api_key);
  strcat (obv_req, getReq);



  //trying to retrieve list of sites - too much data...
  //  Serial.println("****REQ TEST***");
  //  Serial.println(obv_req);

  //  String FORECAST_SITE_REQ =
  //    "GET /public/data/val/wxfcs/all/json/sitelist?key=" + String(api_key) + " HTTP/1.1\r\n"
  //    "User-Agent: ESP8266/0.1\r\n"
  //    "Accept: */*\r\n"
  //    "Host: " METOFFICE "\r\n"
  //    "Connection: close\r\n"
  //    "\r\n";

  //    char* response = httpReq(FORECAST_SITE_REQ.c_str());
  char* response = httpReq(daily_req);
  if (forecast(response)) {
    response = httpReq(obv_req);
    if (observation(response)) {
      //      response = httpReq(HOUR_FORECAST_REQ.c_str());
      response = httpReq(hourly_req);
      if (hour_forecast(response)) {
        confirm(9);
        return true;
      }
    } else {
      confirm(1);
      return false;
    }
  }
}

char* httpReq (const char* req) {
  //  static char respBuf[25096]; // response buffer for the JSON
  static char respBuf[5096]; // response buffer for the JSON
  //  static char respBuf[4096]; // response buffer for the JSON

  Serial.println();
  Serial.print(F("Connecting to "));
  Serial.println(METOFFICE);
  analogWrite(LED_BUILTIN, 1000); //turn on built in LED dim

  WiFiClient httpclient; // Use WiFiClient class to create TCP connections
  const int httpPort = 80; // Open socket to server port 80
  if (!httpclient.connect(METOFFICE, httpPort)) {
    Serial.println(F("connection failed"));
    //    delay(DELAY_ERROR);
    //    return; ********************************** FIX THIS
  }
  analogWrite(LED_BUILTIN, 1023); //turn off built in LED

  // This will send the http request to the server
  Serial.print(req);
  httpclient.print(req);
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
      char dateCheckkStr[5];
      aLine.toCharArray(dateCheckkStr, 5);
      if (strcmp(dateCheckkStr, "Date") == 0) { //looking for "Date" at the start of the line
        aLine.toCharArray(timeStamp, 36);
      }
      if (aLine.length() <= 1) { // Blank line denotes end of headers
        skip_headers = false;
      }
    } else {
      int bytesIn;
      bytesIn = httpclient.read((uint8_t *)&respBuf[respLen], sizeof(respBuf) - respLen);
      Serial.print(".");
      //      Serial.print(F("bytesIn ")); Serial.println(bytesIn);
      if (bytesIn > 0) {
        Serial.println();
        Serial.print(F("bytesIn ")); Serial.println(bytesIn);
        respLen += bytesIn;
        if (respLen > sizeof(respBuf)) respLen = sizeof(respBuf);
      } else if (bytesIn < 0) {
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
    //    delay(DELAY_ERROR);
    //    return; ********************************** FIX THIS
  }
  respBuf[respLen++] = '\0'; // Terminate the C string
  Serial.println();
  Serial.print(F("respLen "));
  Serial.println(respLen);
  //Serial.println(respBuf);
  return respBuf;
}

JsonObject& parseJson(char *j) {
  //  StaticJsonBuffer<3*1024> jsonBuffer;
  DynamicJsonBuffer jsonBuffer; //not recommended but seems to work

  // Skip characters until first '{' found
  // Ignore chunked length, if present
  char *jsonstart = strchr(j, '{');
  Serial.print(F("jsonstart :")); Serial.println(jsonstart);
  if (jsonstart == NULL) {
    Serial.println(F("JSON data missing"));
    //    return false;
  }
  j = jsonstart;

  // Parse JSON
  JsonObject& r = jsonBuffer.parseObject(j);
  if (!r.success()) {
    Serial.println(F("jsonBuffer.parseObject() failed"));
    //    return false;
  }
  Serial.println("parsed successfully");
  Serial.println();
  return r;
}

bool forecast(char *json) {

  JsonObject& root = parseJson(json);
  
  Serial.println("Forecast");
  // Extract weather info from parsed JSON  
  JsonObject& DV = root["SiteRep"]["DV"];
  JsonObject& location = root["SiteRep"]["DV"]["Location"];
  //if night- retrieve the next morning
  JsonObject& todayDay = root["SiteRep"]["DV"]["Location"]["Period"][0]["Rep"][0];
  JsonObject& todayNight = root["SiteRep"]["DV"]["Location"]["Period"][0]["Rep"][1];

  
  //  Serial.println(timeStamp); //current date and time
  Serial.print("Current time: "); Serial.println(currentTime());
  Serial.print("Current date: "); Serial.println(currentDate());
  //  String siteName = location["name"]; // change to check if the name exists?
  //  if (siteName.length() != 0) {
  if (location.containsKey("name")) {
    //    (!temp.containsKey("$"))
    //    Serial.print("Location: "); Serial.println(siteName);
    Serial.print("Location: "); Serial.println(location["name"].asString());
    //    String dataTimeStamp = DV["dataDate"];
    //    Serial.print("Data time: "); Serial.println(dataTimeStamp.substring(11, 16));
    //    Serial.print("Data date: "); Serial.println(dataTimeStamp.substring(0, 10));

    //print day forecast
    for (JsonObject::iterator it = todayDay.begin(); it != todayDay.end(); ++it) {
      Serial.print(it->key);
      Serial.print(" : ");
      Serial.println(it->value.asString());
    }
    //print night forecast
    Serial.println();
    for (JsonObject::iterator it = todayNight.begin(); it != todayNight.end(); ++it) {
      Serial.print(it->key);
      Serial.print(" : ");
      Serial.println(it->value.asString());
    }

    dayMaxTemp = todayDay["Dm"];
    Serial.print("Max temp: "); Serial.print(dayMaxTemp); Serial.println(" deg C");
    nightMinTemp = todayNight["Nm"];
    Serial.print("Min temp: "); Serial.print(nightMinTemp); Serial.println(" deg C");
    dayType = todayDay["W"];
    Serial.print("Day weather type: "); Serial.println(weatherTypes[dayType]);
    nightType = todayNight["W"];
    Serial.print("Night weather type: "); Serial.println(weatherTypes[nightType]);
    dayPProb = todayDay["PPd"];
    Serial.print("Day precipitation probability: "); Serial.print(dayPProb); Serial.println("%");
    nightPProb = todayNight["PPn"];
    Serial.print("Night precipitation probability: "); Serial.print(nightPProb); Serial.println("%");
    return true;
  } else {
    Serial.println("JSON empty - no data returned");
    return false;
  }
}

bool hour_forecast(char *json) {
  JsonObject& root = parseJson(json);

  // Extract weather info from parsed JSON
  JsonObject& DV = root["SiteRep"]["DV"];
  JsonObject& location = root["SiteRep"]["DV"]["Location"];
  JsonObject& period = root["SiteRep"]["DV"]["Location"]["Period"];//[0]["Rep"][0];

  Serial.println("Rain forecast");
  //    Serial.println(timeStamp); //current date and time
  Serial.print("Current time: "); Serial.println(currentTime());
  Serial.print("Current date: "); Serial.println(currentDate());
  //  String siteName = location["name"];
  //  if (siteName.length() != 0) {
  if (location.containsKey("name")) {
    Serial.print("Location: "); Serial.println(location["name"].asString());
    //    String dataTimeStamp = DV["dataDate"];
    //    Serial.print("Data time: "); Serial.println(dataTimeStamp.substring(11, 16));
    //    Serial.print("Data date: "); Serial.println(dataTimeStamp.substring(0, 10));

    bool checking = true;
    const char *dateTemp;
    for (int d = 0; d < 5; d++) {
      //      Serial.println();
      //      Serial.print(d);
      //      Serial.print(" : ");
      for (int a = 0; a < 8; a++) {
        //        Serial.print(a);
        if (checking) {
          JsonObject& temp = root["SiteRep"]["DV"]["Location"]["Period"][d]["Rep"][a];
          if (temp["W"] >= 9) { //if weather type is 9: "Light rain shower" or above
            //          if (temp["W"] >= 3) { //testing rain
            rainTime = (temp["$"].as<int>() / 60) * 100; //time of rain
            rainType = temp["W"];
            rainProb = temp["Pp"];
            rainGust = temp["G"];
            //            String dateTemp;
            //            dateTemp = root["SiteRep"]["DV"]["Location"]["Period"][d]["value"].asString();
            dateTemp = root["SiteRep"]["DV"]["Location"]["Period"][d]["value"];
            char substr[9];
            strncpy(substr, dateTemp, 4);
            strncpy(substr + 4, dateTemp + 5, 2);
            strncpy(substr + 6, dateTemp + 8, 2);
            //            strncat(substr, dateTemp, 4);
            //            strncat(substr, dateTemp + 5, 2);
            //            strncat(substr, dateTemp + 8, 2);
            substr[8] = '\0'; //terminate
            //            rainDate = (dateTemp.substring(0, 4) + dateTemp.substring(5, 7) + dateTemp.substring(8, 10)).toInt();
            rainDate = atoi(substr); //convert to int
            // stop if rain forecast is in the future
            if ((rainDate == currentDate() && rainTime > currentTime()) || rainDate > currentDate()) checking = false;
          }
        }
      }
    }
    Serial.println();
    if (checking) Serial.println("No rain forecast in next 5 days");
    else {
      Serial.print("Rain time: "); Serial.println(rainTime);
      Serial.print("Rain date: "); Serial.println(rainDate);

      //    print rain forecast
      //    for (JsonObject::iterator it = rainDay.begin(); it != rainDay.end(); ++it) {
      //      Serial.print(it->key);
      //      Serial.print(" : ");
      //      Serial.println(it->value.asString());
      //    }
    }
    return true;
  } else {
    Serial.println("JSON empty - no data returned");
    return false;
  }
}

bool observation(char *json) {
  JsonObject& root = parseJson(json);

  // Extract weather info from parsed JSON
  JsonObject& DV = root["SiteRep"]["DV"];
  JsonObject& location = root["SiteRep"]["DV"]["Location"];

  Serial.println("Observation");
  //  Serial.println(timeStamp); //current date and time
  Serial.print("Current time: "); Serial.println(currentTime());
  Serial.print("Current date: "); Serial.println(currentDate());
  //  String siteName = location["name"]; //***convert to C string
  //  if (siteName.length() != 0) {
  if (location.containsKey("name")) {
    //**remove this print.asString at some point
    Serial.print("Location: "); Serial.println(location["name"].asString());
    int last; //last observation with weather type
    bool checking = true;
    for (int a = 0; a < 26; a++) {
      if (checking) {
        JsonObject& temp = root["SiteRep"]["DV"]["Location"]["Period"][1]["Rep"][a];
        if (!temp.containsKey("$")) { //the last observation has been passed
          //identify and move back to last observation with "W", as often missing
          for (int t = 1; t < 6; t++) {
            JsonObject& temp2 = root["SiteRep"]["DV"]["Location"]["Period"][1]["Rep"][a - t];
            if (temp2.containsKey("W") && checking) {
              last = a - t;
              Serial.println(last);
              checking = false;
            }
          }
        }
      }
    }
    JsonObject& latest = root["SiteRep"]["DV"]["Location"]["Period"][1]["Rep"][last];

    //print latest observation
    for (JsonObject::iterator it = latest.begin(); it != latest.end(); ++it) {
      Serial.print(it->key);
      Serial.print(" : ");
      Serial.println(it->value.asString());
    }

    obvTime = latest["$"];
    obvTime = (obvTime / 60) * 100; //convert from mins since midnight
    Serial.print("Observation time: "); Serial.println(obvTime);
    currentTemp = round(latest["T"].as<float>());
    Serial.print("Temp: "); Serial.print(currentTemp); Serial.println(" deg C");
    currentType = latest["W"];
    Serial.print("Weather type: "); Serial.println(weatherTypes[currentType]);
    currentWindSpeed = latest["S"];
    strcpy(currentWindDir, latest["D"]);
    return true;
  } else {
    Serial.println("JSON empty - no data returned");
    return false;
  }
}

//control of OLED display
void updateDisplay(int displayMode) {
  int xOffset = 72; //2nd column alignment
  //  displayMode = 2; //***testing
  switch (displayMode) {
    case 0: // observation
      updateRGB(1); //red
      display.clearDisplay();
      display.setTextSize(2);
      display.setTextColor(WHITE);
      if (!bitmapDisplay(currentType, 0, 0)) {
        //      if (!bitmapDisplay(8, 0, 0)) { //testing
        display.setCursor(0, 0);
        display.setTextWrap(false);
        display.println(weatherTypes[currentType]);
      }
      display.setCursor(0, 37);
      display.print(currentTemp);
      display.print((char)247); //degrees symbol
      display.println("C");
      display.setTextSize(1);
      display.setCursor(0, 56);
      display.print("Observ @ ");
      printTime(obvTime);
      drawWind(currentWindDir, currentWindSpeed, display.width() - 55, 0);
      display.display();
      break;

    case 1: // forecast
      updateRGB(2); //green
      display.clearDisplay();
      display.setTextSize(2);
      display.setTextColor(WHITE);
      //day
      display.setCursor(0, 0);
      if (!bitmapDisplay(dayType, 0, 0)) {
        display.setTextWrap(false);
        display.println(weatherTypes[dayType]);
      }
      display.setCursor(0, 37);
      display.print(dayMaxTemp);
      display.print((char)247); //degrees symbol
      display.println("C");
      display.setTextSize(1);
      display.setCursor(0, 56);
      display.println("Today");
      //night
      display.setTextSize(2);
      if (!bitmapDisplay(nightType, xOffset, 0)) {
        display.setCursor(xOffset, 0);
        display.setTextWrap(false);
        display.println(weatherTypes[nightType]);
      }
      display.setCursor(xOffset, 37);
      display.print(nightMinTemp);
      display.print((char)247); //degrees symbol
      display.println("C");
      display.setTextSize(1);
      display.setCursor(xOffset, 56);
      display.println("Tonight");

      display.drawLine(xOffset -8, 0, xOffset - 8, 63, WHITE);
//      void drawFastVLine(uint16_t x0, uint16_t y0, uint16_t length, uint16_t color);
//void drawFastHLine(uint8_t x0, uint8_t y0, uint8_t length, uint16_t color);
      display.display();
      break;

    case 2: // rain forecast
      updateRGB(3); //green
      display.clearDisplay();
      display.setTextColor(WHITE);
      display.setCursor(0, 0);
      if (rainDate == 0) {
        //        display.setTextWrap(true);
        display.setTextSize(2);
        display.println("No rain");
        display.println("in the");
        display.println("next");
        display.println("5 days");
      } else {
        if (!bitmapDisplay(rainType, 0, 0)) {
          display.setTextWrap(false);
          display.setTextSize(2);
          display.println(weatherTypes[rainType]);
        }
        //        drawGust(xOffset + 10, 0, rainGust);
        drawGust(xOffset + 10, 31, rainGust);
        display.setCursor(0, 37);
        display.setTextSize(2);
        if (rainDate == currentDate()) {
          printTime(rainTime);
          display.setCursor(0, 56);
          display.setTextSize(1);
          display.print("Today");
          //          printDate(rainDate);
        } else {
          printDate(rainDate);
          display.setCursor(0, 56);
          display.setTextSize(1);
          printTime(rainTime);
        }
        //        drawProb(xOffset + 10, 39, rainProb);
        drawProb(xOffset + 10, 0, rainProb);
      }
      //128x64
      display.drawLine(xOffset + 7, 28, 127, 28, WHITE);
      display.drawLine(xOffset + 7, 0, xOffset + 7, 63, WHITE);
      display.display();
      break;
  }
}


long currentDate() {
  if (timeStamp[0] == 'D') {
    char substr[9];
    char monStr[4];
    strncpy(monStr, timeStamp + 14, 3); //months
    monStr[3] = '\0'; //terminate
    int monInt;
    for (int m = 0; m < 12; m++) {
      if (strcmp(months[m], monStr) == 0) monInt = m + 1;
    }
    char p[3]; //c string containing the month number
    itoa(monInt, p, 10); //convert from int to char
    strncpy(substr, timeStamp + 18, 4); //year
    if (monInt < 10) {
      strncpy(substr + 4, "0", 1); //padding
      strncpy(substr + 5, p, 1); //months
    } else strncpy(substr + 4, p, 2); //months
    strncpy(substr + 6, timeStamp + 11, 2); //days
    substr[8] = '\0'; //terminate
    //  Serial.print(substr);
    return atoi(substr); //convert to int
  } else return 0;
}

int currentTime() {
  if (timeStamp[0] == 'D') {
    //why is it not possible to use strncat here? because timeStamp2 is not a cons char *?
    char substr[5];
    strncpy(substr, timeStamp + 23, 2); //hours
    strncpy(substr + 2, timeStamp + 26, 2); //mins
    //strncat(substr, timeStamp + 23, 2); //hours
    //  strncat(substr, timeStamp + 26, 2); //mins
    substr[4] = '\0'; //terminate
    //  Serial.print("*****");
    //  Serial.print(substr);
    return atoi(substr); //convert to int
    //  return (timeStamp.substring(17, 19) + timeStamp.substring(20, 22)).toInt(); //hours mins
  } else return 0;
}

void printTime(int t) {
  //  t = 935;
  //  Serial.println(t);
  char u[5];
  char timeString[6];
  itoa(t, u, 10); //convert from int to char, base 10
  //  Serial.println(u);
  //  Serial.println(t / 100);
  if (t < 1000) {
    strncpy(timeString, "0", 1); //hours
    strncpy(timeString + 1, u, 1); //hours
    strncpy(timeString + 3, u + 1, 2); //mins
  } else {
    strncpy(timeString, u, 2); //hours
    strncpy(timeString + 3, u + 2, 2); //mins
  }
  strncpy(timeString + 2, ":", 1);
  timeString[5] = '\0'; //terminate
  //  Serial.println(timeString);
  display.print(timeString);
}

void printDate(long d) {
  //  t = 935;
  //Serial.println(d);
  char v[9];
  char dateString[6];
  itoa(d, v, 10); //convert from int to char, base 10
  //Serial.println(v);
  strncpy(dateString, v + 6, 2); //day
  strncpy(dateString + 2, "/", 1);
  strncpy(dateString + 3, v + 4, 2); //month
  dateString[5] = '\0'; //terminate
  //Serial.println(dateString);
  display.print(dateString);
}

void drawWind(char *dir, int s, int x, int y) {
  int points;
  int radius = 17;
  int midX = x + radius + 10; //middle point X
  int midY = y + radius + 10; //middle point Y
  for (int a = 0; a < 16; a++) {
    if (strcmp(compass[a], dir) == 0) points = a;
  }
  float angle = (22.5 * points) + 180;
  int triX1 = xCord(midX, radius + 10, rad(angle));
  int triY1 = yCord(midY, radius + 10, rad(angle));
  int triX2 = xCord(midX, radius + 5, rad(angle + 15));
  int triY2 = yCord(midY, radius + 5, rad(angle + 15));
  int triX3 = xCord(midX, radius + 5, rad(angle - 15));
  int triY3 = yCord(midY, radius + 5, rad(angle - 15));
  display.drawCircle(midX, midY, radius, WHITE);
  display.fillTriangle(triX1, triY1, triX2, triY2, triX3, triY3, WHITE);
  //  int testws = 11;
  display.setCursor(x + 16 + ((currentWindSpeed >= 20) ? 1 : (currentWindSpeed < 10) ? 6 : 0), y + 20); //adjust x for single or double digits
  //  display.setCursor(x + 16 + ((testws >= 20) ? 1 : (testws < 10) ? 6 : 0), y + 20); //adjust x for single or double digits
  //  display.setCursor(x + 16 +((currentWindSpeed >= 20) ? 0 : (currentWindSpeed >= 10 ? 1 : 6)), y + 20); //adjust x for single or double digits
  display.setTextSize(2);
  display.println(currentWindSpeed);
  //  display.println(testws);

  //  display.display();
}

void drawProb(int x, int y, int p) {
  //  p = 9;
  display.setCursor(x + 12, y);
  display.setTextSize(1);
  display.print("Prob");
  display.setCursor(x + ((p == 100) ? 0 : (p < 10) ? 15 : 8), y + 10); //adjust x for single or double digits
  display.setTextSize(2);
  display.print(p);
  display.print("%");
  //  display.setCursor(x + 12, y + 17);
  //  display.setTextSize(1);
  //  display.print("Prob");
}


void drawGust(int x, int y, int g) {
  //  g = 9;
  display.setCursor(x + 12, y);
  display.setTextSize(1);
  display.print("Gust");
  display.setCursor(x + ((g >= 100) ? 5 : (g < 10) ? 19 : 12), y + 10); //adjust x for single or double digits
  display.setTextSize(2);
  display.print(g);
  display.setCursor(x + 15, y + 25);
  display.setTextSize(1);
  display.print("mph");

  //        display.setCursor(xOffset + 23, 20);
  //  display.setTextSize(1);
  //  display.print("gust");
}

//convert degrees to radians
float rad(float deg) {
  return (deg * 71) / 4068;
}

//calculate x coordinate
int xCord(float x, float r, float t) {
  return round(x + r * cos(t));
}

//calculate y coordinate
int yCord(float y, float r, float t) {
  return round(y + r * sin(t));
}

//display weather icon
bool bitmapDisplay(int weather, int x, int y) {
  weather = 9;
  switch (weather) {
    case 0:
      display.drawBitmap(x, y, clearNight0, 48, 32, 1);
      return true;
      break;
    case 1:
      display.drawBitmap(x, y, sunnyDay1, 48, 32, 1);
      return true;
      break;
    case 2:
      display.drawBitmap(x, y, partlyCloudyNight2, 48, 32, 1);
      return true;
      break;
    case 3:
      display.drawBitmap(x, y, partlyCloudyDay3, 48, 32, 1);
      return true;
      break;
  }
  //  if (weather == 2 || weather == 3 ) { // partly cloudy (day/night) ***add seperate night version icon with moon in place of sun
  //    display.drawBitmap(x, y, partlyCloudy, 40, 32, 1);
  //    return true;
  //  } else
  if (weather == 7 || weather == 8 ) { // cloudy / overcast
    display.drawBitmap(x, y, cloudy, 56, 32, 1);
    return true;
  } else if (weather == 9 || weather == 10 ) { //lightRainShower ***add seperate night version icon with moon in place of sun
    display.drawBitmap(x, y, lightRainShower, 56, 32, 1);
    return true;
  } else if (weather == 11 || weather == 12 ) { //lightRain ***implement seperate drizzle icon for 11
    display.drawBitmap(x, y, lightRain, 40, 32, 1);
    return true;
  } else if (weather >= 13 && weather <= 15 ) { //heavyrain ***implement seperate heavy rain shower night/day for 13 & 14
    display.drawBitmap(x, y, heavyRain, 40, 32, 1);
    return true;
  } else return false;
}

//RGB led control
void updateRGB (int newColour) {
  int r, g, b;
  switch (newColour) {
    case 0: //off
      r = 0;
      g = 0;
      b = 0;
      break;

    case 1: //red
      r = 500;
      g = 0;
      b = 0;
      break;

    case 2: //green
      r = 0;
      g = 500;
      b = 0;
      break;

    case 3: //blue
      r = 0;
      g = 0;
      b = 500;
      break;

    case 9: //white
      r = 500;
      g = 500;
      b = 500;
      break;
  }
  //invert for common anode
  analogWrite(redPin, 1023 - r);
  analogWrite(greenPin, 1023 - g);
  analogWrite(bluePin, 1023 - b);
  RGB = newColour;
}

void confirm(int c) {
  updateRGB(0);
  delay(10);
  updateRGB(c); //colour
  delay(10);
  updateRGB(RGB); //***back to original colour
}

//void printBoth(char *m) {
//  Serial.println(m);
//  display.println(m);
//}

bool readSwitch() {
  switchReading = digitalRead(switchPin);
  if ((switchReading != lastSwitchReading) && ((millis() - lastDebounceTime) > debounceDelay)) {
    lastSwitchReading = switchReading;
    lastDebounceTime = millis();
    return switchReading;
  } else return false;
}
