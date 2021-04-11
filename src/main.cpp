#include <Arduino.h>
#define PJON_MAX_PACKETS 4
#define PJON_PACKET_MAX_LENGTH 52
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
#define PIN_POWER_LIGHT   A3
#define PIN_TRIGGER       11
#define PIN_LED           12
#define PIN_SCL           A5
#define PIN_SDA           A4

struct ColorValue {
  int R, G, B, C;
};
// RED		raw red 737 green 179 blue 202 c 1061
// GREEN	raw red 292 green 564 blue 352 c 1226
// BLUE	raw red 258 green 597 blue 733 c 1607
// PINK	raw red 1955 green 663 blue 871 c 3449
// CYAN	raw red 509 green 698 blue 763 c 1998
// YELLOW	raw red 1413 green 1143 blue 615 c 3264
// BLACK	raw red 101 green 89 blue 78 c 254
// WHITE	raw red 1182 green 1161 blue 959 c 3381

ColorValue white, red, green, blue, yellow, cyan, pink, black;

void makeColor(ColorValue *color, int r, int g, int b, int c) {
  (*color).R = r;
  (*color).G = g;
  (*color).B = b;
  (*color).C = c;
}

ColorValue *colors[8] = {&white, &red, &green, &blue, &yellow, &cyan, &pink, &black};
char colorNames[8] = {'W', 'R', 'G', 'B', 'Y', 'C', 'P', 'E'};

boolean activated = false;
Timer<1> colorTimer, waitTimer;
ColorValue* code[5];
int codeCount = 0;
int lastTrigger = HIGH;

Adafruit_TCS34725 colorReader = Adafruit_TCS34725(TCS34725_INTEGRATIONTIME_50MS, TCS34725_GAIN_1X);
PJON<SoftwareBitBang> bus(13);

void send(uint8_t *msg, uint8_t len) {
  bus.send(1, msg, len);
  while (bus.update()) {};//wait for send to be completed
}

void send(const char *msg, int len) {
  uint8_t buf[35];
  memcpy(buf, msg, len);
  send(buf, len);
}

void error_handler(uint8_t code, uint16_t data, void *custom_pointer) {
  if(code == PJON_CONNECTION_LOST) {
    Serial.print("Connection lost with device id ");
    Serial.println(bus.packets[data].content[0], DEC);
  }
}

void sendLcd(const char *line1, const char *line2) {
  uint8_t msg[35];
  msg[0] = 'L';
  strncpy((char *)&msg[1], line1, 17);
  strncpy((char *)&msg[18], line2, 17);
  Serial.print("Sending ");
  Serial.println((char *)msg);
  send(msg, 35);
}

void sendLcdImmediate(const char *line1, const char *line2) {
  uint8_t msg[35];
  msg[0] = 'Z';
  strncpy((char *)&msg[1], line1, 17);
  strncpy((char *)&msg[18], line2, 17);
  Serial.print("Sending ");
  Serial.println((char *)msg);
  send(msg, 35);
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
    data[6] = 0;
    sendLcd("Code", (char *)&data[1]);
  } else if (data[0] == 'W') {  //player has won

  } else if (data[0] == 'L') {  //player has lost

  }
}

void initComm() {
  bus.strategy.set_pin(PIN_COMM);
  bus.include_sender_info(false);
  bus.set_error(error_handler);
  bus.set_receiver(commReceive);
  bus.begin();
}

ColorValue *findClosestColor(int r, int g, int b, int c) {
  long totalDelta = 0;
  int closestIdx = 0;
  long minDelta = 100000;
  for (int i=0;i<8;i++) {
    totalDelta = abs((*colors[i]).R-r) + abs((*colors[i]).G-g) + abs((*colors[i]).B-b) + abs((*colors[i]).C - c);
//    Serial.println(totalDelta);
    if (totalDelta < minDelta) {
      minDelta = totalDelta;
      closestIdx = i;
    }
  }
  char line2[2];
  line2[1] = 0;
  line2[0] = colorNames[closestIdx];
  sendLcdImmediate("Color", line2);
//  Serial.println(colorNames[closestIdx]);
  return colors[closestIdx];
}

void checkColorPattern(int r, int g, int b, int c) {
  ColorValue *bestMatch = findClosestColor(r, g, b, c);
  
  if (bestMatch == code[codeCount]) {
    codeCount++;
    if (codeCount == 5) {
      sendMp3(TRACK_CONTROL_ROOM_ACCESS_GRANTED);
      activated = false;
      digitalWrite(PIN_POWER_LIGHT, LOW);
      send((uint8_t *)"D", 1);
      waitTimer.cancel();
    }
  }
}

bool doColorRead(void* t) {
  // float red, green, blue;
  // colorReader.getRGB(&red, &green, &blue);
  uint16_t red2, green2, blue2, c;
  long rt=0, gt=0, bt=0, ct=0;

  int loops = 3;
  for (int i=0;i<loops;i++) {
    colorReader.getRawData(&red2, &green2, &blue2, &c);
    rt += red2;
    gt += green2;
    bt += blue2;
    ct += c;
    delay(10);
  }
  red2 = rt / loops;
  green2 = gt / loops;
  blue2 = bt / loops;
  c = ct / loops;
  digitalWrite(PIN_LED, LOW);
  // Serial.print("raw red ");
  // Serial.print(red2);
  // Serial.print(" green ");
  // Serial.print(green2);
  // Serial.print(" blue ");
  // Serial.print(blue2);
  // Serial.print(" c ");
  // Serial.println(c);
  checkColorPattern((int)red2, (int)green2, (int)blue2, (int)c);
  return false;
}

bool resetProgress(void* t) {
  sendLcdImmediate("Controller", "Reset");
  codeCount = 0;
  return false;
}

void readColor() {
  waitTimer.cancel();
  digitalWrite(PIN_LED, HIGH);
  colorTimer.in(115, doColorRead);
  waitTimer.in(10000, resetProgress);
}

void initControlRoom() {
  makeColor(&white, 1365, 1349, 1311, 3931);
  makeColor(&red, 607, 154, 173, 874);
  makeColor(&green, 347, 689, 426, 1494);
  makeColor(&blue, 269, 648, 800, 1682);
  makeColor(&yellow, 1569, 1257, 880, 3600);
  makeColor(&cyan, 660, 917, 1018, 2651);
  makeColor(&pink, 1891, 614, 1020, 3276);
  makeColor(&black, 91, 79, 70, 217);
  randomSeed(analogRead(0));
  pinMode(PIN_POWER_LIGHT, OUTPUT);
  pinMode(PIN_LED, OUTPUT);
  digitalWrite(PIN_LED, LOW);
  digitalWrite(PIN_POWER_LIGHT, LOW);
  pinMode(PIN_TRIGGER, INPUT);
  colorReader.begin();
}

void startup() {
  delay(8000*2 + 1000);  //wait for modem and firewall
  digitalWrite(PIN_POWER_LIGHT, HIGH);
  sendLcd("Control", "Ready");
  delay(1000);
  digitalWrite(PIN_POWER_LIGHT, LOW);
}

void setup() {
  Serial.begin(9600);
  initControlRoom();
  delay(2000);
  initComm();

  startup();
}

void loop() {
  colorTimer.tick();
  waitTimer.tick();
  int trigger = digitalRead(PIN_TRIGGER);
  if (trigger != lastTrigger && activated) {
    lastTrigger = trigger;
    if (trigger == LOW) {
      readColor();
    }
  }
  bus.update();
  bus.receive(750);
}