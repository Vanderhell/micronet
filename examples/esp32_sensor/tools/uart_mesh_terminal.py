import argparse
import queue
import sys
import threading
import time

import serial


def parse_demo_line(line):
    if not line.startswith("MNET_DEMO|"):
        return None
    fields = {}
    for item in line.strip().split("|")[1:]:
        if "=" not in item:
            continue
        key, value = item.split("=", 1)
        fields[key] = value
    return fields


class PortReader(threading.Thread):
    def __init__(self, index, name, ser, output_queue, stats):
        super().__init__(daemon=True)
        self.index = index
        self.name = name
        self.ser = ser
        self.output_queue = output_queue
        self.stats = stats

    def run(self):
        while True:
            try:
                raw = self.ser.readline()
            except serial.SerialException as exc:
                self.output_queue.put((self.index, f"[serial-error] {exc}"))
                return

            if not raw:
                continue

            line = raw.decode("utf-8", errors="replace").rstrip()
            parsed = parse_demo_line(line)
            if parsed is not None:
                node = parsed.get("node", self.name)
                event = parsed.get("event", "")
                entry = self.stats.setdefault(node, {"rx": 0, "tx": 0, "last": ""})
                if event == "rx":
                    entry["rx"] += 1
                elif event == "tx":
                    entry["tx"] += 1
                entry["last"] = line
            self.output_queue.put((self.index, line))


def print_summary(stats):
    print("")
    print("Summary:")
    for node in sorted(stats):
        entry = stats[node]
        print(f"  {node}: tx={entry['tx']} rx={entry['rx']}")
        if entry["last"]:
            print(f"    last={entry['last']}")
    print("")


def send_line(port_map, target, text):
    payload = (text.rstrip() + "\n").encode("utf-8")
    if target == "all":
        for ser in port_map.values():
            ser.write(payload)
        return

    index = int(target)
    ser = port_map.get(index)
    if ser is None:
        print(f"Unknown target {target}")
        return
    ser.write(payload)


def main():
    parser = argparse.ArgumentParser(description="Watch three ESP32 serial ports and send commands.")
    parser.add_argument("--ports", nargs=3, required=True, help="Exactly three serial ports, one per node.")
    parser.add_argument("--baud", type=int, default=115200, help="Serial baud rate.")
    args = parser.parse_args()

    port_map = {}
    stats = {}
    output_queue = queue.Queue()

    for index, port_name in enumerate(args.ports, start=1):
        ser = serial.Serial(port_name, args.baud, timeout=0.25)
        port_map[index] = ser
        PortReader(index, port_name, ser, output_queue, stats).start()

    print("Interactive commands:")
    print("  /1 status")
    print("  /2 ping 1")
    print("  /3 send 1 ahoj")
    print("  /all hello")
    print("  /summary")
    print("  /quit")
    print("")

    def printer():
        while True:
            index, line = output_queue.get()
            timestamp = time.strftime("%H:%M:%S")
            print(f"[{timestamp}] [node{index}] {line}")

    threading.Thread(target=printer, daemon=True).start()

    try:
        while True:
            line = sys.stdin.readline()
            if not line:
                break
            text = line.strip()
            if not text:
                continue
            if text == "/quit":
                break
            if text == "/summary":
                print_summary(stats)
                continue
            if text.startswith("/all "):
                send_line(port_map, "all", text[5:])
                continue
            if text.startswith("/") and len(text) > 3 and text[2] == " " and text[1].isdigit():
                send_line(port_map, text[1], text[3:])
                continue
            print("Unknown command. Use /1, /2, /3, /all, /summary or /quit.")
    finally:
        for ser in port_map.values():
            ser.close()


if __name__ == "__main__":
    main()
