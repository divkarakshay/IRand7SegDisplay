#!/usr/bin/env python3
"""Record and replay IR signals for the A23 front-panel ESP32.

Recordings are stored ONLY in this project directory (ir_recordings.json).
The ESP32 never stores them - it just receives IR (prints "IR: type=..") and
transmits on a serial command ("SEND type=.. val=.. bits=..").

Usage:
  python3 tools/ir_recorder.py list
  python3 tools/ir_recorder.py record              # capture codes until Ctrl-C
  python3 tools/ir_recorder.py record --label vol_up
  python3 tools/ir_recorder.py record --once       # capture a single code
  python3 tools/ir_recorder.py replay vol_up
  python3 tools/ir_recorder.py replay NEC 0x00CF8976 32   # by raw values

Storage format (ir_recordings.json, project root):
  [{"label": "vol_up", "type": 3, "typeName": "NEC",
    "value": "0x00CF8976", "bits": 32}, ...]
"""

import argparse
import json
import os
import re
import sys
import time

import serial

PROJECT_DIR = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
STORE = os.path.join(PROJECT_DIR, "ir_recordings.json")
DEFAULT_PORT = "/dev/cu.usbserial-0001"
BAUD = 115200

IR_RE = re.compile(rb"^IR: type=(\d+) val=0x([0-9A-Fa-f]+) bits=(\d+)$")

TYPE_NAMES = {
    3: "NEC", 4: "SONY", 5: "RC5", 6: "RC6", 9: "SAMSUNG",
    10: "LG", 11: "JVC", 13: "PANASONIC", 26: "DAIKIN", 28: "SAMSUNG36",
    29: "GREE", 0: "UNKNOWN",
}


def open_serial(port):
    s = serial.Serial(port, BAUD, timeout=0.05)
    s.setDTR(False)
    s.setRTS(False)
    time.sleep(0.1)
    s.reset_input_buffer()
    return s


def load_store():
    if not os.path.exists(STORE):
        return []
    with open(STORE) as f:
        return json.load(f)


def save_store(entries):
    with open(STORE, "w") as f:
        json.dump(entries, f, indent=2)
        f.write("\n")


def send_command(s, cmd):
    s.reset_input_buffer()
    s.write(cmd.encode() + b"\n")
    s.flush()
    deadline = time.time() + 2
    buf = b""
    while time.time() < deadline:
        buf += s.read(256)
        if b"SEND OK" in buf:
            return True
    return False


def cmd_list(_args):
    entries = load_store()
    if not entries:
        print("No recordings yet. Run 'record' and press keys on the remote.")
        return
    for e in entries:
        print(f"{e['label']:16s} {e['typeName']:<10s} {e['value']} ({e['bits']} bits)")


def cmd_replay(args):
    entries = load_store()
    match = None
    for e in entries:
        if e["label"] == args.spec:
            match = e
            break
    if match is None:
        # try raw spec: TYPE VALUE BITS  e.g. "NEC 0x00CF8976 32"
        parts = args.spec.split()
        if len(parts) == 3:
            tname = parts[0].upper()
            typ = next((k for k, v in TYPE_NAMES.items() if v == tname), None)
            if typ is not None:
                match = {"label": "raw", "type": typ, "value": parts[1],
                         "bits": int(parts[2])}
    if match is None:
        print(f"Not found: {args.spec}")
        sys.exit(1)

    s = open_serial(args.port)
    print(f"Replaying {match['label']}: {match['value']} "
          f"({match['typeName']}, {match['bits']} bits)")
    ok = send_command(s, f"SEND type={match['type']} val={match['value']} "
                          f"bits={match['bits']}")
    s.close()
    if not ok:
        print("No 'SEND OK' from ESP32 - is the firmware running?")
        sys.exit(1)
    print("Sent.")


def cmd_record(args):
    s = open_serial(args.port)
    entries = load_store()
    existing = {(e["type"], e["value"], e["bits"]) for e in entries}
    last = None
    count = 0
    auto = 1
    print("Recording... press keys on the remote (Ctrl-C to stop).")
    try:
        while True:
            line = s.readline()
            if not line:
                continue
            m = IR_RE.match(line.strip())
            if not m:
                print(f"  (skip) {line.decode(errors='replace').strip()}")
                continue
            typ, valhex, bits = int(m.group(1)), m.group(2).decode(), int(m.group(3))
            key = (typ, valhex, bits)
            if key == last:            # repeated frame of same key
                continue
            last = key
            if typ not in TYPE_NAMES or typ == 0:
                print(f"  (unsupported type {typ}, skipping)")
                continue
            label = args.label if (args.label and count == 0) else f"code{auto}"
            auto += 1
            entry = {"label": label, "type": typ,
                     "typeName": TYPE_NAMES[typ], "value": "0x" + valhex.upper(),
                     "bits": bits}
            if key in existing:
                print(f"  (already saved: {label} - {entry['value']})")
            else:
                entries.append(entry)
                existing.add(key)
                save_store(entries)
                print(f"  saved {label}: {entry['typeName']} {entry['value']} "
                      f"({bits} bits)")
            count += 1
            if args.once:
                break
    except KeyboardInterrupt:
        pass
    s.close()
    print(f"Done. {count} code(s) -> {STORE}")


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--port", default=DEFAULT_PORT, help=f"serial port "
                     f"(default {DEFAULT_PORT})")
    sub = ap.add_subparsers(dest="cmd", required=True)

    p = sub.add_parser("list", help="show saved recordings")
    p.set_defaults(fn=cmd_list)

    p = sub.add_parser("record", help="capture IR codes into ir_recordings.json")
    p.add_argument("--label", help="name for the first captured code")
    p.add_argument("--once", action="store_true", help="capture a single code")
    p.set_defaults(fn=cmd_record)

    p = sub.add_parser("replay", help="send a saved code (by label) or raw spec")
    p.add_argument("spec", help="label from list, or 'TYPE 0xVALUE BITS'")
    p.set_defaults(fn=cmd_replay)

    args = ap.parse_args()
    args.fn(args)


if __name__ == "__main__":
    main()