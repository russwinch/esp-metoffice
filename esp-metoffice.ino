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

  This example is for a 64x48 size display using I2C to communicate
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
  - ensure blank data is not returned and use previous data if it is (look for a location?)
  - identify date and time of next rain forecast - add 3hourly forecast?
  - produce more icons - find a set and batch all when size determined
  - implement reading from ldr to adjust led brightness
  - implement timeout to turn the oled off after a delay
  - add mode to show last data dates and times
  - animate icons
  - add more logging to oled screen and implement logging bool
  - determine current date and time and compare to rain forecast to filter old data
  - led feedback on updates (switchable)

  - ability to determine / set the current location (by IP ? )
  - implement weather warnings  
  - postcode to coordinate search with postcodes.io
  - look up nearest sites using siteslist and pythag
  - tidy up the use of strings for the forecast requests

*/
#include <ESP8266WiFi.h>
#include <ArduinoJson.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

// checking the right version of the OLED library is included, should support 64x48
#if (SSD1306_LCDHEIGHT != 48)
#error("Height incorrect, please fix Adafruit_SSD1306.h!");
#endif

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

// Met office weather types
const char *weatherTypes[] = {"Clear night", "Sunny day", "Partly cloudy (night)", "Partly cloudy (day)", "Not used", "Mist", "Fog", "Cloudy", "Overcast", "Light rain shower (night)", "Light rain shower (day)", "Drizzle", "Light rain", "Heavy rain shower (night)", "Heavy rain shower (day)", "Heavy rain", "Sleet shower (night)", "Sleet shower (day)", "Sleet", "Hail shower (night)", "Hail shower (day)", "Hail", "Light snow shower (night)", "Light snow shower (day)", "Light snow", "Heavy snow shower (night)", "Heavy snow shower (day)", "Heavy snow", "Thunder shower (night)", "Thunder shower (day)", "Thunder"};

// Met Office settings
#define METOFFICE "datapoint.metoffice.gov.uk"

// current date & time from http request
String timeStamp;

// observation values
int obvTime; //time of observation
int currentTemp; //current temperature deg C
int currentType; //current weather type

// forecast values
int dayMaxTemp;
int nightMinTemp;
int dayType;
int nightType;
int dayPProb;
int nightPProb;

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
  Serial.println("values from JSON");
  Serial.print("forecast ID: "); Serial.println(forecast_id); //forecast site ID
  Serial.print("observation ID: "); Serial.println(observation_id); //observation site ID
  Serial.print("API key: "); Serial.println(api_key); //api key

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
  String OBSERVATION_REQ =
    "GET /public/data/val/wxobs/all/json/" + String(observation_id) + "?res=hourly&key=" + String(api_key) + " HTTP/1.1\r\n"
    "User-Agent: ESP8266/0.1\r\n"
    "Accept: */*\r\n"
    "Host: " METOFFICE "\r\n"
    "Connection: close\r\n"
    "\r\n";

  String FORECAST_REQ =
    "GET /public/data/val/wxfcs/all/json/" + String(forecast_id) + "?res=daily&key=" + String(api_key) + " HTTP/1.1\r\n"
    "User-Agent: ESP8266/0.1\r\n"
    "Accept: */*\r\n"
    "Host: " METOFFICE "\r\n"
    "Connection: close\r\n"
    "\r\n";

  String HOUR_FORECAST_REQ =
    "GET /public/data/val/wxfcs/all/json/" + String(forecast_id) + "?res=3hourly&key=" + String(api_key) + " HTTP/1.1\r\n"
    "User-Agent: ESP8266/0.1\r\n"
    "Accept: */*\r\n"
    "Host: " METOFFICE "\r\n"
    "Connection: close\r\n"
    "\r\n";

  String FORECAST_SITE_REQ =
    "GET /public/data/val/wxfcs/all/json/sitelist?key=" + String(api_key) + " HTTP/1.1\r\n"
    "User-Agent: ESP8266/0.1\r\n"
    "Accept: */*\r\n"
    "Host: " METOFFICE "\r\n"
    "Connection: close\r\n"
    "\r\n";

  //    char* response = httpReq(FORECAST_SITE_REQ.c_str());
  char* response = httpReq(FORECAST_REQ.c_str());
  if (forecast(response)) {
    response = httpReq(OBSERVATION_REQ.c_str());
    if (observation(response)) {
      response = httpReq(HOUR_FORECAST_REQ.c_str());
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
      if (aLine.substring(0, 4) == "Date") timeStamp = aLine.substring(6); //looking for "Date" at the start of the line
      if (aLine.length() <= 1) { // Blank line denotes end of headers
        skip_headers = false;
      }
    } else {
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
    //    delay(DELAY_ERROR);
    //    return; ********************************** FIX THIS
  }
  respBuf[respLen++] = '\0'; // Terminate the C string
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

  // Extract weather info from parsed JSON
  JsonObject& DV = root["SiteRep"]["DV"];
  JsonObject& location = root["SiteRep"]["DV"]["Location"];
  JsonObject& todayDay = root["SiteRep"]["DV"]["Location"]["Period"][0]["Rep"][0];
  JsonObject& todayNight = root["SiteRep"]["DV"]["Location"]["Period"][0]["Rep"][1];

  Serial.println("Forecast");
  Serial.println(timeStamp); //current date and time
  String siteName = location["name"];
  if (siteName.length() != 0) {
    Serial.print("Location: "); Serial.println(siteName);
    String dataTimeStamp = DV["dataDate"];
    //  Serial.print("Data timestamp: "); Serial.println(dataTimeStamp);
    Serial.print("Data date: "); Serial.println(dataTimeStamp.substring(0, 10));
    Serial.print("Data time: "); Serial.println(dataTimeStamp.substring(11, 16));

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
  Serial.println(timeStamp); //current date and time
  String siteName = location["name"];
  if (siteName.length() != 0) {
    Serial.print("Location: "); Serial.println(siteName);
    String dataTimeStamp = DV["dataDate"];
    //  Serial.print("Data timestamp: "); Serial.println(dataTimeStamp);
    Serial.print("Data date: "); Serial.println(dataTimeStamp.substring(0, 10));
    Serial.print("Data time: "); Serial.println(dataTimeStamp.substring(11, 16));

    int rainTime; //last observation with weather type
    String rainDate;
    bool checking = true;
    for (int d = 0; d < 5; d++) {
      for (int a = 0; a < 8; a++) {
        if (checking) {
          JsonObject& temp = root["SiteRep"]["DV"]["Location"]["Period"][d]["Rep"][a];
          if (temp["W"] >= 9) { //
            rainTime = temp["$"]; //time of rain
            rainDate = root["SiteRep"]["DV"]["Location"]["Period"][d]["value"].asString();
            checking = false;
          }
        }
      }
    }

    if (checking) Serial.println("No rain forecast in next 5 days");
    else {
      Serial.print("Rain time: "); Serial.println((rainTime / 60) * 100);

      //    JsonObject& rainDay = root["SiteRep"]["DV"]["Location"]["Period"][rainDate];
      Serial.print("Rain date: "); Serial.println(rainDate);
      //    Serial.println(rainDay["type"]);

      //    print rain forecast
      //    for (JsonObject::iterator it = rainDay.begin(); it != rainDay.end(); ++it) {
      //      Serial.print(it->key);
      //      Serial.print(" : ");
      //      Serial.println(it->value.asString());
      //    }
    }

    //    dayMaxTemp = todayDay["Dm"];
    //    Serial.print("Max temp: "); Serial.print(dayMaxTemp); Serial.println(" deg C");
    //    nightMinTemp = todayNight["Nm"];
    //    Serial.print("Min temp: "); Serial.print(nightMinTemp); Serial.println(" deg C");
    //    dayType = todayDay["W"];
    //    Serial.print("Day weather type: "); Serial.println(weatherTypes[dayType]);
    //    nightType = todayNight["W"];
    //    Serial.print("Night weather type: "); Serial.println(weatherTypes[nightType]);
    //    dayPProb = todayDay["PPd"];
    //    Serial.print("Day precipitation probability: "); Serial.print(dayPProb); Serial.println("%");
    //    nightPProb = todayNight["PPn"];
    //    Serial.print("Night precipitation probability: "); Serial.print(nightPProb); Serial.println("%");
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
  Serial.println(timeStamp); //current date and time
  String siteName = location["name"];
  if (siteName.length() != 0) {
    Serial.print("Location: "); Serial.println(siteName);
    String dataTimeStamp = DV["dataDate"];
    //  Serial.print("Data timestamp: "); Serial.println(dataTimeStamp);
    Serial.print("Data date: "); Serial.println(dataTimeStamp.substring(0, 10));
    Serial.print("Data time: "); Serial.println(dataTimeStamp.substring(11, 16));

    int last; //last observation with weather type
    bool checking = true;
    for (int a = 0; a < 24; a++) {
      if (checking) {
        JsonObject& temp = root["SiteRep"]["DV"]["Location"]["Period"][1]["Rep"][a];
        if (!temp.containsKey("$")) { //the last observation has been passed
          //****add something here to identify and move back to last observation with "W", as often missing
          last = a - 1;
          checking = false;
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
    return true;
  } else {
    Serial.println("JSON empty - no data returned");
    return false;
  }
}

//control of OLED display
void updateDisplay(int displayMode) {
  switch (displayMode) {
    case 0: // observation
      updateRGB(1); //red
      display.clearDisplay();
      display.setTextSize(2);
      display.setTextColor(WHITE);
      if (bitmapDisplay(currentType)) {
        display.setCursor(0, 34);
      } else {
        display.setCursor(0, 0);
        display.setTextWrap(false);
        display.println(weatherTypes[currentType]);
      }
      display.print(currentTemp);
      display.print((char)247); //degrees symbol
      display.println("C");
      display.display();
      break;

    case 1: // day forecast
      updateRGB(2); //green
      display.clearDisplay();
      display.setTextSize(2);
      display.setTextColor(WHITE);
      if (bitmapDisplay(dayType)) {
        display.setCursor(0, 34);
      } else {
        display.setCursor(0, 0);
        display.setTextWrap(false);
        display.println(weatherTypes[dayType]);
      }
      display.print(dayMaxTemp);
      display.print((char)247); //degrees symbol
      display.println("C");
      display.display();
      break;

    case 2: // night forecast
      updateRGB(3); //green
      display.clearDisplay();
      display.setTextSize(2);
      display.setTextColor(WHITE);
      if (bitmapDisplay(nightType)) {
        display.setCursor(0, 34);
      } else {
        display.setCursor(0, 0);
        display.setTextWrap(false);
        display.println(weatherTypes[nightType]);
      }
      display.print(nightMinTemp);
      display.print((char)247); //degrees symbol
      display.println("C");
      display.display();
      break;
  }
}

bool bitmapDisplay(int weather) {
  if (weather == 2 || weather == 3 ) { // partly cloudy (day/night) ***add seperate night version with moon in place of sun
    display.drawBitmap(0, 0, partlyCloudy, 40, 32, 1);
    return true;
  } else if (weather == 7 || weather == 8 ) { // cloudy / overcast
    display.drawBitmap(0, 0, cloudy, 48, 32, 1);
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
