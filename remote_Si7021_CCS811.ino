#include <ESP8266WiFi.h>
#include "Adafruit_MQTT.h"
#include "Adafruit_MQTT_Client.h"
#include "ArduinoJson.h"
#include "Adafruit_CCS811.h"
#include "Adafruit_Si7021.h"
#include "secrets.h"

/************************* WiFi Access Point *********************************/
#define WLAN_SSID  SECRET_WIFI_SSID
// uncomment if using password to connect to wifi. also change in setup_wifi() below
//#define WLAN_PASS SECRET_WIFI_PASSWORD

/************************* Adafruit.io Setup *********************************/

#define AIO_SERVER      "io.adafruit.com"
// Using port 8883 for MQTTS
#define AIO_SERVERPORT  8883
// Adafruit IO Account Configuration
// (to obtain these values, visit https://io.adafruit.com and click on Active Key)
#define AIO_USERNAME    SECRET_AIO_USERNAME
#define AIO_KEY         SECRET_AIO_KEY

/************ Global State (you don't need to change this!) ******************/

// WiFiFlientSecure for SSL/TLS support
WiFiClientSecure client;

// Setup the MQTT client class by passing in the WiFi client and MQTT server and login details.
Adafruit_MQTT_Client mqtt(&client, AIO_SERVER, AIO_SERVERPORT, AIO_USERNAME, AIO_KEY);

// io.adafruit.com SHA1 fingerprint
static const char *fingerprint PROGMEM = "77 00 54 2D DA E7 D8 03 27 31 23 99 EB 27 DB CB A5 4C 57 18";

/****************************** Feeds ***************************************/

#define AIO_FEEDNAME    SECRET_AIO_FEEDNAME
// Setup a feed for publishing.
// Notice MQTT paths for AIO follow the form: <username>/feeds/<feedname>
Adafruit_MQTT_Publish feed = Adafruit_MQTT_Publish(&mqtt, AIO_USERNAME "/feeds/" SECRET_AIO_FEEDNAME);

/****************************** Sensor Setup ***************************************/

// JSON setup
const size_t capacity = JSON_OBJECT_SIZE(1) + 2*JSON_OBJECT_SIZE(3) + JSON_OBJECT_SIZE(6) + 500;
DynamicJsonDocument doc(capacity);
JsonObject ESP8266_obj = doc.createNestedObject("ESP8266");
JsonObject Si7021_obj = doc.createNestedObject("Si7021");
JsonObject CCS811_obj = doc.createNestedObject("CCS811");

// CCS811 setup
int readings = 0;
int readings_to_avg = 30;

float eCO2 = 0;
float neweCO2;
float TVOC = 0;
float newTVOC;
float diffeCO2 = 25;
float diffTVOC = 5;

Adafruit_CCS811 ccs;

// Si7021 setup
float hum = 0.0;
float temp = 0.0;
float diffTemp = 0.2;
float diffRH = 0.5;

Adafruit_Si7021 sensor = Adafruit_Si7021();

// general setup
bool updateFlag = false;
bool timeUpdateFlag = true; // init to true so first message always gets sent with all sensor values
unsigned long lastTimeUpdate = 0;

void setup()
{
  WiFi.mode(WIFI_STA);
  Serial.begin(115200);
  setup_wifi();
  MQTT_connect();
  ESP8266_obj["ChipID"] = ESP.getChipId();
  // start Si7021
  if (!sensor.begin())
  {
    Serial.println("Did not find Si7021 sensor!");
    Si7021_obj["Status"] = "ERROR";
  }
  else
  {
    Si7021_obj["Status"] = "OK";
   switch(sensor.getModel())
   {
   case SI_Engineering_Samples:
     Si7021_obj["Model"] = "SI engineering samples"; break;
   case SI_7013:
     Si7021_obj["Model"] = "Si7013"; break;
   case SI_7020:
     Si7021_obj["Model"] = "Si7020"; break;
   case SI_7021:
     Si7021_obj["Model"] = "Si7021"; break;
   case SI_UNKNOWN:
   default:
     Si7021_obj["Model"] = "Unknown";
   }
    Si7021_obj["Rev"] = sensor.getRevision();
    Si7021_obj["SerialNum"] = String(sensor.sernum_a, HEX) + String(sensor.sernum_b, HEX);
  }
  
  // start CCS811
  if(!ccs.begin())
  {
    Serial.println("Failed to start CCS811 sensor! Please check your wiring.");
    CCS811_obj["Status"] = "ERROR";
  }
  else
  {
    CCS811_obj["Status"] = "OK";
  }
  if(Si7021_obj["Status"] == "ERROR" || CCS811_obj["Status"] == "ERROR")
  {
    sendJSONviaMQTT(doc);
  }
}

void loop()
{
  //reconnect to MQTT broker if necessary
  MQTT_connect();

  if (millis() - lastTimeUpdate >= 5*60*1000) //5 minutes
  {
    timeUpdateFlag = true;
  }

  // process data from Si7021
  float newHum = sensor.readHumidity();
  float newTempC = sensor.readTemperature();
  float newTemp = Fahrenheit(newTempC);
  if (checkBound(newTemp, temp, diffTemp) || timeUpdateFlag)
  {
    temp = newTemp;
    Serial.print("New temperature: ");
    Serial.println(String(temp).c_str());
    Si7021_obj["Temperature"] = temp;
    updateFlag = true;
  }

  if (checkBound(newHum, hum, diffRH) || timeUpdateFlag)
  {
    hum = newHum;
    Serial.print("New humidity: ");
    Serial.println(String(hum).c_str());
    Si7021_obj["Humidity"] = hum;
    updateFlag = true;
  }

  // process data from CCS811
  if(ccs.available())
  {
    ccs.setEnvironmentalData(int(newHum + 0.5), newTempC);
    if(!ccs.readData())
    {
      int eCO2reading = ccs.geteCO2();
      int TVOCreading = ccs.getTVOC(); 
      neweCO2 += float(eCO2reading);
      newTVOC += float(TVOCreading);
      readings++;
      if(readings == readings_to_avg)
      {
        readings = 0;
        neweCO2 /= readings_to_avg;
        newTVOC /= readings_to_avg;

        if (millis() > 60000) // allow sensor to warm up
        {
          if (checkBound(neweCO2, eCO2, diffeCO2) || timeUpdateFlag)
          {
            eCO2 = neweCO2;
            Serial.print("New eCO2: ");
            Serial.println(eCO2);
            CCS811_obj["eCO2"] = eCO2;
            updateFlag = true;
          }
          if (checkBound(newTVOC, TVOC, diffTVOC) || timeUpdateFlag)
          {
            TVOC = newTVOC;
            Serial.print("New TVOC: ");
            Serial.println(TVOC);
            CCS811_obj["TVOC"] = TVOC;
            updateFlag = true;
          }
        }
      }
    }
  }

  // send data
  if (updateFlag)
  {
    if (timeUpdateFlag)
    {
      Serial.println("Max time between updates reached.");
      timeUpdateFlag = false;
    }
    lastTimeUpdate = millis(); // reset timer
    sendJSONviaMQTT(doc);
    updateFlag = false;
  }
}

void sendJSONviaMQTT(DynamicJsonDocument doc)
{
  Serial.println("Sending the following MQTT Message...");
  serializeJson(doc, Serial);
  Serial.println("");
  char buffer[1024];
  size_t n = serializeJson(doc, buffer);
  if (! feed.publish(buffer)) 
  {
    Serial.println(F("Publish Failed"));
  } 
  else 
  {
    Serial.println(F("Publish OK!"));
  }
}

void setup_wifi() 
{
  delay(10);
  // We start by connecting to a WiFi network
  Serial.println();
  Serial.print("Connecting to ");
  Serial.println(WLAN_SSID);

  //use one or the other
  WiFi.begin(WLAN_SSID, NULL); // open network
  //WiFi.begin(WLAN_SSID, WLAN_PASS); // password protected network

  while (WiFi.status() != WL_CONNECTED)
  {
    delay(500);
    Serial.print(".");
  }

  Serial.println("");
  Serial.println("WiFi connected");
  Serial.println("IP address: ");
  Serial.println(WiFi.localIP());

  // check the fingerprint of io.adafruit.com's SSL cert
  client.setFingerprint(fingerprint);
}

// Function to connect and reconnect as necessary to the MQTT server.
// Should be called in the loop function and it will take care of connecting.
void MQTT_connect() {
  int8_t ret;

  // Stop if already connected.
  if (mqtt.connected()) {
    return;
  }

  Serial.print("Connecting to MQTT... ");

  while ((ret = mqtt.connect()) != 0) { // connect will return 0 for connected
       Serial.println(mqtt.connectErrorString(ret));
       Serial.println("Retrying MQTT connection in 5 seconds...");
       mqtt.disconnect();
       delay(5000);  // wait 5 seconds
  }

  Serial.println("MQTT Connected!");
}

bool checkBound(float newValue, float prevValue, float maxDiff)
{
  return !isnan(newValue) && (newValue < prevValue - maxDiff || newValue > prevValue + maxDiff);
}

//Celsius to Fahrenheit conversion
double Fahrenheit(double celsius)
{
  return 1.8 * celsius + 32;
}
