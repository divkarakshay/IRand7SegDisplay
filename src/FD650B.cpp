#include "FD650B.h"

// Common-cathode 7-seg codes, active-high segments, DP = bit 7.
// (Any physical permutation of the segment wires still renders digits
// 0-9 and A-F correctly, so this table works regardless of PCB wiring.)
const uint8_t FD650B::SEG_0 = 0x3F;
const uint8_t FD650B::SEG_1 = 0x06;
const uint8_t FD650B::SEG_2 = 0x5B;
const uint8_t FD650B::SEG_3 = 0x4F;
const uint8_t FD650B::SEG_4 = 0x66;
const uint8_t FD650B::SEG_5 = 0x6D;
const uint8_t FD650B::SEG_6 = 0x7D;
const uint8_t FD650B::SEG_7 = 0x07;
const uint8_t FD650B::SEG_8 = 0x7F;
const uint8_t FD650B::SEG_9 = 0x6F;
const uint8_t FD650B::SEG_A = 0x77;
const uint8_t FD650B::SEG_B = 0x7C;
const uint8_t FD650B::SEG_C = 0x39;
const uint8_t FD650B::SEG_D = 0x5E;
const uint8_t FD650B::SEG_E = 0x79;
const uint8_t FD650B::SEG_F = 0x71;
const uint8_t FD650B::SEG_DASH = 0x40;
const uint8_t FD650B::SEG_BLANK = 0x00;

// Command bytes (FD650B datasheet, section 6)
#define FD650_CMD_MODE   0x48   // + settings byte (brightness<<4 | on/off)
#define FD650_CMD_DIG1   0x68   // digit addresses step by 2: 0x68,0x6A,0x6C,0x6E
#define FD650_CMD_KEYS   0x4F   // then read 1 byte

static const uint8_t DIG_ADDR[4] = {FD650_CMD_DIG1, 0x6A, 0x6C, 0x6E};

// ~100 kHz interface clock, per datasheet recommendation (max 4 MHz, and the
// app note suggests <=100 kHz for long-wire panels).
#define HALF_CLK_US 5

void FD650B::begin(uint8_t brightness) {
  pinMode(_clk, OUTPUT);
  digitalWrite(_clk, HIGH);
  pinMode(_dio, OUTPUT);
  digitalWrite(_dio, HIGH);
  setDisplay(true, brightness);
  for (uint8_t i = 0; i < 3; i++) setDigit(i, SEG_BLANK);
}

void FD650B::start() {
  // START: DIO falls while CLK is high (I2C-like)
  pinMode(_dio, OUTPUT);
  digitalWrite(_dio, HIGH);
  digitalWrite(_clk, HIGH);
  delayMicroseconds(HALF_CLK_US);
  digitalWrite(_dio, LOW);
  delayMicroseconds(HALF_CLK_US);
  digitalWrite(_clk, LOW);
}

void FD650B::stop() {
  // STOP: DIO rises while CLK is high
  pinMode(_dio, OUTPUT);
  digitalWrite(_dio, LOW);
  digitalWrite(_clk, HIGH);
  delayMicroseconds(HALF_CLK_US);
  digitalWrite(_dio, HIGH);
  delayMicroseconds(HALF_CLK_US);
}

bool FD650B::writeByte(uint8_t b) {
  pinMode(_dio, OUTPUT);
  for (uint8_t i = 0; i < 8; i++) {
    digitalWrite(_dio, (b & 0x80) ? HIGH : LOW);   // set data while CLK low
    digitalWrite(_clk, HIGH);                      // latch on rising edge
    delayMicroseconds(HALF_CLK_US);
    digitalWrite(_clk, LOW);
    delayMicroseconds(HALF_CLK_US);
    b <<= 1;
  }
  // 9th clock: chip pulls DIO low as ACK
  pinMode(_dio, INPUT);                            // high-Z, chip has 7k pull-up
  digitalWrite(_clk, HIGH);
  delayMicroseconds(HALF_CLK_US);
  bool ack = (digitalRead(_dio) == LOW);
  digitalWrite(_clk, LOW);
  delayMicroseconds(HALF_CLK_US);
  return ack;
}

uint8_t FD650B::readByte() {
  pinMode(_dio, INPUT);
  uint8_t v = 0;
  for (uint8_t i = 0; i < 8; i++) {
    v <<= 1;
    digitalWrite(_clk, LOW);                       // chip outputs on CLK falling edge
    delayMicroseconds(HALF_CLK_US);
    digitalWrite(_clk, HIGH);
    delayMicroseconds(HALF_CLK_US);
    if (digitalRead(_dio)) v |= 0x01;              // sample on rising edge
  }
  return v;
}

bool FD650B::setDisplay(bool on, uint8_t brightness) {
  uint8_t level = (brightness > 7) ? 7 : brightness;
  uint8_t duty = ((7 - level) & 0x07) << 4;        // 0x00 = 8/8 duty (brightest)
  start();
  bool ack1 = writeByte(FD650_CMD_MODE);
  bool ack2 = writeByte(duty | (on ? 0x01 : 0x00));
  stop();
  return ack1 && ack2;
}

bool FD650B::ping() { return setDisplay(true, 4); }

void FD650B::setDigit(uint8_t index, uint8_t segs) {
  _scrolling = false;              // any explicit write takes over the display
  rawSetDigit(index, segs);
}

void FD650B::rawSetDigit(uint8_t index, uint8_t segs) {
  if (index > 3) return;
  start();
  writeByte(DIG_ADDR[index]);
  writeByte(segs);
  stop();
}

// Column decomposition for smooth scrolling. Each 7-seg cell is 3 columns:
//   L = left verticals  {F,E}          -> bit5|bit4 = 0x30
//   M = horizontals     {A,G,D} (+DP)  -> bit0|bit6|bit3|bit7 = 0xC9
//   R = right verticals {B,C}          -> bit1|bit2 = 0x06
// A window of 9 columns (= 3 cells) slides over the canvas one column per
// frame, so characters enter/leave the display at 1/3-char granularity.
#define COL_L 0x30
#define COL_M 0xC9
#define COL_R 0x06
#define SCROLL_PAD 3                 // one blank cell of lead-in/lead-out

void FD650B::scrollText(const char* text, uint32_t stepMs) {
  if (text == nullptr || text[0] == 0) return;
  _scrollStepMs = (stepMs < 10) ? 10 : stepMs;
  size_t len = strlen(text);
  if (len <= 3) {                    // fits the display: no scrolling needed
    _scrolling = false;
    showText(text);
    return;
  }
  if (len > 28) len = 28;            // canvas cap (3*28 + 6 = 90)
  _scrolling = false;
  int n = 0;
  for (int i = 0; i < SCROLL_PAD; i++) _canvas[n++] = 0;
  for (size_t i = 0; i < len; i++) {
    uint8_t s = charToSeg(text[i]);
    _canvas[n++] = s & COL_L;
    _canvas[n++] = s & COL_M;
    _canvas[n++] = s & COL_R;
  }
  for (int i = 0; i < SCROLL_PAD; i++) _canvas[n++] = 0;
  _canvasLen = n;
  _scrollPos = -9;                   // start fully off-screen (blank frame first)
  _scrollNext = millis() + _scrollStepMs;
  _scrolling = true;
}

bool FD650B::scrollTick() {
  if (!_scrolling) return false;
  if ((int32_t)(millis() - _scrollNext) < 0) return true;   // not due yet
  _scrollNext = millis() + _scrollStepMs;
  _scrollPos++;
  if (_scrollPos > _canvasLen - SCROLL_PAD) {   // text fully scrolled out
    _scrolling = false;
    return false;
  }
  for (uint8_t i = 0; i < 3; i++) {
    uint8_t seg = 0;
    for (uint8_t k = 0; k < 3; k++) {
      int c = _scrollPos + i * 3 + k;
      if (c >= 0 && c < _canvasLen) seg |= _canvas[c];
    }
    rawSetDigit(i, seg);
  }
  return true;
}

void FD650B::showNumber(uint16_t value, uint8_t dpIndex) {
  if (value > 999) value %= 1000;
  uint8_t d[3] = {(uint8_t)(value / 100), (uint8_t)((value / 10) % 10), (uint8_t)(value % 10)};
  for (uint8_t i = 0; i < 3; i++) {
    uint8_t seg = segCode(d[i]);
    if (i == dpIndex) seg |= 0x80;                // decimal point
    setDigit(i, seg);
  }
}

void FD650B::showHex(uint16_t value) {
  uint8_t d[3] = {(uint8_t)((value >> 8) & 0x0F), (uint8_t)((value >> 4) & 0x0F), (uint8_t)(value & 0x0F)};
  if ((value & 0xFFF) < 0x100) d[0] = 0xFF;       // blank leading zeroes
  if ((value & 0xFFF) < 0x010) d[1] = 0xFF;
  for (uint8_t i = 0; i < 3; i++) {
    uint8_t n = d[i];
    uint8_t seg = (n == 0xFF) ? SEG_BLANK
                              : ((uint8_t[]){SEG_0, SEG_1, SEG_2, SEG_3, SEG_4, SEG_5, SEG_6,
                                             SEG_7, SEG_8, SEG_9, SEG_A, SEG_B, SEG_C, SEG_D,
                                             SEG_E, SEG_F})[n & 0x0F];
    setDigit(i, seg);
  }
}

uint8_t FD650B::segCode(uint8_t n) {
  static const uint8_t segs[10] = {SEG_0, SEG_1, SEG_2, SEG_3, SEG_4,
                                   SEG_5, SEG_6, SEG_7, SEG_8, SEG_9};
  return (n < 10) ? segs[n] : SEG_BLANK;
}

void FD650B::showText(const char* text) {
  for (uint8_t i = 0; i < 3; i++) {
    char c = text[i];
    setDigit(i, charToSeg(c));
    if (c == 0) break;
  }
}

uint8_t FD650B::charToSeg(char c) {
  if (c == 0) return SEG_BLANK;
  c = toupper(c);
  if (c >= '0' && c <= '9') return segCode(c - '0');
  switch (c) {
    case 'A': return SEG_A;
    case 'B': return SEG_B;
    case 'C': return SEG_C;
    case 'D': return SEG_D;
    case 'E': return SEG_E;
    case 'F': return SEG_F;
    case 'H': return 0x76;
    case 'L': return 0x38;
    case 'N': return 0x54;
    case 'O': return SEG_0;
    case 'P': return 0x73;
    case 'S': return SEG_5;
    case 'T': return 0x78;
    case 'U': return 0x3E;
    case 'Y': return 0x6E;
    case '-': return SEG_DASH;
    default:  return SEG_BLANK;
  }
}

uint8_t FD650B::readKeyRaw() {
  start();
  writeByte(FD650_CMD_KEYS);
  uint8_t k = readByte();
  stop();
  return k;
}
