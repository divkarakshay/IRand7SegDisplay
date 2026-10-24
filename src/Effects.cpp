#include "Effects.h"

static const char* const FX_NAMES[Effects::FX_COUNT_N] = {
    "RIDER", "BOUNCE", "SNAKE", "BINARY", "COUNT", "BLINK", "PULSE", "TEXT"};

// One "column" of a lit 7-seg cell (see FD650B.cpp scroll constants):
// L = left verticals {F,E}, M = horizontals {A,G,D}, R = right verticals {B,C}
static const uint8_t COL_MASK[3] = {0x30, 0x49, 0x06};

const char* Effects::name(int index) {
  if (index < 0 || index >= FX_COUNT_N) return "?";
  return FX_NAMES[index];
}

bool Effects::due(uint32_t ms) { return (int32_t)(millis() - _next) >= 0; }

void Effects::select(int index) {
  index %= FX_COUNT_N;
  if (index < 0) index += FX_COUNT_N;
  _index = index;
  _running = true;
  _pos = 0;
  _dir = 1;
  _val = 0;
  _next = millis();                  // first step renders immediately
  _d.setDisplay(true, _bright);
}

void Effects::begin() { select(0); }
void Effects::next() { select(_index + 1); }
void Effects::prev() { select(_index - 1); }

void Effects::stop() {
  _running = false;
  _d.setDisplay(true, _bright);      // effects may have left the display off
}

void Effects::tick() {
  if (!_running) return;
  switch (_index) {
    case FX_RIDER:  stepRider();  break;
    case FX_BOUNCE: stepBounce(); break;
    case FX_SNAKE:  stepSnake();  break;
    case FX_BINARY: stepBinary(); break;
    case FX_COUNT:  stepCount();  break;
    case FX_BLINK:  stepBlink();  break;
    case FX_PULSE:  stepPulse();  break;
    case FX_TEXT:   stepText();   break;
  }
}

void Effects::stepRider() {
  if (!due(45)) return;
  _next = millis() + 45;
  _d.setDigit(0, 0);
  _d.setDigit(1, 0);
  _d.setDigit(2, 0);
  _pos += _dir;
  if (_pos > 8) { _pos = 7; _dir = -1; }   // 9 columns = 3 digits, bounce
  if (_pos < 0) { _pos = 1; _dir = 1; }
  _d.setDigit(_pos / 3, COL_MASK[_pos % 3]);
}

void Effects::stepBounce() {
  if (!due(280)) return;
  _next = millis() + 280;
  _pos += _dir;
  if (_pos > 2) { _pos = 1; _dir = -1; }
  if (_pos < 0) { _pos = 1; _dir = 1; }
  for (int i = 0; i < 3; i++) _d.setDigit(i, 0);
  _d.setDigit(_pos, FD650B::SEG_8 | 0x80);   // "8." ball
}

void Effects::stepSnake() {
  if (!due(40)) return;
  _next = millis() + 40;
  // 21 segments lit one by one: per digit clockwise A,B,C,D,E,F,G (bit0..6)
  for (int d = 0; d < 3; d++) {
    int lit = _val - 7 * d;
    uint8_t m = 0;
    if (lit >= 7) m = 0x7F;
    else if (lit > 0) m = (uint8_t)((1u << lit) - 1);
    _d.setDigit(d, m);
  }
  _val = (_val + 1) % 21;
}

void Effects::stepBinary() {
  if (!due(60)) return;
  _next = millis() + 60;
  for (int d = 0; d < 3; d++)
    _d.setDigit(d, (uint8_t)((_val >> (7 * d)) & 0x7F));
  _val = (_val + 1) & 0x1FFFFF;            // 21 bits
}

void Effects::stepCount() {
  if (!due(30)) return;
  _next = millis() + 30;
  _d.showNumber((uint16_t)(_val % 1000), (uint8_t)((_val / 7) % 3));
  _val++;
}

void Effects::stepBlink() {
  if (!due(250)) return;
  _next = millis() + 250;
  _val = !_val;
  if (_val) {
    for (int i = 0; i < 3; i++) _d.setDigit(i, 0xFF);   // "8.8.8."
    _d.setDisplay(true, _bright);
  } else {
    _d.setDisplay(false, _bright);
  }
}

void Effects::stepPulse() {
  if (!due(90)) return;
  _next = millis() + 90;
  for (int i = 0; i < 3; i++) _d.setDigit(i, 0xFF);
  _pos += _dir;
  if (_pos > 14) { _pos = 13; _dir = -1; }
  if (_pos < 0) { _pos = 1; _dir = 1; }
  uint8_t lvl = (_pos <= 7) ? (uint8_t)_pos : (uint8_t)(14 - _pos);  // 0..7..0
  _d.setDisplay(true, lvl);              // 0 = brightest -> breath
}

void Effects::stepText() {
  if (_d.isScrolling()) return;
  if (!due(400)) return;                 // small pause, then re-scroll
  _next = millis() + 400;
  _d.setDisplay(true, _bright);
  _d.scrollText("A23 HELLO 0123456789", 40);
}
