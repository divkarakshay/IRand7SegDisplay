#pragma once

#include <Arduino.h>
#include "FD650B.h"

// Non-blocking visual-effects engine for the 3-digit FD650B display.
// Call tick() from loop(); next()/prev()/select() pick an effect.
// An effect runs until stop() is called (or another effect is selected).
class Effects {
 public:
  enum {
    FX_RIDER = 0,   // single segment-bar sweeping left/right in 1/3-char steps
    FX_BOUNCE,      // "8." bouncing between the three digits
    FX_SNAKE,       // segments light up one by one, clockwise per digit
    FX_BINARY,      // all 21 segments count in binary
    FX_COUNT,       // rolling 000..999 counter with a moving decimal point
    FX_BLINK,       // "8.8.8." blinking
    FX_PULSE,       // brightness breathing ("8.8.8.")
    FX_TEXT,        // smoothly scrolls a demo text (uses FD650B::scrollText)
    FX_COUNT_N
  };

  Effects(FD650B& disp) : _d(disp) {}

  void begin();                        // start with the first effect
  void next();                         // next effect (wraps)
  void prev();                         // previous effect (wraps)
  void select(int index);              // start a specific effect (wraps)
  void stop();                         // leave effects mode, display on
  void tick();                         // advance current effect (call in loop)
  bool running() const { return _running; }
  int index() const { return _index; }
  void setBrightness(uint8_t b) { _bright = (b > 7) ? 7 : b; }
  static const char* name(int index);

 private:
  FD650B& _d;
  bool _running = false;
  int _index = 0;
  uint8_t _bright = 4;                 // mirrors main's brightness (0=bright)
  int16_t _pos = 0;
  int8_t _dir = 1;
  uint32_t _val = 0;
  uint32_t _next = 0;

  bool due(uint32_t ms);
  void stepRider();
  void stepBounce();
  void stepSnake();
  void stepBinary();
  void stepCount();
  void stepBlink();
  void stepPulse();
  void stepText();
};
