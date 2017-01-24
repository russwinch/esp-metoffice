//needed for library
#include <FS.h>                   //this needs to be first, or it all crashes and burns...
#include <DNSServer.h>
#include <ESP8266WebServer.h>
#include <WiFiManager.h>          //https://github.com/tzapu/WiFiManager

//libraries already included in main sketch
//#include <ESP8266WiFi.h>          //https://github.com/esp8266/Arduino
//#include <ArduinoJson.h>          //https://github.com/bblanchon/ArduinoJson

////define your default values here, if there are different values in config.json, they are overwritten.
//char forecast_id[8]; //forecast site ID
//char observation_id[6]; //observation site ID
//char api_key[40]; //api key

//flag for saving data
bool shouldSaveConfig = false;

//time to show messages
int msgDel = 100;

void logMsg(char *m, char *n) {
  display.clearDisplay(); // Clear the buffer.
  display.setCursor(0, 0);
  display.println(m);
  display.println(n);
  display.display();
  Serial.println(m);
  Serial.println(n);
  delay(msgDel);
}

//callback notifying us of the need to save config
void saveConfigCallback () {
  Serial.println("Should save config");
  shouldSaveConfig = true;
}

void configModeCallback (WiFiManager *myWiFiManager) {
  display.clearDisplay(); // Clear the buffer.
  display.setTextSize(2);
  display.setCursor(0, 0);
  display.setTextColor(WHITE); // normal text
//  display.setTextColor(BLACK, WHITE); // 'inverted' text
  display.println("SSID:");
//  display.setTextColor(WHITE, BLACK); // normal text
  display.println(ap_ssid);
//  display.setTextColor(BLACK, WHITE); // 'inverted' text
  display.println("Password:");
//  display.setTextColor(WHITE, BLACK); // normal text
  display.println(ap_password);
  display.display();

  Serial.println("Entered config mode");
  Serial.println(WiFi.softAPIP());

  Serial.println(myWiFiManager->getConfigPortalSSID());
}

bool manageWifi() {
  //  Serial.begin(115200);

  //clean FS, for testing
  //  SPIFFS.format();

  //  display.clearDisplay(); // Clear the buffer.
  display.setTextSize(1);
  display.setTextColor(WHITE);
  //  display.setCursor(0, 0);

  //read configuration from FS json
  if (logging) logMsg("1/4.", "mounting FS...");
  if (SPIFFS.begin()) {
    if (logging) logMsg("2/4.", "mounted file system");
    if (SPIFFS.exists("/config.json")) {
      //file exists, reading and loading
      if (logging) logMsg("3/4.", "reading config file");
      File configFile = SPIFFS.open("/config.json", "r");
      if (configFile) {
        if (logging) logMsg("4/4.", "opened config file");
        size_t size = configFile.size();
        // Allocate a buffer to store contents of the file.
        std::unique_ptr<char[]> buf(new char[size]);

        configFile.readBytes(buf.get(), size);
        DynamicJsonBuffer jsonBuffer;
        JsonObject& json = jsonBuffer.parseObject(buf.get());
        json.printTo(Serial);
        if (json.success()) {
          Serial.println("\nparsed json");

          strcpy(forecast_id, json["forecast_id"]);
          strcpy(observation_id, json["observation_id"]);
          strcpy(api_key, json["api_key"]);
          
        } else {
          if (logging) logMsg("failed to load json config", "");
          //catch error***
          return false;
        }
      }
    }
  } else {
    if (logging) logMsg("failed to mount FS", "");
    //catch error***
    return false;
  }
  //end read

  // The extra parameters to be configured (can be either global or just in the setup)
  // After connecting, parameter.getValue() will get you the configured value
  // id/name placeholder/prompt default length
  WiFiManagerParameter metoffice_text("<h3>Met Office details</h3><b>Forecast ID</b>");
  WiFiManagerParameter custom_forecast_id("forecast_id", "forecast id", forecast_id, 8);
  WiFiManagerParameter forecast_text("<br>To obtain a forecast site ID, call the following to return JSON of all sites: http://datapoint.metoffice.gov.uk/public/data/val/wxfcs/all/json/sitelist?key=YOUR_API_Key<br><br><b>Observation ID</b>");
  WiFiManagerParameter custom_observation_id("observation_id", "observation id", observation_id, 16);
  WiFiManagerParameter observation_text("<br>To obtain an observation site ID, call the following to return JSON of all sites: http://datapoint.metoffice.gov.uk/public/data/val/wxobs/all/json/sitelist?key=YOUR_API_Key<br><br><b>API Key</b>");
  WiFiManagerParameter custom_api_key("api_key", "api key", api_key, 40);
  WiFiManagerParameter api_text("<br>Register for Met Office Datapoint here: <a href='http://www.metoffice.gov.uk/datapoint'>metoffice.gov.uk/datapoint</a>");
  //WiFiManager
  //Local intialization. Once its business is done, there is no need to keep it around
  WiFiManager wifiManager;

  //set config save notify callback
  wifiManager.setSaveConfigCallback(saveConfigCallback);

  //set static ip
  //  wifiManager.setSTAStaticIPConfig(IPAddress(10,0,1,99), IPAddress(10,0,1,1), IPAddress(255,255,255,0));

  //add all your parameters here
  wifiManager.addParameter(&metoffice_text);
  wifiManager.addParameter(&custom_forecast_id);
  wifiManager.addParameter(&forecast_text);
  wifiManager.addParameter(&custom_observation_id);
  wifiManager.addParameter(&observation_text);
  wifiManager.addParameter(&custom_api_key);
  wifiManager.addParameter(&api_text);

  //reset settings - for testing
  //    wifiManager.resetSettings();

  //set minimu quality of signal so it ignores AP's under that quality
  //defaults to 8%
  //wifiManager.setMinimumSignalQuality();

  //sets timeout until configuration portal gets turned off
  //useful to make it all retry or go to sleep
  //in seconds
    wifiManager.setTimeout(600);
//  wifiManager.setTimeout(120);


  wifiManager.setAPCallback(configModeCallback);

  //go into AP mode if button held down at startup
  if (digitalRead(switchPin) == HIGH) wifiManager.startConfigPortal(ap_ssid, ap_password);
  
  //fetches ssid and pass and tries to connect
  //if it does not connect it starts an access point with the specified name
  //and goes into a blocking loop awaiting configuration
  else if (!wifiManager.autoConnect(ap_ssid, ap_password)) {
    if (logging) logMsg("failed to connect and hit timeout", "");
    delay(3000);
    //reset and try again, or maybe put it to deep sleep
    ESP.reset();
    delay(5000);
  }

  //if you get here you have connected to the WiFi
  if (logging) logMsg("connected.", "");

  //read updated parameters
  strcpy(forecast_id, custom_forecast_id.getValue());
  strcpy(observation_id, custom_observation_id.getValue());
  strcpy(api_key, custom_api_key.getValue());

  //save the custom parameters to FS
  if (shouldSaveConfig) {
    if (logging) logMsg("saving config.", "");
    DynamicJsonBuffer jsonBuffer;
    JsonObject& json = jsonBuffer.createObject();
    json["forecast_id"] = forecast_id;
    json["observation_id"] = observation_id;
    json["api_key"] = api_key;

    File configFile = SPIFFS.open("/config.json", "w");
    if (!configFile) {
      //catch error***
      if (logging) logMsg("failed to open config file for writing", "");
      return false;
    }
    json.printTo(Serial);
    json.printTo(configFile);
    configFile.close();
    //end save
  }
  if (logging) {
    display.println("local ip");
    display.println(WiFi.localIP());
    display.display();
    Serial.println("local ip.");
    Serial.println(WiFi.localIP());
    delay(msgDel);
  }
  // if (logging) logMsg("local ip", WiFi.localIP()); //doesn't work :(
  return true;
}



