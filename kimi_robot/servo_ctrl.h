#pragma once

#include <Arduino.h>

void servoInit();
void servoSetAngle(int angle);
void servoRelease();
int  servoGetAngle();
bool servoIsAttached();
