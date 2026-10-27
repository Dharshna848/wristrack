#include <Wire.h>
#include <SparkFun_BNO080_Arduino_Library.h>
#include <WiFi.h>
#include <WiFiUdp.h>
#include <math.h>

#define SDA_PIN 6
#define SCL_PIN 7

#define FOREARM_ADDRESS 0x4B
#define WRIST_ADDRESS   0x4A

BNO080 forearm;
BNO080 wrist;

struct Quaternion {
  float w;
  float x;
  float y;
  float z;
};

// Live sensor orientations.
Quaternion armQuat   = {0, 0, 0, 0};
Quaternion wristQuat = {0, 0, 0, 0};

// Fixed neutral reference: captured from armQuat during startup calibration.
Quaternion neutralQuat = {1, 0, 0, 0};

// Peak wrist orientation is retained for wrist-pitch flick detection.
Quaternion peakWristQuat = {1, 0, 0, 0};

bool calibratedNeutral = false;

// Bowling detection state.
bool frontPos = false;
bool backPos = false;
bool armGoingUp = false;
bool waitingForFlick = false;
bool samplingRelease = false;
bool flickDetectedDuringSample = false;

float lastPitch = 0;
float pitchPeak = -999;
float flickStartPitch = 0;

const float FLICK_THRESHOLD = 8.0f;

// Sampling settings.
const float LOWERING_DROP_DEG = 10.0f;
const unsigned long MIN_AFTER_FLICK_MS = 300;
const unsigned long MIN_SAMPLE_MS = 150;
const unsigned long MAX_SAMPLE_MS = 3000;

unsigned long sampleStartTime = 0;
unsigned long flickTime = 0;
unsigned long sampleNumber = 0;

float samplePitchPeak = -999.0f;
float maxX = 0;
float maxY = 0;
float maxZ = 0;
float maxRotationAngle = 0;

// Wi-Fi credentials: replace these placeholders with your own values.
const char* ssid = "<wifiusername>";
const char* password = "<password>";

WiFiUDP udp;
const int UDP_PORT = 3333;
IPAddress destIP(<0,0,0,0>); // Change to the system's IP address.

// ---------------- Quaternion Math ----------------

Quaternion multiply(Quaternion a, Quaternion b)
{
  Quaternion q;
  q.w = a.w*b.w - a.x*b.x - a.y*b.y - a.z*b.z;
  q.x = a.w*b.x + a.x*b.w + a.y*b.z - a.z*b.y;
  q.y = a.w*b.y - a.x*b.z + a.y*b.w + a.z*b.x;
  q.z = a.w*b.z + a.x*b.y - a.y*b.x + a.z*b.w;
  return q;
}

// Valid for a normalized/unit quaternion.
Quaternion inverse(Quaternion q)
{
  Quaternion r;
  r.w = q.w;
  r.x = -q.x;
  r.y = -q.y;
  r.z = -q.z;
  return r;
}

float quaternionNorm(Quaternion q)
{
  return sqrt(q.w*q.w + q.x*q.x + q.y*q.y + q.z*q.z);
}

Quaternion normalizeQuaternion(Quaternion q)
{
  float n = quaternionNorm(q);
  if (n < 0.000001f)
    return {1, 0, 0, 0};

  q.w /= n;
  q.x /= n;
  q.y /= n;
  q.z /= n;
  return q;
}

// Shortest overall rotation represented by a delta quaternion, in degrees.
float getRotationAngleDegrees(Quaternion delta)
{
  delta = normalizeQuaternion(delta);
  float w = fabs(delta.w);
  if (w > 1.0f) w = 1.0f;
  return 2.0f * acos(w) * 57.2957795f;
}

// ---------------- Read Sensors ----------------

void updateSensors()
{
  if (forearm.dataAvailable())
  {
    armQuat.w = forearm.getQuatReal();
    armQuat.x = forearm.getQuatI();
    armQuat.y = forearm.getQuatJ();
    armQuat.z = forearm.getQuatK();
  }

  if (wrist.dataAvailable())
  {
    wristQuat.w = wrist.getQuatReal();
    wristQuat.x = wrist.getQuatI();
    wristQuat.y = wrist.getQuatJ();
    wristQuat.z = wrist.getQuatK();
  }
}

// ---------------- Pitch Calculation ----------------
// Formula used - Standard Quaternion to Euler angle convertion
// Limits the angle within the range of -90 to +90 degrees

float getPitch(Quaternion q)
{
  float sinp = 2.0f * (q.w * q.y - q.z * q.x);
  if (fabs(sinp) >= 1.0f)
    return copysign(90.0f, sinp);
  return asin(sinp) * 57.2957795f;
}

// ---------------- Neutral Calibration ----------------

void autoCalibrateNeutral()
{
  
  sendUdpEvent("CALIBRATION_STARTED");
   
  Serial.println("Waiting for IMUs to stabilize...");

  unsigned long start = millis();
  bool armReady = false;
  bool wristReady = false;

  while (millis() - start < 6000)
  {
    updateSensors();

    if (armQuat.w != 0 || armQuat.x != 0 || armQuat.y != 0 || armQuat.z != 0)
      armReady = true;
    if (wristQuat.w != 0 || wristQuat.x != 0 || wristQuat.y != 0 || wristQuat.z != 0)
      wristReady = true;

    if (armReady && wristReady)
      break;
    delay(10);
  }

  Serial.println("IMUs ready. Calibrating neutral...");
  Serial.println("Hold the forearm in the desired neutral position.");
  delay(500);
  updateSensors();

  // Saving the forearm quaternion only once.
  neutralQuat = normalizeQuaternion(armQuat);
  calibratedNeutral = true;

  sendUdpEvent("CALIBRATION_COMPLETE");

  Serial.print("Fixed neutral arm quaternion: ");
  Serial.print(neutralQuat.w, 5); Serial.print(",");
  Serial.print(neutralQuat.x, 5); Serial.print(",");
  Serial.print(neutralQuat.y, 5); Serial.print(",");
  Serial.println(neutralQuat.z, 5);
  Serial.println("Neutral auto-calibrated.");
  Serial.println();
}

// Forward declaration because sampling starts by collecting a sample.
void collectReleaseSample(float forearmPitch, float wristPitch);

// ---------------- Start Sampling At Top ----------------

void startReleaseSampling(float forearmPitch)
{ 
  samplingRelease = true; // Setting the samplingrelease to true to indicate sample starts from here
  flickDetectedDuringSample = false; //variable to determine the flickdetction happened or not
  sampleStartTime = millis(); // Sample start time
  flickTime = 0;
  sampleNumber = 0;
  samplePitchPeak = pitchPeak; // Assigning the current pitch peak as sample pitch peak

  // Setting all the maximum quaternions to 0

  maxX = 0;
  maxY = 0;
  maxZ = 0;
  maxRotationAngle = 0;

  Serial.println();
  Serial.println("========== TOP-OF-ACTION SAMPLING ==========");
  Serial.println("Sampling started at detected top of action.");
  Serial.println("Deviation is measured against the fixed neutral arm quaternion.");
  Serial.println("Sampling continues through and after flick.");
  Serial.println();

  Serial.println(
    "Sample,Elapsed_ms,ForearmPitch,WristPitch,"
    "WristW,WristX,WristY,WristZ,"
    "DeltaX,DeltaY,DeltaZ,RotationAngleDeg"
  );

  // Start collecting the sample and calculate the wrist deviation
  collectReleaseSample(forearmPitch, getPitch(wristQuat));
}

// ---------------- Finish Sampling ----------------

void finishReleaseSampling()
{  
  samplingRelease = false;
  
  // Maximum quaternions are determined by the time sampling is completed

  Serial.println();
  Serial.println("========== SAMPLE STOPPED ==========");
  Serial.print("Total samples: "); Serial.println(sampleNumber);
  Serial.print("Maximum absolute Delta X: "); Serial.println(maxX, 5);
  Serial.print("Maximum absolute Delta Y: "); Serial.println(maxY, 5);
  Serial.print("Maximum absolute Delta Z: "); Serial.println(maxZ, 5);
  Serial.print("Maximum relative rotation angle (degrees): ");
  Serial.println(maxRotationAngle, 3);

  // Wrist Deviation is detected if the Maximum quaternions in Z and Y coordinates go beyond the threshold limit

  bool wristDeviationDetected = (maxZ > 0.5f) || (maxX > 0.30f);
  const char* status = wristDeviationDetected ? "WRISTDEVIATION" : "NEUTRAL";

  if (wristDeviationDetected)
    Serial.println("RESULT: WRIST DEVIATION DETECTED");
  else
    Serial.println("RESULT: NEUTRAL");

  // Send result and measured maximum quaternions to the Python program over UDP.
  udp.beginPacket(destIP, UDP_PORT);
  udp.printf(
    "{\"release\":true,\"status\":\"%s\",\"maxX\":%.5f,\"maxY\":%.5f,\"maxZ\":%.5f,\"samples\":%lu,\"maxRotationAngle\":%.3f}",
    status, maxX, maxY, maxZ, (unsigned long)sampleNumber, maxRotationAngle
  );
  udp.endPacket();

  Serial.println("Final result sent over UDP.");
  Serial.println("Waiting for next front position...");
  Serial.println("====================================");
  Serial.println();

  // Reset for the next delivery; keep neutralQuat unchanged.
  frontPos = false;
  backPos = false;
  armGoingUp = false;
  waitingForFlick = false;
  pitchPeak = -999;
  samplePitchPeak = -999;
}

// ---------------- Collect Every Sample ----------------

void collectReleaseSample(float forearmPitch, float wristPitch)
{
  if (!samplingRelease)
    return;

  unsigned long now = millis();
  unsigned long elapsed = now - sampleStartTime;

  // Current wrist quaternion is compared with the fixed neutral arm quaternion.
  Quaternion currentWrist = normalizeQuaternion(wristQuat);
  Quaternion delta = normalizeQuaternion(
    multiply(inverse(neutralQuat), currentWrist)
  );

  // q and -q represent the same physical orientation. Keep delta consistent.
  if (delta.w < 0)
  {
    delta.w = -delta.w;
    delta.x = -delta.x;
    delta.y = -delta.y;
    delta.z = -delta.z;
  }

  float absX = fabs(delta.x);
  float absY = fabs(delta.y);
  float absZ = fabs(delta.z);
  
  // Keep assigning the maximum by checking with the previous values 
  if (absX > maxX) maxX = absX;
  if (absY > maxY) maxY = absY;
  if (absZ > maxZ) maxZ = absZ;

  float rotationAngle = getRotationAngleDegrees(delta);
  if (rotationAngle > maxRotationAngle)
    maxRotationAngle = rotationAngle;

  sampleNumber++;

  Serial.print(sampleNumber); Serial.print(",");
  Serial.print(elapsed); Serial.print(",");
  Serial.print(forearmPitch, 3); Serial.print(",");
  Serial.print(wristPitch, 3); Serial.print(","); 
  Serial.print(currentWrist.w, 5); Serial.print(",");
  Serial.print(currentWrist.x, 5); Serial.print(",");
  Serial.print(currentWrist.y, 5); Serial.print(",");
  Serial.print(currentWrist.z, 5); Serial.print(",");
  Serial.print(delta.x, 5); Serial.print(",");
  Serial.print(delta.y, 5); Serial.print(",");
  Serial.print(delta.z, 5); Serial.print(","); 
  Serial.println(rotationAngle, 3);

  if (forearmPitch > samplePitchPeak)
    samplePitchPeak = forearmPitch;
  
  // Detect wrist flick by comparing the start wrist pitch - flickStartPitch and the current wrist pitch
  // If the differnece is more than the set FLICK_THRESHOLD, WristFlick is detected
  

  if (!flickDetectedDuringSample)
  {
    float flickAmount = flickStartPitch - wristPitch;
    if (flickAmount > FLICK_THRESHOLD)
    {
      flickDetectedDuringSample = true;
      flickTime = now;
      Serial.println("WRIST FLICK DETECTED DURING SAMPLING");
      Serial.println("Continuing to collect post-flick samples.");
    }
  }

  bool forearmLowered = forearmPitch <= samplePitchPeak - LOWERING_DROP_DEG;
  bool enoughTimeAfterFlick = flickDetectedDuringSample &&
                              (now - flickTime >= MIN_AFTER_FLICK_MS);

  // Sampling is calculated before wrist flick and it continues until a certain time 
  // Logic - If the elapsed time >= minimum sample time and the time went past the flick for a set time period (MIN_AFTER_FLICK_MS) and forearm is lowered more than 10 degrees

  if (elapsed >= MIN_SAMPLE_MS && enoughTimeAfterFlick && forearmLowered)
  {
    finishReleaseSampling();
    return;
  }
  
  // or sampling is taken until the maximum sampling time is reached

  if (elapsed >= MAX_SAMPLE_MS)
  {
    Serial.println("WARNING: Sampling stopped due to timeout.");

    // Calculate the maximum quaternions at the end of sampling
    finishReleaseSampling();
  }
}

// ---------------- Detection ----------------

void detectWristDeviation()
{
  updateSensors();

  // Calculates both Forearm pitch and wrist pitch
  // Forearm pitch is used to determine arm position and movement to detect bowling action - Front, Back, Upward movement, Top of bowling action
  // Wrist pitch is used to determine the wrist flick (bowling) and the time to sample

  float pitch = getPitch(armQuat);
  float wristPitch = getPitch(wristQuat);

  if (samplingRelease)
  {
    collectReleaseSample(pitch, wristPitch);
    lastPitch = pitch;
    return;
  }

  // FRONT POSITION Detection using forearm pitch : pitch between -10 to 30 degree
  if (!frontPos && pitch > -10 && pitch < 30)
  {
    frontPos = true;
    Serial.println("Front position detected");
  }

  // BACK POSITION Detection using forearm pitch : pitch < -30
  if (frontPos && !backPos && pitch < -30)
  {
    backPos = true;
    Serial.println("Back position detected");
  }

  // Upward arm movement detection using forearm pitch : if pitch > last pitch + 3
  if (backPos && pitch > lastPitch + 3)
  {
    armGoingUp = true;
    if (pitch > pitchPeak)
    {
      pitchPeak = pitch;
      peakWristQuat = wristQuat;
    }
  }

  // Storing the pitch peak for both arm and wrist

  if (armGoingUp && pitch > pitchPeak)
  {
    pitchPeak = pitch;
    peakWristQuat = wristQuat;
  }

  // TOP OF ACTION Detction using forearm pitch : if arm going up is set to true and pitch < pitchpeak < 3
  // The moment when arm starts to move down, top of action is detected
  // Pitch peak for both arm and wrist are stored
  // This is the point when sampling starts

  if (armGoingUp && pitch < pitchPeak - 3)
  {
    armGoingUp = false;

    Serial.print("TOP OF ACTION detected at pitch: ");
    Serial.println(pitchPeak);

    // Wrist pitch at the peak remains the flick reference.
    flickStartPitch = getPitch(peakWristQuat);
    waitingForFlick = true;

    // Sampling starts here
    startReleaseSampling(pitch);
  }

  lastPitch = pitch;
}

// UDP Status Message Function
void sendUdpEvent(const char* eventName) {
  udp.beginPacket(destIP, UDP_PORT);
  udp.printf(
    "{\"event\":\"%s\"}",
    eventName
  );
  udp.endPacket();
}


// ---------------- Setup ----------------

void setup()
{
  Serial.begin(115200);
  delay(1000);
  Serial.println("Bowling Wrist Deviation Detector - Fixed Neutral Reference");

  Wire.begin(SDA_PIN, SCL_PIN);
  Wire.setClock(100000);

  if (forearm.begin(FOREARM_ADDRESS, Wire))
  {
    Serial.println("Forearm OK");
    forearm.enableRotationVector(20);
  }
  else
  {
    Serial.println("ERROR: Forearm IMU not found!");
    while (true) delay(100);
  }

  if (wrist.begin(WRIST_ADDRESS, Wire))
  {
    Serial.println("Wrist OK");
    wrist.enableRotationVector(20);
  }
  else
  {
    Serial.println("ERROR: Wrist IMU not found!");
    while (true) delay(100);
  }

  forearm.calibrateAll();
  wrist.calibrateAll();

  Serial.println("Connecting to WiFi...");
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED)
  {
    delay(200);
    Serial.print(".");
  }

  Serial.println();
  Serial.println("WiFi connected");
  Serial.print("ESP32 IP: ");
  Serial.println(WiFi.localIP());

  udp.begin(UDP_PORT);
  Serial.println("UDP started");

  // first step is neutral calibration
  autoCalibrateNeutral();
}

// ---------------- Loop ----------------

void loop()
{

  //checks if auto calibration is done
  if (!calibratedNeutral)
    return;

  // Calculate wrist deviation
  detectWristDeviation();
  delay(10);
}
