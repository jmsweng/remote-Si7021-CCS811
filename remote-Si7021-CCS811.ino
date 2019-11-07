#include <PubSubClient.h>
#include <ESP8266WiFi.h>
#include "ArduinoJson.h"
#include "Adafruit_CCS811.h"
#include "Adafruit_Si7021.h"
#include "secrets.h"

//wifi setup
#define wifi_ssid SECRET_WIFI_SSID
#define wifi_password SECRET_WIFI_PASSWORD
WiFiClient espClient;

// MQTT setup
#define MQTT_CLIENT_NAME "remote_Si7021_CCS811"
#define mqtt_server SECRET_MQTT_SERVER
//#define mqtt_user "user"
//#define mqtt_password "password"
char* mqtt_topic = "sensors/multisensor";
PubSubClient client(espClient);

// JSON setup
const size_t capacity = JSON_OBJECT_SIZE(1) + 2*JSON_OBJECT_SIZE(3) + JSON_OBJECT_SIZE(6)+500;
DynamicJsonDocument doc(capacity);
JsonObject ESP8266_obj = doc.createNestedObject("ESP8266");
JsonObject Si7021_obj = doc.createNestedObject("Si7021");
JsonObject CCS811_obj = doc.createNestedObject("CCS811");

// CCS811 setup
int readings = 1;
int readings_to_avg = 1;

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

void setup()
{
  WiFi.mode(WIFI_STA);
  Serial.begin(115200);
  setup_wifi();
  client.setServer(mqtt_server, 1883);
  reconnect();
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
//    switch(sensor.getModel())
//    {
//    case SI_Engineering_Samples:
//      Si7021_obj["Model"] = "SI engineering samples"; break;
//    case SI_7013:
//      Si7021_obj["Model"] = "Si7013"; break;
//    case SI_7020:
//      Si7021_obj["Model"] = "Si7020"; break;
//    case SI_7021:
//      Si7021_obj["Model"] = "Si7021"; break;
//    case SI_UNKNOWN:
//    default:
//      Si7021_obj["Model"] = "Unknown";
//    }
    //Si7021_obj["Rev"] = sensor.getRevision();
    //Si7021_obj["SerialNum"] = String(sensor.sernum_a, HEX) + String(sensor.sernum_b, HEX);
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

  // process data from Si7021
  float newHum = sensor.readHumidity();
  float newTempC = sensor.readTemperature();
  float newTemp = Fahrenheit(newTempC);
  if (checkBound(newTemp, temp, diffTemp))
  {
    temp = newTemp;
    Serial.print("New temperature: ");
    Serial.println(String(temp).c_str());
    Si7021_obj["Temperature"] = temp;
    updateFlag = true;
  }

  if (checkBound(newHum, hum, diffRH))
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
            Serial.print("New eCO2: ");
            Serial.println(eCO2);
            CCS811_obj["eCO2"] = eCO2;
            updateFlag = true;
          }
          //if (checkBound(newTVOC, TVOC, diffTVOC))
          if (newTVOC != TVOC)
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
