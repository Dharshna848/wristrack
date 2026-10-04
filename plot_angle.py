import csv
import matplotlib.pyplot as plt
from pathlib import Path

CSV_FILE = Path("wrist_angles.csv")

if not CSV_FILE.exists():
    print(f"Could not find {CSV_FILE}")
    print("Run udp_angle_logger.py first.")
    raise SystemExit

ball_numbers = []
angles = []

with open(CSV_FILE, "r", newline="") as file:
    reader = csv.DictReader(file)

    for row in reader:
        ball_numbers.append(int(row["ball_number"]))
        angles.append(float(row["angle"]))

if not ball_numbers:
    print("No ball data found in the CSV file.")
    raise SystemExit

plt.figure(figsize=(10, 5))

plt.plot(
    ball_numbers,
    angles,
    marker="o"
)

plt.xlabel("No. of balls")
plt.ylabel("Angle (degrees)")
plt.title("Wrist Side-to-Side Angle per Ball")

plt.xticks(ball_numbers)
plt.grid(True)

plt.tight_layout()
plt.show()