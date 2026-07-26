#include "config.h"
#include "servo_ctrl.h"
#include "esp_arduino_version.h"

static const uint32_t SERVO_MIN_US = 500;
static const uint32_t SERVO_MAX_US = 2400;
static const uint8_t  SERVO_LEDC_CH = 0;
static int  servoAngle = 90;
static bool servoAttached = false;

static void servoWriteUs(uint32_t us) {
  uint32_t duty = (uint64_t)us * 65535 / 20000;
#if ESP_ARDUINO_VERSION_MAJOR >= 3
  ledcWrite(PIN_SERVO, duty);
#else
  ledcWrite(SERVO_LEDC_CH, duty);
#endif
}

void servoSetAngle(int angle) {
  angle = constrain(angle, 0, 180);
  servoAngle = angle;
  servoAttached = true;
  servoWriteUs(SERVO_MIN_US + (uint32_t)angle * (SERVO_MAX_US - SERVO_MIN_US) / 180);
  Serial.printf("[SERVO] angle=%d\n", angle);
}

void servoRelease() {
  servoAttached = false;
  servoWriteUs(0);
  Serial.println("[SERVO] released");
}

void servoInit() {
#if ESP_ARDUINO_VERSION_MAJOR >= 3
  ledcAttach(PIN_SERVO, 50, 16);
#else
  ledcSetup(SERVO_LEDC_CH, 50, 16);
  ledcAttachPin(PIN_SERVO, SERVO_LEDC_CH);
#endif
  servoSetAngle(90);
  Serial.println("[SERVO] MG90S on GPIO4 ready (50Hz LEDC)");
}

int  servoGetAngle() { return servoAngle; }
bool servoIsAttached() { return servoAttached; }
