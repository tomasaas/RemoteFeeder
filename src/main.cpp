#include <ESP32Servo.h>
#include "feed.h"
#include <WiFi.h>
// #include <WiFiClientSecure.h>
#include <PubSubClient.h>
#include <secrets.h> // wifi login

// Servo library
Servo myservo;

// WiFi library
WiFiClient wifiClient; // trenger en wifiklient til mqtt

// MQTT library
PubSubClient mqttClient(
    "194.164.61.72", 
    1883, 
    wifiClient); 

void reconnect() {
  // Loop until we're reconnected
  while (!mqttClient.connected()) {
    Serial.println("Attempting MQTT connection...");
    // Attempt to connect // med brukernavn og passord
    if (mqttClient.connect("RemoteFeederESP32", "tomasa", "marvin")) {
      Serial.println("MQTT broker connected successfully!");
      // Once connected, publish an announcement...
      mqttClient.publish("outTopic","hello world"); // sikkert unødvendig..?
      // ... and resubscribe
      mqttClient.subscribe("remotefeeder/feed");
    } else {
      Serial.print("failed, rc=");
      Serial.print(mqttClient.state()); // state returnerer (feil)koder. sjekk PubSubClient API – state()
      Serial.println(" try again in 5 seconds");
      // Wait 5 seconds before retrying
      delay(5000);
    }
  }
}

void callback(char* topic, byte* payload, unsigned int length) {
  Serial.print("Message arrived [");
    Serial.print(topic);
    Serial.println("]");

    if (strcmp(topic, "remotefeeder/feed") == 0) {
        feed(myservo);
    }
}


void setup() 
{
    Serial.begin(9600); // Default baud rate i pio device monitor -h

    //Servo library
    Serial.println("Attaching to servo...");
    myservo.attach(13, 1000, 2000);  // Attach to pin 13, min pulsbredde (1500 senturm vinkel), maks pulsbredde
    bool attached = myservo.attached();
    attached 
        ? Serial.println("Attached successfully to myservo!") 
        : Serial.println("Attachment to myservo failed");

    // WiFi library
    Serial.println("Connecting to WiFi...");
    WiFi.begin(WIFI_SSID, WIFI_PASSWD);
    while (WiFi.status() != WL_CONNECTED) { 
        delay(500); 
    }
    Serial.println("Wi-Fi connected successfully!");
    WiFi.mode(WIFI_STA); // vanlig wifi klient, trenger sikkert ikke denne linjen. 

    // MQTT library
    mqttClient.setCallback(callback);}

void loop() 
{
    if (!mqttClient.connected()) { 
        reconnect(); 
    }
    mqttClient.loop();
}
