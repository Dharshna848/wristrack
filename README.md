Project Summary 

This project uses an IMU-based system for detecting wrist deviation during cricket bowling. Two BNO086 sensors are mounted on an athlete’s arm and wrist to measure their relative orientation and movement. Quaternion-based orientation data is processed by ESP-32 C6 microcontroller to determine the wrist’s deviation relative to the forearm. This system is intended to provide measurements to help bowlers minimize excessive wrist movement which can impact bowler’s consistent ball deliveries, speed of the ball and legal deliveries. 

Files - 

Docs/WrisTrack.pdf - Document that explains the architecture of whole project

Docs/WrisTrack.docx - WrisTrack project documentation in docx format

Source_code/Arduion/Wristrack_code.ino - Arduino code

Source_code/Python/udplogger.py - Python program to receive the status of ball deliveries and store it in a csv file

Source_code/Python/Plot_graph.py - Python program to plot the ball deliveries

Results/arduino_output_final.txt - Results from Arduino while connected through USB

Results/wrist_deviation_detector_python.txt - Results from Python Udp logger

Results/wrist_deviation_chart.png - Results from Graph plotter

Demo/Recording_demo_results.mp4 - Recording for Wristrack project demo
