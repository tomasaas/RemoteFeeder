#include <ESP32Servo.h>
#include "feed.h"

Servo myservo;

void setup() 
{
    Serial.begin(9600); // Default baud rate i pio device monitor -h

    myservo.attach(13, 1000, 2000);  // Attach to pin 18, min pulsbredde (1500 senturm vinkel), maks pulsbredde
    bool attached = myservo.attached();
    attached ? Serial.println("Attached successfully to myservo!") : Serial.println("Attachment to myservo failed");
}

void loop() 
{
    feed(myservo);
}

