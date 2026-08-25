#pragma once
#include <ESP32Servo.h>

void feed(Servo& myservo, bool& feedRequested, bool& feeding, int& feedStep, unsigned long& stepStarted);