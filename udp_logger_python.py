import socket
import json
import csv
from pathlib import Path

UDP_IP = "0.0.0.0"
UDP_PORT = 3333

CSV_FILE = Path("wrist_angles.csv")

sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
sock.bind((UDP_IP, UDP_PORT))

# Create CSV with header if needed
file_exists = CSV_FILE.exists()

csv_file = open(CSV_FILE, "a", newline="")
writer = csv.writer(csv_file)

if not file_exists or CSV_FILE.stat().st_size == 0:
    writer.writerow(["ball_number", "angle", "status", "pitch_peak"])
    csv_file.flush()

# Recover the last ball number if the CSV already contains data
ball_number = 0

if CSV_FILE.exists():
    with open(CSV_FILE, "r", newline="") as existing_file:
        reader = csv.DictReader(existing_file)
        for row in reader:
            try:
                ball_number = max(ball_number, int(row["ball_number"]))
            except (ValueError, KeyError):
                pass

print(f"Listening for UDP packets on port {UDP_PORT}...")
print(f"Saving ball data to: {CSV_FILE.resolve()}")
print("Ball number increases ONLY when Arduino sends release=true.")
print("Press Ctrl+C to stop.\n")

try:
    while True:
        data, addr = sock.recvfrom(1024)

        try:
            message = data.decode()
            parsed = json.loads(message)

            release = parsed.get("release", False)
            status = parsed.get("status", "UNKNOWN")
            angle = parsed.get("angle", None)
            pitch_peak = parsed.get("pitchPeak", None)

            print(
                f"From {addr[0]}: "
                f"release={release}, "
                f"status={status}, "
                f"angle={angle} deg, "
                f"pitchPeak={pitch_peak}"
            )

            # A ball is counted from the Arduino's
            # peak-pitch/reversal release event.
            if release:
                ball_number += 1

                writer.writerow([
                    ball_number,
                    angle,
                    status,
                    pitch_peak
                ])
                csv_file.flush()

                print(
                    f"  -> BALL {ball_number} SAVED: "
                    f"{status}, {angle} degrees"
                )

        except Exception as e:
            print("Bad packet:", data, "Error:", e)

except KeyboardInterrupt:
    print("\nStopping logger...")

finally:
    csv_file.close()
    sock.close()
    print(f"CSV saved to: {CSV_FILE.resolve()}")
