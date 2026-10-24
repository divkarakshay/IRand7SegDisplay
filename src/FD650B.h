#pragma once

#include <Arduino.h>

// Driver for the FD650B-S "LED display driver + keypad scanner" (SOP16).
// 2-wire bit-banged interface, NOT standard I2C:
//   CLK : master output, push-pull. Data latched on CLK rising edge.
//   DIO : bidirectional, open-drain inside the chip with ~7k internal pull-up.
//         ESP32 must switch this pin between OUTPUT (master writes) and
//         INPUT/high-Z (chip ACK, chip key data).
// Commands (byte, MSB first):
//   0x48 + settings   -> mode/settings. settings = brightness<<4 | displayOn
//                        (brightness 0x0 = 8/8 duty = brightest, 0x1..0x7 dimmer)
//   0x68,0x6A,0x6C,0x6E -> display data for digit 1..4 (one data byte each)
//   0x4F              -> read 1 key byte
// Key byte: bit7=0, bit6=1 when pressed, bits5..3 = row (KIn), bit2=1,
//           bits1..0 = column (DIGn). Idle/no key = 0x2E.
// VDD 2.7..5.5 V. Recommended clock ~100 kHz. Max 4 MHz.
class FD650B {
 public:
  FD650B(uint8_t dioPin, uint8_t clkPin) : _dio(dioPin), _clk(clkPin) {}

  void begin(uint8_t brightness = 4);   // 0 (dim) .. 7 (bright)
  bool setDisplay(bool on, uint8_t brightness);  // returns false if chip not answering

  // segs: bit7=DP, bit6=G, bit5=F, bit4=E, bit3=D, bit2=C, bit1=B, bit0=A
  void setDigit(uint8_t index, uint8_t segs);   // index 0..3 (leftmost=0)
  void showNumber(uint16_t value, uint8_t dpIndex = 0xFF);  // 0..999
  void showHex(uint16_t value);                // last 3 hex digits
  void showText(const char* text);             // up to 3 chars

  // Smooth horizontal scrolling for text longer than 3 chars. Each 7-seg
  // cell is treated as 3 columns (left verticals / horizontals / right
  // verticals), so the window slides in 1/3-character steps. Non-blocking:
  // call scrollTick() from loop() until isScrolling() is false. Text up to
  // 3 chars is shown statically instead. Any setDigit/show* stops it.
  void scrollText(const char* text, uint32_t stepMs = 40);
  bool scrollTick();        // advance scroll frame when due; true while scrolling
  bool isScrolling() const { return _scrolling; }

  uint8_t readKeyRaw();    // 0x2E = no key; pressed codes 0x44..0x7F
  bool ping();             // true if chip ACKs both bytes of the mode command

  static const uint8_t SEG_0, SEG_1, SEG_2, SEG_3, SEG_4, SEG_5, SEG_6,
      SEG_7, SEG_8, SEG_9, SEG_A, SEG_B, SEG_C, SEG_D, SEG_E, SEG_F,
      SEG_DASH, SEG_BLANK;

 private:
  void start();
  void stop();
  bool writeByte(uint8_t b);   // returns chip ACK (true = line pulled low)
  uint8_t readByte();
  void rawSetDigit(uint8_t index, uint8_t segs);  // like setDigit but keeps scrolling
  static uint8_t segCode(uint8_t n);
  static uint8_t charToSeg(char c);

  uint8_t _dio, _clk;
  bool _scrolling = false;
  uint8_t _canvas[90];         // scroll canvas: 3 columns per char + padding
  int _canvasLen = 0;
  int _scrollPos = 0;
  uint32_t _scrollNext = 0;
  uint32_t _scrollStepMs = 40;
};
