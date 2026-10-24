// A23 front-panel repurpose: 3-digit display + 6 keys + IR RECORD/REPLAY on ESP32
//
// Wiring (panel 5-pin connector labelled  VDD IR GND DIO CLK ):
//   VDD  -> ESP32 3V3     (FD650B: 2.7-5.5 V; IR receiver: 2.5-5.5 V typical)
//   GND  -> ESP32 GND
//   IR   -> ESP32 GPIO27  (IR receiver demodulated output)
//   DIO  -> ESP32 GPIO21  (FD650B DAT, bidirectional)
//   CLK  -> ESP32 GPIO22  (FD650B CLK)
//
// IR TX (the panel has no IR LED - add one to replay):
//   GPIO26 -> 1k resistor -> NPN (BC547/2N2222) base; collector -> IR LED
//   (940nm) cathode; IR LED anode -> 3V3; emitter -> GND. See README.
//
// This firmware does NOT store recordings - the PC does:
//   tools/ir_recorder.py maintains ir_recordings.json in the project dir.
//   - Record: the script listens to this serial output:
//       IR: type=3 val=0x00CF8976 bits=32
//   - Replay: the script sends:  SEND type=3 val=0x00CF8976 bits=32
//
// Panel buttons (hardware is verified): MENU toggles display (or leaves
// effects mode), OK shows idle, CH-/+ adjust brightness, VOL-/+ cycle the
// visual-effects mode (RIDER BOUNCE SNAKE BINARY COUNT BLINK PULSE TEXT).
// Serial extras: FX (list), FX <n> (run effect), TEXT <message> (scroll).

#include <Arduino.h>
#include <IRrecv.h>
#include <IRsend.h>
#include <IRutils.h>
#include "FD650B.h"
#include "Effects.h"

#define PIN_DIO     21
#define PIN_CLK     22
#define PIN_IR      27
#define PIN_IR_TX   26

FD650B disp(PIN_DIO, PIN_CLK);
Effects fx(disp);
IRrecv irrecv(PIN_IR);
IRsend irsend(PIN_IR_TX);
decode_results ir;

static const uint8_t KEY_RAW[6] = {0x47, 0x4F, 0x57, 0x5F, 0x67, 0x6F};
static const char* KEY_NAMES[6] = {"MENU", "OK", "CH-", "CH+", "VOL-", "VOL+"};

static uint8_t lastKey = 0x2E;      // FD650B idle code = no key
static uint8_t brightness = 4;
static bool dispOn = true;
static uint32_t sndUntil = 0;

// ---- keys -------------------------------------------------------------------

static int keyIndex(uint8_t code) {
  for (int i = 0; i < 6; i++)
    if (KEY_RAW[i] == code) return i;
  return -1;
}

static void handleKey(uint8_t code) {
  if (code == lastKey) return;
  bool pressed = code & 0x40;
  lastKey = code;
  if (!pressed) return;

  int idx = keyIndex(code);
  if (idx < 0) {
    Serial.printf("Key! raw=0x%02X row=%u col=%u UNMAPPED\n", code,
                  (code >> 3) & 7, code & 3);
    return;
  }
  Serial.printf("Key! %s\n", KEY_NAMES[idx]);
  switch (idx) {
    case 0:                                                       // MENU
      if (fx.running()) { fx.stop(); disp.showText("rdy"); }
      else { dispOn = !dispOn; disp.setDisplay(dispOn, brightness); }
      break;
    case 1:  fx.stop(); disp.showText("rdy"); break;              // OK
    case 2:  if (brightness > 0) brightness--;
             fx.setBrightness(brightness);
             disp.setDisplay(true, brightness); break;            // CH-
    case 3:  if (brightness < 7) brightness++;
             fx.setBrightness(brightness);
             disp.setDisplay(true, brightness); break;            // CH+
    case 4:  fx.prev();                                            // VOL-
    case 5:  fx.next();                                            // VOL+
      Serial.printf("FX %d/%d %s\n", fx.index() + 1, Effects::FX_COUNT_N,
                    Effects::name(fx.index()));
      break;
  }
}

// ---- serial commands (from tools/ir_recorder.py) ----------------------------

static void runCommand(String cmd) {
  if (cmd.startsWith("SEND")) {
    int type = -1, bits = -1;
    unsigned long long val = 0;
    int p;
    if ((p = cmd.indexOf("type=")) >= 0) type = cmd.substring(p + 5).toInt();
    if ((p = cmd.indexOf("val=")) >= 0)  val  = strtoull(cmd.substring(p + 4).c_str(), nullptr, 0);
    if ((p = cmd.indexOf("bits=")) >= 0) bits = cmd.substring(p + 5).toInt();
    if (type >= 0 && bits > 0) {
      Serial.printf("Sending: type=%d val=0x%llX bits=%d\n", type, val, bits);
      irsend.send((decode_type_t)type, val, bits);
      Serial.println("SEND OK");
      disp.showText("SND");
      sndUntil = millis() + 800;
    } else {
      Serial.println("SEND ERR");
    }
  } else if (cmd == "HELP") {
    Serial.println(F("Commands: SEND type=<n> val=<0xhex> bits=<n> | "
                     "FX | FX <0..7> | TEXT <message>"));
  } else if (cmd.startsWith("FX")) {
    String rest = cmd.substring(2);
    rest.trim();
    if (!rest.length()) {
      Serial.printf("FX: now %d (%s)\n", fx.index(), Effects::name(fx.index()));
      for (int i = 0; i < Effects::FX_COUNT_N; i++)
        Serial.printf("  %d %s\n", i, Effects::name(i));
    } else {
      fx.select(rest.toInt());
      Serial.printf("FX: %d (%s)\n", fx.index(), Effects::name(fx.index()));
    }
  } else if (cmd.startsWith("TEXT")) {
    String t = cmd.substring(4);
    t.trim();
    if (t.length()) {
      fx.stop();
      disp.scrollText(t.c_str());
      Serial.printf("TEXT: '%s'\n", t.c_str());
    } else {
      Serial.println("TEXT ERR (usage: TEXT <message>)");
    }
  } else {
    Serial.printf("Unknown: '%s' (HELP)\n", cmd.c_str());
  }
}

static void handleSerial() {
  static String buf;
  while (Serial.available()) {
    char c = Serial.read();
    if (c == '\n') {
      buf.trim();
      if (buf.length()) runCommand(buf);
      buf = "";
    } else {
      buf += c;
    }
  }
}

// ---- IR handling ------------------------------------------------------------

static void handleIR() {
  if (!irrecv.decode(&ir)) return;
  if (ir.decode_type != UNKNOWN) {
    Serial.printf("IR: type=%u val=0x%08X bits=%u", (uint8_t)ir.decode_type,
                  (uint32_t)(ir.value & 0xFFFFFFFF), ir.bits);
    if (ir.repeat) Serial.print(" (rep)");
    Serial.println();
    if (!ir.repeat) {
      fx.stop();
      disp.showHex(ir.value);
      sndUntil = millis() + 800;
    }
  }
  irrecv.resume();
}

// ---- main -------------------------------------------------------------------

void setup() {
  Serial.begin(115200);
  delay(200);
  Serial.println(F("=== A23 front panel on ESP32: IR record/replay ==="));
  Serial.printf("FD650B: DIO=%d CLK=%d | IR RX=GPIO%d | IR TX=GPIO%d\n",
                PIN_DIO, PIN_CLK, PIN_IR, PIN_IR_TX);
  Serial.println(F("Record: watch serial 'IR:' lines. Replay: 'SEND type=.. val=.. bits=..'"));

  disp.begin(brightness);
  if (!disp.ping())
    Serial.println(F("WARNING: FD650B did not ACK - check DIO/CLK/VDD/GND wiring"));
  else
    Serial.println(F("FD650B alive (ACK ok)"));

  irrecv.enableIRIn();
  irsend.begin();
  disp.showText("ESP");
  delay(1200);
  disp.showText("rdy");
}

void loop() {
  uint8_t k = disp.readKeyRaw();
  handleKey(k);
  handleIR();
  handleSerial();
  fx.tick();

  if (sndUntil && millis() > sndUntil) {
    sndUntil = 0;
    disp.showText("rdy");
  }

  if (!dispOn && !fx.running()) disp.setDisplay(false, brightness);
  delay(20);
}