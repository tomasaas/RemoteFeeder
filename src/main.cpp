#include <ESP32Servo.h>
#include "feed.h"
#include <WiFi.h>

Servo myservo;

const char* ssid = "Zyxel_8086";
const char* password = "REMOVED";

void setup() 
{
    Serial.begin(9600); // Default baud rate i pio device monitor -h

    myservo.attach(13, 1000, 2000);  // Attach to pin 18, min pulsbredde (1500 senturm vinkel), maks pulsbredde
    bool attached = myservo.attached();
    attached ? Serial.println("Attached successfully to myservo!") : Serial.println("Attachment to myservo failed");

    WiFi.begin(ssid, password);
    while (WiFi.status() != WL_CONNECTED) { 
        delay(500); 
    }
    Serial.println("Wi-Fi connected successfully!");
    WiFi.mode(WIFI_STA); // vanlig wifi klient, bare for ordens skyld. 


}

void loop() 
{
    // feed(myservo);


}

