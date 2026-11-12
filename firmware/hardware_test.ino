// HARDWARE TEST - run this BEFORE the main firmware
// Checks the laser, then both servos, in a loop. Open Serial Monitor at 115200 to see the stage.
// Library needed: ESP32Servo. Board: "ESP32 Dev Module"

#include <ESP32Servo.h>

#define PAN_PIN   18
#define TILT_PIN  19
#define LASER_PIN 23

Servo panServo, tiltServo;

void sweep(Servo &s, int from, int to) {
  int stepDir = (to > from) ? 1 : -1;
  for (int a = from; a != to; a += stepDir) { s.write(a); delay(15); }
  s.write(to);
}

void setup() {
  Serial.begin(115200);
  pinMode(LASER_PIN, OUTPUT);
  digitalWrite(LASER_PIN, LOW);

  panServo.setPeriodHertz(50);
  tiltServo.setPeriodHertz(50);
  panServo.attach(PAN_PIN, 500, 2400);
  tiltServo.attach(TILT_PIN, 500, 2400);
  panServo.write(90);
  tiltServo.write(90);
  delay(1500);
}

void loop() {
  Serial.println("1/4 Laser: blink 3 times");
  for (int i = 0; i < 3; i++) {
    digitalWrite(LASER_PIN, HIGH); delay(400);
    digitalWrite(LASER_PIN, LOW);  delay(400);
  }

  Serial.println("2/4 Pan servo: 60 -> 120 -> 90");
  sweep(panServo, 90, 60);
  sweep(panServo, 60, 120);
  sweep(panServo, 120, 90);
  delay(500);

  Serial.println("3/4 Tilt servo: 70 -> 110 -> 90");
  sweep(tiltServo, 90, 70);
  sweep(tiltServo, 70, 110);
  sweep(tiltServo, 110, 90);
  delay(500);

  Serial.println("4/4 Laser on while both servos center");
  digitalWrite(LASER_PIN, HIGH);
  delay(1500);
  digitalWrite(LASER_PIN, LOW);
  delay(1000);
}
