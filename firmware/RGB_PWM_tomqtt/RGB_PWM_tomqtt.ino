#include <FS.h>                   //this needs to be first, or it all crashes and burns...
//find code at
//https://github.com/CurlyWurly-1/ESP8266-WIFIMANAGER-MQTT/blob/master/MQTT_with_WiFiManager.ino
#include <ESP8266WiFi.h>          //https://github.com/esp8266/Arduino
//needed for library
#include <DNSServer.h>
#include <ESP8266WebServer.h>
#include <WiFiManager.h>          //https://github.com/tzapu/WiFiManager
#include <ArduinoJson.h>          //https://github.com/bblanchon/ArduinoJson
#include <PubSubClient.h>

#include <stdio.h>
#include <string.h>


//flag for saving data
bool shouldSaveConfig = false;
int firsttimearound = 1; 

const long oneSecond = 1000;  // a second is a thousand milliseconds
//const long oneSecond = 1000000;  // a second is a 1 million microseconds
const long oneMinute = oneSecond * 60;
const long oneHour   = oneMinute * 60;
const long oneDay    = oneHour * 24;

const int sleepTimeS = 3600; //1 hour in seocnds

String StatusofLED = "off";

//rgb
//12 //13 //14
#define redPin 13
#define greenPin 12
#define bluePin 14
#define buzzerPin 0


//define your default values here, if there are different values in config.json, they are overwritten.
//char mqtt_server[40];
#define mqtt_server       "your_mqtt_server_address"
#define mqtt_port         "1883"
#define mqtt_user         "username"
#define mqtt_pass         "password"
#define mqtt_topic_prefix "/rgbunit/pwm/"
#define mqtt_topic_firsttimearound "/pwm/"

String composeClientID() {
  uint8_t mac[6];
  WiFi.macAddress(mac);
  String clientId;
  
  clientId += macToStr(mac);
  return clientId;
}

String subscribetopicred =  mqtt_topic_prefix + composeClientID() + "/red";
String subscribetopicgreen =  mqtt_topic_prefix + composeClientID() + "/green";
String subscribetopicblue =  mqtt_topic_prefix + composeClientID() + "/blue";
String subscribetopiconoff =  mqtt_topic_prefix + composeClientID() + "/onoff";



String mac; 

WiFiClient espClient;
PubSubClient client(espClient);

//callback notifying us of the need to save config
void saveConfigCallback () {
  Serial.println("Should save config");
  shouldSaveConfig = true;
}


void setup() {
  // put your setup code here, to run once:
  Serial.begin(9600);
  Serial.println();
  pinMode(redPin, OUTPUT);
  pinMode(greenPin, OUTPUT);
  pinMode(bluePin, OUTPUT);
  pinMode(buzzerPin, OUTPUT);
  
  analogWrite(redPin, 0); 
  analogWrite(greenPin, 0); 
  analogWrite(bluePin, 0); 
  digitalWrite(buzzerPin, 1); //pulldown added

  //clean FS for testing
  //  SPIFFS.format();

  //read configuration from FS json
  Serial.println("mounting FS...");

  if (SPIFFS.begin()) {
    Serial.println("mounted file system");
    if (SPIFFS.exists("/config.json")) {
      //file exists, reading and loading
      Serial.println("reading config file");
      File configFile = SPIFFS.open("/config.json", "r");
      if (configFile) {
        Serial.println("opened config file");
        size_t size = configFile.size();
        // Allocate a buffer to store contents of the file.
        std::unique_ptr<char[]> buf(new char[size]);

        configFile.readBytes(buf.get(), size);
        DynamicJsonBuffer jsonBuffer;
        JsonObject& json = jsonBuffer.parseObject(buf.get());
        json.printTo(Serial);
        if (json.success()) {
          Serial.println("\nparsed json");
          strcpy(mqtt_server, json["mqtt_server"]);
          strcpy(mqtt_port, json["mqtt_port"]);
          strcpy(mqtt_user, json["mqtt_user"]);
          strcpy(mqtt_pass, json["mqtt_pass"]);

        } else {
          Serial.println("failed to load json config");
        }
      }
    }
  } else {
    Serial.println("failed to mount FS");
  }
  //end read



  // The extra parameters to be configured (can be either global or just in the setup)
  // After connecting, parameter.getValue() will get you the configured value
  // id/name placeholder/prompt default length
  WiFiManagerParameter custom_mqtt_server("server", "mqtt server", mqtt_server, 40);
  WiFiManagerParameter custom_mqtt_port("port", "mqtt port", mqtt_port, 6);
  WiFiManagerParameter custom_mqtt_user("user", "mqtt user", mqtt_user, 20);
  WiFiManagerParameter custom_mqtt_pass("pass", "mqtt pass", mqtt_pass, 20);

  //WiFiManager
  //Local intialization. Once its business is done, there is no need to keep it around
  WiFiManager wifiManager;

  // Reset Wifi settings for testing
  //  wifiManager.resetSettings();

  //set config save notify callback
  wifiManager.setSaveConfigCallback(saveConfigCallback);

  //set static ip
  //  wifiManager.setSTAStaticIPConfig(IPAddress(10,0,1,99), IPAddress(10,0,1,1), IPAddress(255,255,255,0));

  //add all your parameters here
  wifiManager.addParameter(&custom_mqtt_server);
  wifiManager.addParameter(&custom_mqtt_port);
  wifiManager.addParameter(&custom_mqtt_user);
  wifiManager.addParameter(&custom_mqtt_pass);

  //reset settings - for testing
  //wifiManager.resetSettings();

  //set minimum quality of signal so it ignores AP's under that quality
  //defaults to 8%
  //wifiManager.setMinimumSignalQuality();

  //sets timeout until configuration portal gets turned off
  //useful to make it all retry or go to sleep
  //in seconds
  //wifiManager.setTimeout(120);

  //fetches ssid and pass and tries to connect
  //if it does not connect it starts an access point with the specified name
  //here  "AutoConnectAP"
  //and goes into a blocking loop awaiting configuration
  String apname = "AutoConnect" + composeClientID();
   
  if (!wifiManager.autoConnect(String(apname).c_str(), "rftagit")) {
    Serial.println("failed to connect and hit timeout");
    delay(3000);
    //reset and try again, or maybe put it to deep sleep
    ESP.reset();
    delay(5000);
  }

  //if you get here you have connected to the WiFi
  Serial.println("connected...yeey :)");
  //WiFi.macAddress(mac);

  //read updated parameters
  strcpy(mqtt_server, custom_mqtt_server.getValue());
  strcpy(mqtt_port, custom_mqtt_port.getValue());
  strcpy(mqtt_user, custom_mqtt_user.getValue());
  strcpy(mqtt_pass, custom_mqtt_pass.getValue());
  // strcpy(blynk_token, custom_blynk_token.getValue());

  //save the custom parameters to FS
  if (shouldSaveConfig) {
    Serial.println("saving config");
    DynamicJsonBuffer jsonBuffer;
    JsonObject& json = jsonBuffer.createObject();
    json["mqtt_server"] = mqtt_server;
    json["mqtt_port"] = mqtt_port;
    json["mqtt_user"] = mqtt_user;
    json["mqtt_pass"] = mqtt_pass;

    File configFile = SPIFFS.open("/config.json", "w");
    if (!configFile) {
      Serial.println("failed to open config file for writing");
    }

    json.printTo(Serial);
    json.printTo(configFile);
    configFile.close();
    //end save
  }

  Serial.println("local ip");
  Serial.println(WiFi.localIP());
  //  client.setServer(mqtt_server, 12025);
  //const uint16_t mqtt_port_x = 12025;
  client.setServer(mqtt_server, 1883);
  client.setCallback(callback);
}


void reconnect() {
  // Loop until we're reconnected
  while (!client.connected()) {
    Serial.print("Attempting MQTT connection...");
    // Attempt to connect
    // If you do not want to use a username and password, change next line to
    // if (client.connect("ESP8266Client")) {
    String clientname = "ESP8266Client" + composeClientID();
    if (client.connect(String(clientname).c_str(), mqtt_user, mqtt_pass)) {
      //Comment out to stop flashing of leds jkw
      //client.subscribe(subscribetopiconoff.c_str());
      //Serial.println(subscribetopiconoff.c_str());
      
      client.subscribe(subscribetopicred.c_str());
      Serial.println(subscribetopicred.c_str());
      
      client.subscribe(subscribetopicgreen.c_str());
      Serial.println(subscribetopicgreen.c_str());
      
      client.subscribe(subscribetopicblue.c_str());
      Serial.println(subscribetopicblue.c_str());
      Serial.println("connected");
    } else {
      Serial.print("failed, rc=");
      Serial.print(client.state());
      Serial.println(" try again in 5 seconds");
      // Wait 5 seconds before retrying
      delay(5000);
    }
  }
}


bool checkBound(float newValue, float prevValue, float maxDiff) {
  return !isnan(newValue) &&
         (newValue < prevValue - maxDiff || newValue > prevValue + maxDiff);
}

long lastMsg = 0;
float temp = 0.0;
float hum = 0.0;
float diff = 1.0;

long duration, distance; // Duration used to calculate distance

void loop() {
  // put your main code here, to run repeatedly:
  if (!client.connected()) {
    reconnect();
  }
  client.loop();

  char string[50] = "";
  char mqtt_topic[40] = "";



  String topic =  mqtt_topic_prefix + composeClientID();
  String topic_firsttimearound =  mqtt_topic_firsttimearound + composeClientID();

  if(firsttimearound == 1)
  {
    client.publish(String(topic_firsttimearound).c_str() , String(StatusofLED).c_str(), false);
    firsttimearound = 0;
  }
}

String macToStr(const uint8_t* mac)
{
  String result;
  for (int i = 0; i < 6; ++i) {
    result += String(mac[i], 16);
    
  }
  return result;
}



void callback(char* topic, byte* payload, unsigned int length) {
  int value = 0;
  Serial.print("Message arrived [");
  Serial.print(topic);
  Serial.print("] ");
  for (int i = 0; i < length; i++) {
    Serial.print((char)payload[i]);
  }
  Serial.println("");
  
  //client.publish(String(topic).c_str() , String(StatusofLED).c_str(), false);
 
  payload[length] = '\0';
  String s = String((char*)payload);
  value = s.toInt();

  //remember to uncomment  remrk for thi topi in the reconnect section
  if (strcmp(topic,subscribetopiconoff.c_str())==0){
    Serial.println("onoff mqtt message received");
    // Switch on the LED if an 1 was received as first character
  if ((char)payload[0] == '1') {
    analogWrite(redPin, 255); 
    analogWrite(greenPin, 255); 
    analogWrite(bluePin, 255); 
    digitalWrite(buzzerPin, 1); 
    StatusofLED = "on";
  } else {
    analogWrite(redPin, 0); 
    analogWrite(greenPin, 0); 
    analogWrite(bluePin, 0); 
    digitalWrite(buzzerPin, 0); 
     StatusofLED = "off";
  }
  }
 
  if (strcmp(topic,subscribetopicred.c_str())==0) {
    //Serial.println(value);
    analogWrite(redPin, value); 
     StatusofLED = "on";
  }
 
  if (strcmp(topic,subscribetopicblue.c_str())==0) {
    //Serial.println(value);
    analogWrite(bluePin, value); 
     StatusofLED = "on";
  }  
 
  if (strcmp(topic,subscribetopicgreen.c_str())==0) {
    //Serial.println(value);
    analogWrite(greenPin, value); 
     StatusofLED = "on";
  }  
}
