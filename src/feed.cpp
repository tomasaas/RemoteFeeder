#include <ESP32Servo.h>
#include "feed.h"

void feed(Servo& servo) {
    // sender inn myservo objektet som "servo" her
    int speedAbs = 30; 
    int CCW = 90 + speedAbs; 
    int CW = 90 - speedAbs; 
    int stop = 90;
    int feedTime = 5000;     

    servo.write(CW);
    delay(800);
    servo.write(stop);

    delay(feedTime);
    
    servo.write(CCW);
    delay(800); 
    servo.write(stop);
 }