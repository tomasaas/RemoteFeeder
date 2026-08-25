#include <ESP32Servo.h>
#include "feed.h"

void feed(Servo& myservo, bool& feedRequested, bool& feeding, int& feedStep, unsigned long& stepStarted) {
    // sender inn myservo objektet som "servo" her
    
    // Movement parameters
    int speedAbs = 30; 
    int CCW = 90 + speedAbs; 
    int CW = 90 - speedAbs; 
    int stop = 90;
    int feedTime = 5000; 
    int moveTime = 700;

    

    // Logic
    unsigned long now = millis(); 

    if (feedRequested) {
        feedRequested = false; 
        if (!feeding) {
            feeding = true; 
            feedStep = 1;
            stepStarted = now; 
            myservo.write(CW);
        }
    }

    if (feedStep == 1 && now - stepStarted > moveTime) {
        myservo.write(stop);
        feedStep = 2; 
        stepStarted = now; 
    }
    
    if (feedStep == 2 && now - stepStarted > feedTime) {
        myservo.write(CCW);
        feedStep = 3;
        stepStarted = now; 
    }

    if (feedStep == 3 && now - stepStarted > moveTime) {
        myservo.write(stop);
        feedStep = 0; 
        feeding = false; 
    }
 }
 
    // servo.write(CW);
    // delay(800);
    // servo.write(stop);

    // delay(feedTime);
    
    // servo.write(CCW);
    // delay(800); 
    // servo.write(stop);