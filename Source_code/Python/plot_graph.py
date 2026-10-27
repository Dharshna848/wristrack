'''
This is the Python program to get the data from csv file
Plots the graph for delivery status vs number of balls
'''

import csv
import matplotlib.pyplot as plt
from pathlib import Path

#csv file that has the bolwing delivery status
CSV_FILE = Path("C:/Users/bskar/wrist_status.csv")

if not CSV_FILE.exists():
    print(f"{CSV_FILE} doesn't exist")
    raise SystemExit

ball_numbers = []
status_values = []

with open(CSV_FILE, "r", newline="") as file:
    reader = csv.DictReader(file)

    for row in reader:
        ball_numbers.append(int(row["ball_number"]))

        if row["status"] == "WIDE":
            status_values.append(1)
        else:
            status_values.append(0)

plt.figure(figsize=(10, 5))

'''
Plotting the graph
x axis - number of balls
y axis - status of the ball
'''

plt.plot(
    ball_numbers,
    status_values,
    marker="o"
)

plt.xlabel("Ball Number")
plt.ylabel("Status")
plt.title("Wrist Deviation Detection Per Ball")

plt.yticks(
    [0, 1],
    ["NEUTRAL", "WIDE"]
)

plt.xticks(ball_numbers)

plt.grid(True)

plt.tight_layout()
plt.show()
