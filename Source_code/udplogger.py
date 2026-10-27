import socket
import json
import csv
from pathlib import Path

# -----------------------------
# UDP SETTINGS (IP address masked here)
# -----------------------------
UDP_IP = "0.0.0.0"
UDP_PORT = 3333

# -----------------------------
# CSV SETTINGS
# -----------------------------
CSV_FILE = Path("C:/Users/bskar/wrist_status.csv")

# -----------------------------
# CREATE UDP SOCKET
# -----------------------------
sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
sock.bind((UDP_IP, UDP_PORT))

# -----------------------------
# RECOVER LAST BALL NUMBER
# -----------------------------
file_exists = CSV_FILE.exists()
ball_number = 0

if file_exists and CSV_FILE.stat().st_size > 0:
    with open(CSV_FILE, "r", newline="") as existing_file:
        reader = csv.DictReader(existing_file)

        for row in reader:
            try:
                ball_number = max(
                    ball_number,
                    int(row["ball_number"])
                )
            except (ValueError, KeyError, TypeError):
                pass

# -----------------------------
# OPEN CSV FILE
# -----------------------------
csv_file = open(CSV_FILE, "a", newline="")
writer = csv.writer(csv_file)

if not file_exists or CSV_FILE.stat().st_size == 0:
    writer.writerow(["ball_number", "status"])
    csv_file.flush()

# -----------------------------
# STARTUP MESSAGES
# -----------------------------
print(f"Saving delivery data to: {CSV_FILE.resolve()}")
print("Waiting for ESP32 calibration status.")
print("Press Ctrl+C to stop.\n")

try:
    while True:
        data, addr = sock.recvfrom(1024)

        try:
            message = data.decode("utf-8")
            parsed = json.loads(message)

            # -----------------------------
            # HANDLE CALIBRATION EVENTS
            # -----------------------------
            event = parsed.get("event", "")

            if event == "CALIBRATION_STARTED":
                print("\n")
                print("=" * 48)
                print("CALIBRATION STARTED")
                print("Please hold the forearm steady.")
                print("=" * 48)
                print("\n")
                continue

            elif event == "CALIBRATION_COMPLETE":
                print("\n")
                print("=" * 48)
                print("CALIBRATION COMPLETE")
                print("READY TO BOWL!")
                print("=" * 48)
                print("\n")
                continue

            # -----------------------------
            # HANDLE DELIVERY RESULTS
            # -----------------------------
            release = parsed.get("release", False)
            status = parsed.get("status", "UNKNOWN")

            print(
                f"From {addr[0]}: "
                f"release={release}, "
                f"status={status}"
            )

            # Save only when a ball release is detected.
            if release:
                ball_number += 1

                writer.writerow([ball_number, status])
                csv_file.flush()

                print(
                    f"  -> BALL {ball_number} SAVED: {status}"
                )

        except (json.JSONDecodeError, UnicodeDecodeError) as e:
            print("Invalid UDP packet:", data, "Error:", e)

        except Exception as e:
            print("Error processing packet:", data, "Error:", e)

except KeyboardInterrupt:
    print("\nStopping logger...")

finally:
    csv_file.close()
    sock.close()
    print(f"CSV saved to: {CSV_FILE.resolve()}")
