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
  - switching on/off of serial logging
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
#include <OneWire.h>
#include <Adafruit_NeoPixel.h>

#include "credentials.h" //credentials file with wifi settings and Met Office API key
#include "bitmaps.h" //bitmaps file

//enable logging
bool logging = true;
//bool logging = false;

// pin definitions
//const int redPin = D4;
//const int greenPin = D4;
//const int bluePin = D4;
const int switchPin = D8; //built in pull-down
OneWire ds(D6);  // DS18B20 on pin 5 (a 4.7K resistor is necessary) ***PIN 4 doesnt work!!
//const int motionPin = A0;
// SCL GPIO5 (D1)
// SDA GPIO4 (D2)
#define OLED_RESET D3  // GPIO0

//const int pixelPin = D6; //NeoPixel pin
#define PIXELPIN D5
#define NUMPIXELS 1 //total NeoPixels

// When we setup the NeoPixel library, we tell it how many pixels, and which pin to use to send signals.
// Note that for older NeoPixel strips you might need to change the third parameter--see the strandtest
// example for more information on possible values.
Adafruit_NeoPixel pixels = Adafruit_NeoPixel(NUMPIXELS, PIXELPIN, NEO_GRB + NEO_KHZ800);


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
long updateDelay = (5 * 60000);

// string variables
const char *weatherTypes[] = {"Clear night", "Sunny day", "Partly cloudy (night)", "Partly cloudy (day)", "Not used", "Mist", "Fog", "Cloudy", "Overcast", "Light rain shower (night)", "Light rain shower (day)", "Drizzle", "Light rain", "Heavy rain shower (night)", "Heavy rain shower (day)", "Heavy rain", "Sleet shower (night)", "Sleet shower (day)", "Sleet", "Hail shower (night)", "Hail shower (day)", "Hail", "Light snow shower (night)", "Light snow shower (day)", "Light snow", "Heavy snow shower (night)", "Heavy snow shower (day)", "Heavy snow", "Thunder shower (night)", "Thunder shower (day)", "Thunder"};
const char *compass[] = {"E", "ESE", "SE", "SSE", "S", "SSW", "SW", "WSW", "W", "WNW", "NW", "NNW", "N", "NNE", "NE", "ENE"};
const char *months[] = { "Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec" };

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

//sunriseset 2
int sunriseTime;
int sunsetTime;
char sunriseDateTime[26];
char sunsetDateTime[26];

//sunriseset 2
int sunriseTime2;
int sunsetTime2;
char sunriseDateTime2[26];
char sunsetDateTime2[26];

//  sunriseTime = isoToTime(sunriseDateTime);
//                sunsetTime = isoToTime(sunsetDateTime);
char nextMoon [14]; //check length
char nextMoonDate[21]; //check length

// millis() of last update attempt
unsigned long lastUpdateAttempt;

// modes
int currentMode = 0;
unsigned long displayTimeout = 20000;
bool displayOff = false;

// switch
bool switchReading;
bool lastSwitchReading;
unsigned long lastDebounceTime = 0;

// leds
int RGB;
//int targetRGB;

//indoor temperature
float indoorTemp;

void setup() {
  //inputs
  pinMode (switchPin, INPUT);

  //outputs
  //  pinMode (redPin, OUTPUT);
  //  pinMode (greenPin, OUTPUT);
  //  pinMode (bluePin, OUTPUT);
  pinMode(LED_BUILTIN, OUTPUT);

  //turn leds off
  analogWrite(LED_BUILTIN, 1023);
  updateRGB(0);

  if (logging) Serial.begin(115200);

  // by default, we'll generate the high voltage from the 3.3v line internally! (neat!)
  display.begin(SSD1306_SWITCHCAPVCC, 0x3C);  // initialize with the I2C addr 0x3C
  display.setRotation(2);
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

  pixels.begin(); // This initializes the NeoPixel library.

  //*****testing JSON read
  Serial.println("reading values from JSON");
  //  Serial.print("forecast ID: "); Serial.println(forecast_id); //forecast site ID
  //  Serial.print("observation ID: "); Serial.println(observation_id); //observation site ID
  //  Serial.print("API key: "); Serial.println(api_key); //api key

  if (updateData()) updateDisplay(0); //update
  lastDebounceTime = millis(); //prevent immediate screen timeout

}


void loop() {
  //  long oldARead = aRead;
  if (readSwitch() == HIGH) {
    if (displayOff) currentMode = 0;
    else {
      currentMode++;
      //      if (currentMode > (totalModes - 1)) currentMode = 0;
    }
    updateDisplay(currentMode);
  }

  //   timeout the display
  if (millis() - lastDebounceTime > displayTimeout) {
    display.clearDisplay(); // Clear the buffer.
    display.display(); //blank the display
    updateRGB(0);
    displayOff = true;
  }

  //  if (currentMode == 5 && aRead != oldARead) { //keep updating in IR test mode
  //    updateDisplay(currentMode);
  //    delay(300);
  //  }

  //update data
  if (millis() - lastUpdateAttempt > updateDelay) {
    if (updateData() && !displayOff) updateDisplay(currentMode);
    lastUpdateAttempt = millis();
  }
}



//char* updateData(int apiId) {
bool updateData() {
  const int totalReqs = 3; //6; //number of requests to make - increase when adding new apis :) plus a few bits below...
  for (int reqid = 0; reqid < totalReqs; reqid++) {

    //        Serial.println("****REQ*****");
    //        int reqid = 2; //testing
    //        Serial.println(reqid);




    //http GET request components
    const char getReq[] = " HTTP/1.1\r\n"
                          "User-Agent: ESP8266/0.1\r\n"
                          "Accept: */*\r\n"
                          "Host: ";
    const char getReq2[] = "\r\n"
                           "Connection: close\r\n"
                           "\r\n";
    // Met Office
    const char metOffice[] = "datapoint.metoffice.gov.uk";
    const char getFore[] = "GET /public/data/val/wxfcs/all/json/";
    const char getObv[] = "GET /public/data/val/wxobs/all/json/";
    const char threeHourly[] = "?res=3hourly&key=";
    const char daily[] = "?res=daily&key=";
    const char hourly[] = "?res=hourly&key=";

    // Sunrise Sunset
    const char sunRiseSet[] = "api.sunrise-sunset.org";
    const char getSun[] = "GET /json?lat=";
    const char sunLng[] = "&lng=";
    const char sunFormat[] = "&date=today&formatted=0";

    // Sunrise Sunset 2
    const char usno[] = "api.usno.navy.mil";
    const char getUsno[] = "GET /rstt/oneday?date=12/06/2016&coords=";
    //    const char getUsno[] = "GET /rstt/oneday?date=today&coords=";
    const char usnoTimezone[] = "&tz=0";

    // Wunderground
    const char WUnderground[] = "api.wunderground.com";
    const char WUNDERGROUND_REQ[] =
      "GET /api/" WU_API_KEY "/conditions/q/" WU_LOCATION ".json HTTP/1.1\r\n"
      "User-Agent: ESP8266/0.1\r\n"
      "Accept: */*\r\n"
      "Host: " WUNDERGROUND "\r\n"
      "Connection: close\r\n"
      "\r\n";

    static char httpRequest[200] = "";
    //    Serial.print(httpRequest);
    static char server[27] = "";
    switch (reqid) {
      case 0:
        //build daily forecast request
        strcpy (server, metOffice);
        strcpy (httpRequest, getFore);
        strcat (httpRequest, forecast_id);
        strcat (httpRequest, daily);
        strcat (httpRequest, api_key);
        strcat (httpRequest, getReq);
        strcat (httpRequest, metOffice);
        strcat (httpRequest, getReq2);
        break;
      case 1:
        //build 3hourly forecast request
        strcpy (server, metOffice);
        strcpy (httpRequest, getFore);
        strcat (httpRequest, forecast_id);
        strcat (httpRequest, threeHourly);
        strcat (httpRequest, api_key);
        strcat (httpRequest, getReq);
        strcat (httpRequest, metOffice);
        strcat (httpRequest, getReq2);
        break;
      case 2:
        //build observation request
        strcpy (server, metOffice);
        strcpy (httpRequest, getObv);
        strcat (httpRequest, observation_id);
        strcat (httpRequest, hourly);
        strcat (httpRequest, api_key);
        strcat (httpRequest, getReq);
        strcat (httpRequest, metOffice);
        strcat (httpRequest, getReq2);
        break;
      case 3:
        //build sun request
        strcpy (server, sunRiseSet);
        strcpy (httpRequest, getSun);
        strcat (httpRequest, latitude);
        strcat (httpRequest, sunLng);
        strcat (httpRequest, longitude);
        strcat (httpRequest, sunFormat);
        strcat (httpRequest, getReq);
        strcat (httpRequest, sunRiseSet);
        strcat (httpRequest, getReq2);
        break;
      case 4:
        //build usno request
        strcpy (server, usno);
        strcpy (httpRequest, getUsno);//http://api.usno.navy.mil/rstt/oneday?date=today&coords=
        strcat (httpRequest, latitude);
        strcat (httpRequest, ",");
        strcat (httpRequest, longitude);
        strcat (httpRequest, usnoTimezone);//&tz=0
        strcat (httpRequest, "&ID=");
        strcat (httpRequest, usnoId);
        strcat (httpRequest, getReq);
        strcat (httpRequest, usno);
        strcat (httpRequest, getReq2);
        break;
      case 5:
        //build wunderground request
        strcpy (server, WUnderground);
        strcpy (httpRequest, WUNDERGROUND_REQ);
        break;
    }

    //    Serial.println("****REQ TEST***");
    //    Serial.println(httpRequest);

    //trying to retrieve list of sites - too much data...
    //  String FORECAST_SITE_REQ =
    //    "GET /public/data/val/wxfcs/all/json/sitelist?key=" + String(api_key) + " HTTP/1.1\r\n"
    //    "User-Agent: ESP8266/0.1\r\n"
    //    "Accept: */*\r\n"
    //    "Host: " METOFFICE "\r\n"
    //    "Connection: close\r\n"
    //    "\r\n";


    //    return

    //  static char httpRequest[200] = "";
    //  static char server[27] = "";
    //
    //  //build the get request and return server
    ////  getRequest(apiId, req, server);


    //  static char respBuf[25096]; // response buffer for the JSON
    static char respBuf[5096]; // response buffer for the JSON
    //  static char respBuf[4096]; // response buffer for the JSON

    Serial.println();
    Serial.print(F("Connecting to "));
    //    Serial.println((reqid == 3) ? sunRiseSet : (reqid == 4) ? usno : (reqid == 5) ? WUnderground : metOffice);
    Serial.println(server);
    analogWrite(LED_BUILTIN, 1000); //turn on built in LED dim

    WiFiClient httpclient; // Use WiFiClient class to create TCP connections
    const int httpPort = 80; // Open socket to server port 80
    //    if (!httpclient.connect((reqid == 3) ? sunRiseSet : (reqid == 4) ? usno : (reqid == 5) ? WUnderground : metOffice, httpPort)) {
    if (!httpclient.connect(server, httpPort)) {
      Serial.println(F("connection failed"));
      return false;
    }
    analogWrite(LED_BUILTIN, 1023); //turn off built in LED

    // This will send the http request to the server
    Serial.print(httpRequest);
    httpclient.print(httpRequest);
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
        //        int count = 0;
        //        while (count < 50) { // give up after 50 attempts - debug and reinstate this
        bytesIn = httpclient.read((uint8_t *)&respBuf[respLen], sizeof(respBuf) - respLen);
        Serial.print(".");
        //          count += 1 ;
        //        }
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
      return false;
    }
    respBuf[respLen++] = '\0'; // Terminate the C string
    Serial.println();
    Serial.print(F("respLen "));
    Serial.println(respLen);
    //Serial.println(respBuf);

    //  StaticJsonBuffer<3*1024> jsonBuffer;
    DynamicJsonBuffer jsonBuffer; //not recommended but seems to work

    // Skip characters until first '{' found
    // Ignore chunked length, if present
    char *jsonstart = strchr(respBuf, '{');
    Serial.print(F("jsonstart :")); Serial.println(jsonstart);
    if (jsonstart == NULL) {
      Serial.println(F("JSON data missing"));
      return false;
    }

    // Parse JSON
    JsonObject& root = jsonBuffer.parseObject(jsonstart);
    if (!root.success()) {
      Serial.println(F("jsonBuffer.parseObject() failed"));
      return false;
    }
    Serial.println("parsed successfully");
    Serial.println();

    //  return *jsonstart;



    // Day forecast
    if (reqid == 0) {
      Serial.println("Forecast");
      // Extract weather info from parsed JSON
      JsonObject& DV = root["SiteRep"]["DV"];
      JsonObject& location = root["SiteRep"]["DV"]["Location"];
      //if night- retrieve the next morning
      JsonObject& todayDay = root["SiteRep"]["DV"]["Location"]["Period"][0]["Rep"][0];
      JsonObject& todayNight = root["SiteRep"]["DV"]["Location"]["Period"][0]["Rep"][1];
      Serial.print("Current time: "); Serial.println(currentTime());
      Serial.print("Current date: "); Serial.println(currentDate());
      if (location.containsKey("name")) {
        Serial.print("Location: "); Serial.println(location["name"].asString());
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
        //        return true;
      } else {
        Serial.println("JSON empty - no data returned");
        return false;
      }
    }

    // 3hourly forecast
    if (reqid == 1) {
      Serial.println("Rain forecast");
      // Extract weather info from parsed JSON
      JsonObject& DV = root["SiteRep"]["DV"];
      JsonObject& location = root["SiteRep"]["DV"]["Location"];
      JsonObject& period = root["SiteRep"]["DV"]["Location"]["Period"];
      Serial.print("Current time: "); Serial.println(currentTime());
      Serial.print("Current date: "); Serial.println(currentDate());
      if (location.containsKey("name")) {
        Serial.print("Location: "); Serial.println(location["name"].asString());
        bool checking = true;
        const char *dateTemp;
        for (int d = 0; d < 5; d++) {
          for (int a = 0; a < 8; a++) {
            if (checking) {
              JsonObject& temp = root["SiteRep"]["DV"]["Location"]["Period"][d]["Rep"][a];
              if (temp["W"] >= 9) { //if weather type is 9: "Light rain shower" or above
                //          if (temp["W"] >= 3) { //testing rain
                rainTime = (temp["$"].as<int>() / 60) * 100; //time of rain
                rainType = temp["W"];
                rainProb = temp["Pp"];
                rainGust = temp["G"];
                dateTemp = root["SiteRep"]["DV"]["Location"]["Period"][d]["value"];
                char substr[9];
                strncpy(substr, dateTemp, 4);
                strncpy(substr + 4, dateTemp + 5, 2);
                strncpy(substr + 6, dateTemp + 8, 2);
                //            strncat(substr, dateTemp, 4);
                //            strncat(substr, dateTemp + 5, 2);
                //            strncat(substr, dateTemp + 8, 2);
                substr[8] = '\0'; //terminate
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
      } else {
        Serial.println("JSON empty - no data returned");
        return false;
      }
    }

    // Observation
    if (reqid == 2) {
      Serial.println("Observation");
      // Extract weather info from parsed JSON
      JsonObject& DV = root["SiteRep"]["DV"];
      JsonObject& location = root["SiteRep"]["DV"]["Location"];

      //  Serial.println(timeStamp); //current date and time
      Serial.print("Current time: "); Serial.println(currentTime());
      Serial.print("Current date: "); Serial.println(currentDate());
      if (location.containsKey("name")) {
        Serial.print("Location: "); Serial.println(location["name"].asString());
        int last; //last observation with weather type
        bool checking = true;
        for (int a = 0; a < 26; a++) {
          if (checking) {
            JsonObject& temp = root["SiteRep"]["DV"]["Location"]["Period"][1]["Rep"][a];
            //            Serial.println(temp); //***additional logging
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
        //        return true;
      } else {
        Serial.println("JSON empty - no data returned");
        return false;
      }
    }

    // Sunrise Sunset
    if (reqid == 3) {
      Serial.println("Sunrise Sunset");
      Serial.print("Current time: "); Serial.println(currentTime());
      Serial.print("Current date: "); Serial.println(currentDate());
      // Extract info from parsed JSON
      JsonObject& results = root["results"];
      Serial.print("Status: "); Serial.println(root["status"].asString());
      if (strcmp(root["status"], "OK") == 0) {
        strcpy(sunriseDateTime, results["sunrise"]);
        strcpy(sunsetDateTime, results["sunset"]);
        sunriseTime = isoToTime(sunriseDateTime);
        sunsetTime = isoToTime(sunsetDateTime);
        Serial.print("Sunrise datetime:"); Serial.println(sunriseDateTime);
        Serial.print("Sunrise time:"); Serial.println(sunriseTime);
        Serial.print("Sunset datetime:"); Serial.println(sunsetDateTime);
        Serial.print("Sunset time:"); Serial.println(sunsetTime);

        //print results
        for (JsonObject::iterator it = results.begin(); it != results.end(); ++it) {
          Serial.print(it->key);
          Serial.print(" : ");
          Serial.println(it->value.asString());
        }
        //        obvTime = latest["$"];
        //        obvTime = (obvTime / 60) * 100; //convert from mins since midnight
        //        Serial.print("Observation time: "); Serial.println(obvTime);
        //        currentTemp = round(latest["T"].as<float>());
        //        Serial.print("Temp: "); Serial.print(currentTemp); Serial.println(" deg C");
        //        currentType = latest["W"];
        //        Serial.print("Weather type: "); Serial.println(weatherTypes[currentType]);
        //        currentWindSpeed = latest["S"];
        //        strcpy(currentWindDir, latest["D"]);
      } else {
        Serial.println("Failed status check");
        return false;
      }
    }
    // Sunrise Sunset 2
    if (reqid == 4) {
      Serial.println("Sunrise Sunset 2: USNO");
      Serial.print("Current time: "); Serial.println(currentTime());
      Serial.print("Current date: "); Serial.println(currentDate());
      // Extract info from parsed JSON
      JsonObject& sundata = root["sundata"];
      Serial.print("Status: "); //Serial.println(root["error"].asString());
      if (strcmp(root["error"], "false") != 0) {
        Serial.println("Failed status check");
        return false;
      } else {
        Serial.println("OK");


        for (int su = 0; su < 5; su++) {
          if (strcmp(root["sundata"][su]["phen"], "R") == 0) {
            Serial.print("Sunrise: "); Serial.println(root["sundata"][su]["time"].asString());
            strcpy(sunriseDateTime2, root["sundata"][su]["time"]);
          }
          if (strcmp(root["sundata"][su]["phen"], "S") == 0) {
            Serial.print("Sunset: "); Serial.println(root["sundata"][su]["time"].asString());
            strcpy(sunsetDateTime2, root["sundata"][su]["time"]);
          }
        }

        //        strcpy(sunsetDateTime2, results["sunset"]);
        //        sunriseTime = isoToTime(sunriseDateTime);
        //        sunsetTime = isoToTime(sunsetDateTime);
        //        Serial.print("Sunrise datetime:"); Serial.println(sunriseDateTime);
        //        Serial.print("Sunrise time:"); Serial.println(sunriseTime);
        //        Serial.print("Sunset datetime:"); Serial.println(sunsetDateTime);
        //        Serial.print("Sunset time:"); Serial.println(sunsetTime);

        //        sunriseTime2 = isoToTime(sunriseDateTime);
        //        sunsetTime2 = isoToTime(sunsetDateTime);
        strcpy(nextMoon, root["closestphase"]["phase"]);
        strcpy(nextMoonDate, root["closestphase"]["date"]);

        int moonDateInt;
        Serial.println(root["closestphase"]["phase"].asString());
        Serial.println(root["closestphase"]["date"].asString());
        moonDateInt = moonDate(nextMoonDate);
        Serial.println(moonDateInt);

        //        display.println(nextMoon);
        //        display.println(nextMoonDate);

        //print results
        for (JsonObject::iterator it = root.begin(); it != root.end(); ++it) {
          Serial.print(it->key);
          Serial.print(" : ");
          Serial.println(it->value.asString());
        }
        //        Serial.println();
        //        Serial.println("Sundata");

        //        for (int su = 0; su < 5; su++) {
        //          if (strcmp(root["sundata"][su]["phen"], "R") == 0) {
        //            Serial.print("Sunrise: "); Serial.println(root["sundata"][su]["time"].asString());
        //          }
        //          if (strcmp(root["sundata"][su]["phen"], "S") == 0) {
        //            Serial.print("Sunset: "); Serial.println(root["sundata"][su]["time"].asString());
        //          }
        //        }
        //        Serial.println(root["closestphase"]["phase"].asString());
        //        Serial.println(root["closestphase"]["date"].asString());
        //        for (JsonObject::iterator it = sundata.begin(); it != sundata.end(); ++it) {
        //          Serial.print(it->key);
        //          Serial.print(" : ");
        //          Serial.println(it->value.asString());
        //        }
        //        obvTime = latest["$"];
        //        obvTime = (obvTime / 60) * 100; //convert from mins since midnight
        //        Serial.print("Observation time: "); Serial.println(obvTime);
        //        currentTemp = round(latest["T"].as<float>());
        //        Serial.print("Temp: "); Serial.print(currentTemp); Serial.println(" deg C");
        //        currentType = latest["W"];
        //        Serial.print("Weather type: "); Serial.println(weatherTypes[currentType]);
        //        currentWindSpeed = latest["S"];
        //        strcpy(currentWindDir, latest["D"]);

      }
    }
  }
  indoorTemp = readTemp(); //read internal temperature

  confirm(9); //flash led white
  return true; //updated successfully **amend this so it all doesn't fail if 1 data item isn't returned?
}

//control of OLED display
void updateDisplay(int displayMode) {
  int totalModes = 6;
  if (displayMode > (totalModes - 1)) {
    currentMode = 0;
    displayMode = 0;
  }
  int xOffset = 72; //2nd column alignment
  //  displayMode = 2; //***testing
  display.clearDisplay();
  switch (displayMode) {
    // Observation
    case 0:
      updateRGB(1); //red
      //      display.clearDisplay();
      display.setTextSize(2);
      display.setTextColor(WHITE);
      if (!bitmapDisplay(0, 0, currentType)) {
        //      if (!bitmapDisplay(0, 0, 8)) { //testing
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
      drawWind(display.width() - 55, 0, currentWindDir, currentWindSpeed);
      //      display.display();
      break;

    // Forecast
    case 1:
      updateRGB(2); //green
      //      display.clearDisplay();
      display.setTextSize(2);
      display.setTextColor(WHITE);
      //day
      display.setCursor(0, 0);
      if (!bitmapDisplay(0, 0, dayType)) {
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
      if (!bitmapDisplay(xOffset, 0, nightType)) {
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

      display.drawFastVLine(xOffset - 8, 0 , 64, WHITE);
      //      display.display();
      break;

    // Rain forecast
    case 2:
      updateRGB(3); //blue
      //      display.clearDisplay();
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
        if (!bitmapDisplay(0, 0, rainType)) {
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
        display.drawFastHLine(xOffset + 7, 28, 121 - xOffset, WHITE);
        display.drawFastVLine(xOffset + 7, 0 , 64, WHITE);
      }
      //      display.display();
      break;

    // Sunrise Sunset
    case 3:
      updateRGB(4); //yellow
      //      display.setTextColor(WHITE);
      display.fillRect(0, 0, 84, 32, WHITE);
      display.setCursor(0, 0);
      display.setTextColor(BLACK, WHITE);
      display.setTextSize(2);
      display.println("Sunrise");
      printTime(sunriseTime);
      display.println();
      //      display.println(sunriseTime);
      //      display.setTextColor(BLACK, WHITE);
      display.setTextColor(WHITE, BLACK);
      display.println("Sunset");
      printTime(sunsetTime);
      display.drawFastVLine(84, 0 , 64, WHITE);
      //      display.display();
      //      if (currentTime() < sunsetTime) Serial.print("***DAY***");
      //      else Serial.print("***NIGHT***");
      break;

    // Sunrise Sunset 2
    case 4:
      updateRGB(5); //orange
      display.setTextColor(WHITE);
      //      display.fillRect(0, 0, 84, 32, WHITE);
      display.setCursor(0, 0);
      //      display.setTextColor(BLACK, WHITE);
      display.setTextSize(2);

      printTime(sunriseTime2);
      display.setCursor(xOffset, 0);
      printTime(sunsetTime2);
      display.setTextSize(1);
      display.setCursor(0, 15);
      display.println("Sunrise");
      display.setCursor(xOffset, 15);
      display.println("Sunset");
      display.setCursor(0, 30);
      display.println(nextMoon);
      display.println(nextMoonDate);
      //      printDate(nextMoonDate);

      //      display.println();
      //      display.println(sunriseTime);
      //      display.drawFastVLine(84, 0 , 64, WHITE);
      //      if (currentTime() < sunsetTime) Serial.print("***DAY***");
      //      else Serial.print("***NIGHT***");
      break;

    // Internal Temp
    case 5:
      updateRGB(9); //white
      display.setTextColor(WHITE);
      display.setCursor(0, 0);
      display.setTextSize(2);
      display.println("Indoor");
      display.println();
      display.print(indoorTemp);
      display.print((char)247); //degrees symbol
      display.println("C");
      break;
  }
  display.display();
  displayOff = false;
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
      strcpy(substr + 4, "0"); //padding
      strncpy(substr + 5, p, 1); //months
    } else strncpy(substr + 4, p, 2); //months
    strncpy(substr + 6, timeStamp + 11, 2); //days
    substr[8] = '\0'; //terminate
    //  Serial.print(substr);
    return atoi(substr); //convert to int
  } else return 0;
}

long moonDate(char *mDate) {
  //    if (timeStamp[0] == 'D') {
  char substr[9];
  char monStr[4];
  strncpy(monStr, mDate, 3); //months
  monStr[3] = '\0'; //terminate
  int monInt;
  for (int m = 0; m < 12; m++) {
    if (strcmp(months[m], monStr) == 0) monInt = m + 1;
  }
  char p[3]; //c string containing the month number
  itoa(monInt, p, 10); //convert from int to char
  //    for (int se = 0 ; se <
  Serial.println("moon months");
  Serial.println(monInt);
  Serial.println(strlen(mDate));
  strncpy(substr, mDate + strlen(mDate) - 4, 4); //year
  if (monInt < 10) {
    strcpy(substr + 4, "0"); //padding
    strncpy(substr + 5, p, 1); //months
  } else strncpy(substr + 4, p, 2); //months
  if (strcmp(mDate + strlen(mDate) - 8, " ") == 0) {
    strcpy(substr + 6, "0"); //padding
    strncpy(substr + 7 , mDate + strlen(mDate) - 7, 1); //days
  } else strncpy(substr + 6, mDate + strlen(mDate) - 8, 2); //days
  substr[8] = '\0'; //terminate
  Serial.println(substr);
  return atoi(substr); //convert to int
  //  } else return 0;
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
  } else return 0;
}

//iso8601
int isoToTime(char *iso) {
  if (iso[0] == '2') { //should be ok as a check for the next ~983 years
    //  Serial.println(iso[0]);
    char timeInt[5];
    strncpy(timeInt, iso + 11, 2); //hours
    strncpy(timeInt + 2, iso + 14, 2); //mins
    timeInt[4] = '\0'; //terminate
    //  Serial.println(timeInt);
    return atoi(timeInt);
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
  strcpy(timeString + 2, ":");
  if (t < 1000) {
    strcpy(timeString, "0"); //hours
    strncpy(timeString + 1, u, 1); //hours
    strncpy(timeString + 3, u + 1, 2); //mins
  } else {
    strncpy(timeString, u, 2); //hours
    strncpy(timeString + 3, u + 2, 2); //mins
  }
  //  strcpy(timeString + 2, ":");
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
  strcpy(dateString + 2, "/");
  strncpy(dateString + 3, v + 4, 2); //month
  dateString[5] = '\0'; //terminate
  //Serial.println(dateString);
  display.print(dateString);
}

//convert degrees to radians
float rad(float deg) {
  return (deg * 71) / 4068;
}

//calculate x coordinate for wind indicator using the parametric equation
int xCord(float x, float r, float t) {
  return round(x + r * cos(rad(t)));
}

//calculate y coordinate for wind indicator
int yCord(float y, float r, float t) {
  return round(y + r * sin(rad(t)));
}

// draw wind indicator
void drawWind(int x, int y, char *dir, int s) { //x , y origin, direction, speed,
  int points;
  int radius = 17;
  int midX = x + radius + 10; //middle point X
  int midY = y + radius + 10; //middle point Y
  for (int a = 0; a < 16; a++) {
    if (strcmp(compass[a], dir) == 0) points = a;
  }
  //  Serial.println(points);
  //  points = 8;//testing
  float angle = (22.5 * points) + 180;
  int triX1 = xCord(midX, radius + 10, angle);
  int triY1 = yCord(midY, radius + 10, angle);
  int triX2 = xCord(midX, radius + 5, angle + 15);
  int triY2 = yCord(midY, radius + 5, angle + 15);
  int triX3 = xCord(midX, radius + 5, angle - 15);
  int triY3 = yCord(midY, radius + 5, angle - 15);
  display.drawCircle(midX, midY, radius, WHITE);
  display.fillTriangle(triX1, triY1, triX2, triY2, triX3, triY3, WHITE);
  //  int testws = 11;
  display.setCursor(x + 16 + ((s >= 20) ? 1 : (s < 10) ? 6 : 0), y + 20); //adjust x for single or double digits
  //  display.setCursor(x + 16 + ((testws >= 20) ? 1 : (testws < 10) ? 6 : 0), y + 20); //adjust x for single or double digits
  //  display.setCursor(x + 16 +((currentWindSpeed >= 20) ? 0 : (currentWindSpeed >= 10 ? 1 : 6)), y + 20); //adjust x for single or double digits
  display.setTextSize(2);
  //  display.print(currentWindSpeed);
  display.print(s);
  //  display.println(testws);
}

// draw rain probability indicator
void drawProb(int x, int y, int p) {
  //  p = 9;
  display.setCursor(x + 12, y);
  display.setTextSize(1);
  display.print("Prob");
  display.setCursor(x + ((p == 100) ? 0 : (p < 10) ? 15 : 8), y + 10); //adjust x for single or double digits
  display.setTextSize(2);
  display.print(p);
  display.print("%");
}

// draw gust indicator
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
}

//display weather icon
bool bitmapDisplay(int x, int y, int weather) {
  //  weather = 9; //testing
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
  if (weather == 7 || weather == 8 ) { // cloudy, overcast
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

    case 4: //yellow
      r = 300;
      g = 400;
      b = 0;
      break;

    case 5: //orange
      r = 400;
      g = 300;
      b = 0;
      break;

    case 9: //white
      r = 500;
      g = 500;
      b = 500;
      break;
  }

  for (int i = 0; i < NUMPIXELS; i++) { // For a set of NeoPixels the first NeoPixel is 0, second is 1, all the way up to the count of pixels minus one.
    pixels.setPixelColor(i, pixels.Color(r, g, b)); // pixels.Color takes RGB values, from 0,0,0 up to 255,255,255
    pixels.show(); // This sends the updated pixel color to the hardware.
  }
  //invert for common anode
  //  analogWrite(redPin, 1023 - r);
  //  analogWrite(greenPin, 1023 - g);
  //  analogWrite(bluePin, 1023 - b);
  RGB = newColour;
}

// flash led once as a confirmation
void confirm(int c) { //c = colour
  int dtime = 30;
  int oldRGB = RGB; //save current colour
  updateRGB(0); //off
  delay(dtime);
  updateRGB(c); //colour
  delay(dtime);
  updateRGB(0); //off
  delay(dtime);
  RGB = oldRGB; //***back to original colour
  updateRGB(RGB);
}

// read and debounce switch **debounce required with touch sensor? simplify?
bool readSwitch() {
  int debounceDelay = 50;
  switchReading = digitalRead(switchPin);
  if ((switchReading != lastSwitchReading) && ((millis() - lastDebounceTime) > debounceDelay)) {
    lastSwitchReading = switchReading;
    lastDebounceTime = millis();
    return switchReading;
  } else return false;
}

float readTemp() {
  byte data[12];
  byte addr[8];
  float celsius;

  if ( !ds.search(addr)) {
    ds.reset_search();
    delay(250);
    return (0);
  }
  ds.reset();
  ds.select(addr);
  ds.write(0x44);        // start conversion, use ds.write(0x44,1) with parasite power on at the end
  delay(1000);     // maybe 750ms is enough, maybe not
  // we might do a ds.depower() here, but the reset will take care of it.
  ds.reset();
  ds.select(addr);
  ds.write(0xBE);         // Read Scratchpad

  for (byte i = 0; i < 9; i++) data[i] = ds.read();           // we need 9 bytes

  // Convert the data to actual temperature
  // because the result is a 16 bit signed integer, it should
  // be stored to an "int16_t" type, which is always 16 bits
  // even when compiled on a 32 bit processor.
  int16_t raw = (data[1] << 8) | data[0];
  celsius = (float)raw / 16.0;
  if (logging) {
    Serial.println();
    Serial.print("Indoor temperature: ");
    Serial.print(celsius);
    Serial.println(" Celsius");
  }
  return celsius;
}


//// read and debounce IR distance sensor
//bool readMotion() {
//  //  long aRead;
//  int span = 5;
//  int motionDebounceDelay = 200;
//  for (int i = 0; i < span; i++) {
//    aRead = aRead + analogRead(motionPin);
//  }
//  aRead = aRead / span;
//  motionReading = (aRead > 700) ? true : false;
//  if ((motionReading != lastMotionReading) && ((millis() - lastMotionDebounceTime) > motionDebounceDelay)) {
//    lastMotionReading = motionReading;
//    lastMotionDebounceTime = millis();
//    //        Serial.println(aRead);
//    return motionReading;
//  } else return false;
//}
