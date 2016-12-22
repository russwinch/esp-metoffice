/*
  Retrieving weather forecast from the UK's Met Office: http://www.metoffice.gov.uk/
  Russ Winch - December 2016
  https://github.com/russwinch/esp-metoffice.git

  - Based on the Weather Underground sketch by bbx10: https://gist.github.com/bbx10/149bba466b1e2cd887bf
  - Using the excellent and well documented Arduino JSON library: https://github.com/bblanchon/ArduinoJson
  - Met Office code definitions for weather types: http://www.metoffice.gov.uk/datapoint/support/documentation/code-definitions
  - Tested on Wemos D1 V2 with oled sheild
  - Using the modified version of the adafruit OLED driver by mcauser: https://github.com/mcauser/Adafruit_SSD1306/tree/esp8266-64x48

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

/***TODO***
  decide which forecast to show : day / night / next day
  ensure blank data is not returned and use previous data if it is (look for a location?)
  identify date and time of next rain
  ability to determine / set the current location (by IP ? )
  produce more icons
  implement reading from ldr to adjust led brightness
  implement timeout to turn the oled off after a delay
  add mode to show last data dates and times

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

// SCL GPIO5 (D1)
// SDA GPIO4 (D2)
#define OLED_RESET D3  // GPIO0
Adafruit_SSD1306 display(OLED_RESET);

//pin definitions
const int redPin = D7;
const int greenPin = D6;
const int bluePin = D5;
const int switchPin = D8; //built in pull-down
const int ldrPin = A0;

// Met Office settings
#define METOFFICE "datapoint.metoffice.gov.uk"
#define MO_FOREID "351713" //forecast site ID: Hackney
#define MO_OBVID "3672" //observation site ID: Northolt
// To obtain a forecast siteID, call the following to return JSON of all sites:
// http://datapoint.metoffice.gov.uk/public/data/val/wxfcs/all/json/sitelist?key=<APIKey>
// To obtain an observation siteID, call the following to return JSON of all sites:
// http://datapoint.metoffice.gov.uk/public/data/val/wxobs/all/json/sitelist?key=<APIKey>

/* Met Office fair use policy:
  You may make no more than 5000 data requests per day; and
  You may make no more than 100 data requests per minute. */

// 1 minute between update checks.
long updateDelay = (1 * 60000);

// Met office weather types
const char *weatherTypes[] = {"Clear night", "Sunny day", "Partly cloudy (night)", "Partly cloudy (day)", "Not used", "Mist", "Fog", "Cloudy", "Overcast", "Light rain shower (night)", "Light rain shower (day)", "Drizzle", "Light rain", "Heavy rain shower (night)", "Heavy rain shower (day)", "Heavy rain", "Sleet shower (night)", "Sleet shower (day)", "Sleet", "Hail shower (night)", "Hail shower (day)", "Hail", "Light snow shower (night)", "Light snow shower (day)", "Light snow", "Heavy snow shower (night)", "Heavy snow shower (day)", "Heavy snow", "Thunder shower (night)", "Thunder shower (day)", "Thunder"};

// HTTP requests
const char FORECAST_REQ[] =
  "GET /public/data/val/wxfcs/all/json/" MO_FOREID "?res=daily&key=" MO_API_KEY " HTTP/1.1\r\n"
  //  "GET /public/data/val/wxfcs/all/json/" MO_SITEID "?res=3hourly&key=" MO_API_KEY " HTTP/1.1\r\n" ///testing 3 hourly
  "User-Agent: ESP8266/0.1\r\n"
  "Accept: */*\r\n"
  "Host: " METOFFICE "\r\n"
  "Connection: close\r\n"
  "\r\n";

const char OBSERVATION_REQ[] =
  "GET /public/data/val/wxobs/all/json/" MO_OBVID "?res=hourly&key=" MO_API_KEY " HTTP/1.1\r\n"
  //  "GET /public/data/val/wxfcs/all/json/" MO_SITEID "?res=3hourly&key=" MO_API_KEY " HTTP/1.1\r\n" ///testing 3 hourly
  "User-Agent: ESP8266/0.1\r\n"
  "Accept: */*\r\n"
  "Host: " METOFFICE "\r\n"
  "Connection: close\r\n"
  "\r\n";

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

//modes
int currentMode = 0;
int totalModes = 3;
bool switchReading;
bool lastSwitchReading;
unsigned long lastDebounceTime = 0;
unsigned long debounceDelay = 50;

void setup() {
  //inputs
  pinMode (ldrPin, INPUT);
  pinMode (switchPin, INPUT);

  //outputs
  pinMode (redPin, OUTPUT);
  pinMode (greenPin, OUTPUT);
  pinMode (bluePin, OUTPUT);
  pinMode(LED_BUILTIN, OUTPUT);

  //leds off
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

  // Connect to WiFi network
  Serial.println();
  Serial.print(F("Connecting to "));
  Serial.println(ssid);

  display.print("Connecting to ");
  display.println(ssid);
  display.display();

  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(F("."));
    display.print(".");
    display.display();
  }
  Serial.println();
  Serial.println(F("WiFi connected"));
  Serial.println(F("IP address: "));
  Serial.println(WiFi.localIP());

  display.clearDisplay();
  display.setCursor(0, 0);
  display.println("Connected.");
  display.println();
  display.println("IP address");
  display.println(WiFi.localIP());
  display.display();

  if (updateData()) updateDisplay(0);
}

void loop() {
  switchReading = digitalRead(switchPin);
  if ((switchReading != lastSwitchReading) && ((millis() - lastDebounceTime) > debounceDelay)) {
    lastSwitchReading = switchReading;
    lastDebounceTime = millis();
    if (switchReading == HIGH) {
      currentMode++;
      if (currentMode > (totalModes - 1)) currentMode = 0;
      updateDisplay(currentMode);
      //      Serial.print("mode: ");
      //      Serial.println(currentMode);
    }
  }

  if (millis() - lastUpdateAttempt > updateDelay) {
    if (updateData()) updateDisplay(currentMode);
    lastUpdateAttempt = millis();
  }
}

bool updateData() {
  char* response;
  response = httpReq(FORECAST_REQ);
  if (forecast(response)) {
    response = httpReq(OBSERVATION_REQ);
    if (observation(response)) return true;
  } else return false;
}

char* httpReq (const char* req) {
  static char respBuf[4096]; // response buffer for the JSON

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
      if (aLine.substring(0, 4) == "Date") timeStamp = aLine.substring(6); //looking for Date at the start of the line
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
    //    delay(DELAY_ERROR);
    //    return; ********************************** FIX THIS
  }
  // Terminate the C string
  respBuf[respLen++] = '\0';
  Serial.print(F("respLen "));
  Serial.println(respLen);
  //Serial.println(respBuf);
  return respBuf;
}

bool forecast(char *json) {
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
    //print day forecast
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

bool observation(char *json) {
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
          //add something here to identify last observation with "W" as often missing
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
      if (currentType == 2 || currentType == 3 || currentType == 7) { //if cloudy
        display.drawBitmap(0, 0, cloudy, 48, 32, 1);
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
      if (dayType == 2 || dayType == 3 || dayType == 7) { //if cloudy
        display.drawBitmap(0, 0, cloudy, 48, 32, 1);
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
      if (nightType == 2 || nightType == 3 || nightType == 7) { //if cloudy
        display.drawBitmap(0, 0, cloudy, 48, 32, 1);
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
}

