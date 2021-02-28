#include <Arduino.h>
#define PJON_MAX_PACKETS 4
#define PJON_PACKET_MAX_LENGTH 33
#include <PJONSoftwareBitBang.h>
#include <arduino-timer.h>
#include <Adafruit_TCS34725.h>
#include <NeoPixelBrightnessBus.h>
#include "../../Escape Room v2 Master/src/tracks.h"

/* Connect SCL    to analog 5
   Connect SDA    to analog 4
   Connect VDD    to 3.3V DC
   Connect GROUND to common ground */
#define PIN_COMM          13
#define PIN_POWER_LIGHT   2
#define PIN_TRIGGER       3
#define PIN_SCL           A5
#define PIN_SDA           A4

RgbColor white(255, 255, 255);
RgbColor red(255, 0, 0);
RgbColor green(0, 255, 0);
RgbColor blue(0, 0, 255);
RgbColor yellow(255, 255, 0);
RgbColor cyan(0, 255, 255);
RgbColor pink(255, 0, 255);
RgbColor black(0, 0, 0);

RgbColor colors[8] = {white, red, green, blue, yellow, cyan, pink, black};
char colorNames[8] = {'W', 'R', 'G', 'B', 'Y', 'C', 'P', 'E'};

boolean activated = false;
Timer<1> colorTimer, waitTimer;
RgbColor code[5];
int codeCount = 0;
int lastTrigger = HIGH;

Adafruit_TCS34725 colorReader = Adafruit_TCS34725(TCS34725_INTEGRATIONTIME_101MS, TCS34725_GAIN_4X);
PJON<SoftwareBitBang> bus(12);

void send(uint8_t *msg, uint8_t len) {
  bus.send(1, msg, len);
  bus.update();
}

void error_handler(uint8_t code, uint16_t data, void *custom_pointer) {
  if(code == PJON_CONNECTION_LOST) {
    Serial.print("Connection lost with device id ");
    Serial.println(bus.packets[data].content[0], DEC);
  }
}

void commReceive(uint8_t *data, uint16_t len, const PJON_Packet_Info &info) {
  if (data[0] == 'A') {
    activated = true;
    digitalWrite(PIN_POWER_LIGHT, HIGH);
  } else if (data[0] == 'C') { //code
    for (int i = 1;i<6;i++) {
      for (int c=0;c<8;c++) {
        if (colorNames[c] == data[i]) {
          code[i-1] = colors[c];
        }
      }
    }
  } else if (data[0] == 'W') {  //player has won

  } else if (data[0] == 'L') {  //player has lost

  }
}

void sendLcd(char *line1, char *line2) {
  uint8_t msg[33];
  msg[0] = 'L';
  strncpy((char *)&msg[1], line1, 16);
  strncpy((char *)&msg[17], line2, 16);
  send(msg, 33);
}

void sendMp3(int track) {
  uint8_t msg[2];
  msg[0] = 'M';
  msg[1] = track;
  send(msg, 2);
}

void sendTone(int tone) {
  uint8_t msg[2];
  msg[0] = 'T';
  msg[1] = tone;
  send(msg, 2);
}

void initComm() {
  bus.strategy.set_pin(PIN_COMM);
  bus.include_sender_info(false);
  bus.set_error(error_handler);
  bus.set_receiver(commReceive);
  bus.begin();
}

RgbColor findClosestColor(int red, int green, int blue) {
  int totalDelta = 0;
  int closestIdx = 0;
  int minDelta = 1000;
  for (int i=0;i<8;i++) {
    totalDelta += abs(colors[i].R-red) + abs(colors[i].G-green) + abs(colors[i].B-blue);
    if (totalDelta < minDelta) {
      minDelta = totalDelta;
      closestIdx = i;
    }
  }
  return colors[closestIdx];
}

void checkColorPattern(int red, int green, int blue) {
  RgbColor bestMatch = findClosestColor(red, green, blue);
  if (bestMatch == code[codeCount]) {
    codeCount++;
    if (codeCount == 5) {
      activated = false;
      digitalWrite(PIN_POWER_LIGHT, LOW);
      send((uint8_t *)"D", 1);
      waitTimer.cancel();
    }
  }
}

bool doColorRead(void* t) {
  float red, green, blue;
  colorReader.getRGB(&red, &green, &blue);
  colorReader.setInterrupt(true);  // turn off LED
  //check color against pattern
  checkColorPattern((int)red, (int)green, (int)blue);
  return false;
}

bool resetProgress(void* t) {
  codeCount = 0;
  return false;
}

void readColor() {
  waitTimer.cancel();
  colorReader.setInterrupt(false);  // turn on LED
  colorTimer.in(115, doColorRead);
  waitTimer.in(10000, resetProgress);
}

void initControlRoom() {
  colorReader.begin();
}

void setup() {
  pinMode(PIN_POWER_LIGHT, OUTPUT);
  digitalWrite(PIN_POWER_LIGHT, LOW);
  pinMode(PIN_TRIGGER, INPUT);
  delay(1000);
  initComm();
}

void loop() {
  colorTimer.tick();
  int trigger = digitalRead(PIN_TRIGGER);
  if (trigger != lastTrigger) {
    lastTrigger = trigger;
    if (trigger == LOW) {
      readColor();
    }
  }
  bus.update();
  bus.receive(750);
}