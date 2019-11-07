#include <PubSubClient.h>
#include <ESP8266WiFi.h>
#include "ArduinoJson.h"
#include "Adafruit_CCS811.h" 
//#include "Adafruit_Si7021.h"
#include "secrets.h"
#include <Wire.h>
//#include <SPI.h>
#include <Adafruit_Sensor.h>
#include <Adafruit_BME280.h>

//wifi setup
#define wifi_ssid SECRET_WIFI_SSID
#define wifi_password SECRET_WIFI_PASSWORD
WiFiClient espClient;

// MQTT setup
#define MQTT_CLIENT_NAME "D1MiniMultisensor_5"
#define mqtt_server SECRET_MQTT_SERVER
//#define mqtt_user "user"
//#define mqtt_password "password"
char* mqtt_topic = "sensors/multisensor";
PubSubClient client(espClient);

// JSON setup
const size_t capacity = JSON_OBJECT_SIZE(1) + 2*JSON_OBJECT_SIZE(3) + JSON_OBJECT_SIZE(6)+500;
DynamicJsonDocument doc(capacity);
JsonObject ESP8266_obj = doc.createNestedObject("ESP8266");
//JsonObject Si7021_obj = doc.createNestedObject("Si7021");
JsonObject BME280_obj = doc.createNestedObject("BME280");
JsonObject CCS811_obj = doc.createNestedObject("CCS811");

// CCS811 setup
// CCS811 address needs to be changed to 0x5B in Adafruit_CCS881.h when using sparkfun PID 15269
int readings = 1;
int readings_to_avg = 1;

float eCO2 = 0;
float neweCO2;
float TVOC = 0;
float newTVOC;
//float diffeCO2 = 25;
//float diffTVOC = 5;

Adafruit_CCS811 ccs;

// Si7021 setup
//float hum = 0.0;
//float temp = 0.0;
//float diffTemp = 0.2;
//float diffRH = 0.5;

//Adafruit_Si7021 sensor = Adafruit_Si7021();

// BME280 setup (I2C not SPI)
#define SEALEVELPRESSURE_HPA (1013.25) //Pressure at sea level
Adafruit_BME280 bme; // I2C
float hum = 0.0;
float temp = 0.0;
float pressure = 0.0;
float altitude = 0.0;
float diffTemp = 0.2;
float diffRH = 1;
float diffPressure = 0.08; // Only consider changes that correspond to ~1 foot elevation change
float diffAltitude = 0.6; // Only consider changes of ~2 foot

// general setup
bool updateFlag = false;

void setup()
{
  WiFi.mode(WIFI_STA);
  Serial.begin(115200);
  setup_wifi();
  client.setServer(mqtt_server, 1883);
  reconnect();
  ESP8266_obj["ChipID"] = ESP.getChipId();
  Serial.println(ESP.getChipId());
  // start BME280
  if (!bme.begin())
  {
    Serial.println("Did not find BME280 sensor!");
    BME280_obj["Status"] = "ERROR";
  }
  else
  {
    BME280_obj["Status"] = "OK";
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
  if(BME280_obj["Status"] == "ERROR" || CCS811_obj["Status"] == "ERROR")
  {
    sendJSONviaMQTT(doc, mqtt_topic);
  }
}

void loop()
{
  //reconnect to MQTT broker if necessary
  if (!client.connected())
  {
    reconnect();
  }
  client.loop();

  // process data from BME280
  float newHum = bme.readHumidity();
  float newTempC = bme.readTemperature();
  float newTemp = Fahrenheit(newTempC);
  float newPressure = (bme.readPressure() / 100.0F);
  float newAltitude = (bme.readAltitude(SEALEVELPRESSURE_HPA));
  
  if (checkBound(newTemp, temp, diffTemp))
  {
    temp = newTemp;
    Serial.print("New temperature: ");
    Serial.println(String(temp).c_str());
    BME280_obj["Temperature"] = temp;
    updateFlag = true;
  }

  if (checkBound(newHum, hum, diffRH))
  {
    hum = newHum;
    Serial.print("New humidity: ");
    Serial.println(String(hum).c_str());
    BME280_obj["Humidity"] = hum;
    updateFlag = true;
  }

  if (checkBound(newPressure, pressure, diffPressure))
  {
    pressure = newPressure;
    Serial.print("New Pressure (hPa): ");
    Serial.println(String(pressure).c_str());
    BME280_obj["Pressure"] = pressure;
    updateFlag = true;
  }
  
  if (checkBound(newAltitude, altitude, diffAltitude))
  {
    altitude = newAltitude;
    Serial.print("New altitude (m): ");
    Serial.println(String(altitude).c_str());
    BME280_obj["Altitude"] = altitude;
    updateFlag = true;
  }

  // process data from CCS811
  if(ccs.available())
  {
    ccs.setEnvironmentalData(int(hum + 0.5), newTempC);
    if(!ccs.readData())
    {
      int eCO2reading = ccs.geteCO2();
      int TVOCreading = ccs.getTVOC();
      Serial.print(TVOCreading);
      Serial.print(", ");
      //neweCO2 += float(eCO2reading);
      //newTVOC += float(TVOCreading);
      neweCO2 = float(eCO2reading);
      newTVOC = float(TVOCreading);
      //readings++;
      if(readings == readings_to_avg)
      {
        //readings = 0;
        //neweCO2 /= readings_to_avg;
        //newTVOC /= readings_to_avg;

        if (millis() > 60000) // allow sensor to warm up
        {
          //if (checkBound(neweCO2, eCO2, diffeCO2))
          if (neweCO2 != eCO2)
          {
            eCO2 = neweCO2;
            Serial.print("\n");
            Serial.print("New eCO2 (ppm): ");
            Serial.println(eCO2);
            CCS811_obj["eCO2"] = eCO2;
            updateFlag = true;
          }
          //if (checkBound(newTVOC, TVOC, diffTVOC))
          if (newTVOC != TVOC)
          {
            TVOC = newTVOC;
            Serial.print("New TVOC (ppb): ");
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
    sendJSONviaMQTT(doc, mqtt_topic);
    updateFlag = false;
  }
}

void sendJSONviaMQTT(DynamicJsonDocument doc, char* mqtt_topic)
{
  Serial.println("Sending the following MQTT Message...");
  serializeJson(doc, Serial);
  Serial.println("");
  char buffer[1024];
  size_t n = serializeJson(doc, buffer);
  if(client.publish(mqtt_topic, buffer, n))
  {
    Serial.println("MQTT publish success.");
  }
  else
  {
    Serial.println("MQTT publish error.");
  }
}

void setup_wifi() 
{
  delay(10);
  // We start by connecting to a WiFi network
  Serial.println();
  Serial.print("Connecting to ");
  Serial.println(wifi_ssid);

  WiFi.begin(wifi_ssid, wifi_password);

  while (WiFi.status() != WL_CONNECTED)
  {
    delay(500);
    Serial.print(".");
  }

  Serial.println("");
  Serial.println("WiFi connected");
  Serial.println("IP address: ");
  Serial.println(WiFi.localIP());
}

void reconnect()
{
  // Loop until we're reconnected
  while (!client.connected())
  {
    Serial.print("Attempting MQTT connection...");
    // Attempt to connect
    // If you do not want to use a username and password, change next line to
    // if (client.connect("ESP8266Client"))
    if (client.connect(MQTT_CLIENT_NAME))//, mqtt_user, mqtt_password))
    {
      Serial.println("connected");
    }
    else
    {
      Serial.print("failed, rc=");
      Serial.print(client.state());
      Serial.println(" try again in 5 seconds");
      // Wait 5 seconds before retrying
      delay(5000);
    }
  }
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
