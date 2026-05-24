import serial
import serial.tools.list_ports
import csv
import os
import time
from datetime import datetime

#═══════════════════════════════════════
# CONFIG
#═══════════════════════════════════════
BAUD_RATE    = 921600
OUTPUT_DIR   = "dataset"
LABEL_NAMES  = {
    0: "NORMAL",
    1: "BRG_FAULT",
    2: "OVERLOAD",
    3: "MISALIGN"
}

CSV_HEADER = [
    "timestamp",
    "accel_x", "accel_y", "accel_z",
    "gyro_x",  "gyro_y",  "gyro_z",
    "temperature", "humidity",
    "current", "motorVoltage",
    "label"
]

#═══════════════════════════════════════
# AUTO DETECT ESP32 PORT
#═══════════════════════════════════════
def find_esp32_port():
    ports = serial.tools.list_ports.comports()
    for port in ports:
        desc = port.description.lower()
        if any(x in desc for x in
               ["cp210", "ch340", "uart",
                "usb serial", "esp32"]):
            return port.device
    return None

#═══════════════════════════════════════
# GENERATE CSV FILENAME
# Format: dataset/NORMAL_2024-01-15_14-30-00.csv
#═══════════════════════════════════════
def make_filename(label_id):
    os.makedirs(OUTPUT_DIR, exist_ok=True)
    label_name = LABEL_NAMES.get(label_id,
                                  f"LABEL_{label_id}")
    timestamp  = datetime.now().strftime(
                  "%Y-%m-%d_%H-%M-%S")
    filename   = f"{OUTPUT_DIR}/{label_name}_{timestamp}.csv"
    return filename

#═══════════════════════════════════════
# VALIDATE CSV ROW
#═══════════════════════════════════════
def parse_line(line):
    try:
        parts = line.strip().split(",")
        if len(parts) != 12:
            return None

        row = {
            "timestamp":    int(parts[0]),
            "accel_x":      float(parts[1]),
            "accel_y":      float(parts[2]),
            "accel_z":      float(parts[3]),
            "gyro_x":       float(parts[4]),
            "gyro_y":       float(parts[5]),
            "gyro_z":       float(parts[6]),
            "temperature":  float(parts[7]),
            "humidity":     float(parts[8]),
            "current":      float(parts[9]),
            "motorVoltage": float(parts[10]),
            "label":        int(parts[11])
        }
        return row
    except (ValueError, IndexError):
        return None

#═══════════════════════════════════════
# MAIN LOGGER
#═══════════════════════════════════════
def main():
    print("=" * 45)
    print("  ESP32 Predictive Maintenance Logger")
    print("=" * 45)

    # Find port
    port = find_esp32_port()
    if port is None:
        print("\n[ERROR] ESP32 not found!")
        print("Available ports:")
        for p in serial.tools.list_ports.comports():
            print(f"  {p.device} - {p.description}")
        port = input("\nEnter port manually (e.g. COM3): ").strip()

    print(f"\n[OK] Using port: {port}")
    print(f"[OK] Baud rate : {BAUD_RATE}")

    # Ask for label
    print("\nSelect label to collect:")
    for k, v in LABEL_NAMES.items():
        print(f"  {k} = {v}")
    
    while True:
        try:
            label_input = int(input("\nEnter label (0-3): "))
            if label_input in LABEL_NAMES:
                break
            print("Invalid — enter 0, 1, 2 or 3")
        except ValueError:
            print("Enter a number")

    # Generate filename
    filename = make_filename(label_input)
    print(f"\n[OK] Saving to: {filename}")
    print(f"[OK] Label    : {LABEL_NAMES[label_input]}")

    # Connect to ESP32
    try:
        ser = serial.Serial(port, BAUD_RATE, timeout=2)
        time.sleep(2)  # Let ESP32 boot
        ser.reset_input_buffer()
        print(f"\n[OK] Connected to ESP32")
    except serial.SerialException as e:
        print(f"\n[ERROR] Cannot open port: {e}")
        return

    # Open CSV and start logging
    sample_count  = 0
    error_count   = 0
    start_time    = time.time()
    current_label = None

    print("\n[LOGGING] Press Ctrl+C to stop\n")

    try:
        with open(filename, "w",
                  newline="") as csvfile:
            writer = csv.DictWriter(csvfile,
                                    fieldnames=CSV_HEADER)
            writer.writeheader()

            while True:
                try:
                    raw = ser.readline()
                    if not raw:
                        continue

                    line = raw.decode("utf-8",
                                      errors="ignore")

                    # Skip status/debug lines
                    if any(x in line for x in
                           ["[", "=", "Label",
                            "START", "INFO",
                            "WARN", "ERROR"]):
                        print(f"[ESP32] {line.strip()}")
                        continue

                    # Parse data row
                    row = parse_line(line)
                    if row is None:
                        error_count += 1
                        continue

                    # Detect label change mid session
                    if current_label is None:
                        current_label = row["label"]
                    elif row["label"] != current_label:
                        print(f"\n[WARN] Label changed"
                              f" {current_label} ->"
                              f" {row['label']}")
                        print("[WARN] Starting new file...")

                        # Close current file
                        csvfile.flush()

                        # Open new file for new label
                        current_label = row["label"]
                        filename = make_filename(
                                    current_label)
                        csvfile = open(filename, "w",
                                       newline="")
                        writer = csv.DictWriter(
                                    csvfile,
                                    fieldnames=CSV_HEADER)
                        writer.writeheader()
                        sample_count = 0
                        print(f"[OK] New file: {filename}")

                    writer.writerow(row)
                    sample_count += 1

                    # Progress update every 500 samples
                    if sample_count % 500 == 0:
                        elapsed = time.time() - start_time
                        rate    = sample_count / elapsed
                        print(
                            f"[LOG] Samples: {sample_count:6d} | "
                            f"Errors: {error_count:4d} | "
                            f"Rate: {rate:.1f} Hz | "
                            f"Label: {LABEL_NAMES[current_label]}"
                        )

                except UnicodeDecodeError:
                    error_count += 1
                    continue

    except KeyboardInterrupt:
        elapsed = time.time() - start_time
        print(f"\n\n{'='*45}")
        print(f"  Logging stopped")
        print(f"{'='*45}")
        print(f"  Samples  : {sample_count}")
        print(f"  Errors   : {error_count}")
        print(f"  Duration : {elapsed:.1f} seconds")
        print(f"  Avg rate : {sample_count/elapsed:.1f} Hz")
        print(f"  Saved to : {filename}")
        print(f"{'='*45}")

    finally:
        ser.close()
        print("\n[OK] Port closed")

if __name__ == "__main__":
    main()