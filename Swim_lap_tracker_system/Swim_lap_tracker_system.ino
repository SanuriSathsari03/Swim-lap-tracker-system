// Main Box


#include <SPI.h>
#include <LoRa.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>

// Hardware pins
#define SONIC_TRIG 7
#define SONIC_ECHO 6
#define FUNCTION_BUTTON 2

// LoRa pins
#define LORA_CS 10
#define LORA_RST 9
#define LORA_DIO 8

// LCD setup
LiquidCrystal_I2C screen(0x27, 16, 4);

// System variables
bool operationalMode = false;
bool priorButtonState = HIGH;
unsigned long intervalStart = 0;
unsigned long currentInterval = 0;
unsigned long aggregateTime = 0;
unsigned long pauseAccumulated = 0;
unsigned long detectionCommence = 0;
unsigned long lastAbsenceTime = 0;
unsigned long lastCommunicationTime = 0;
unsigned long lastIntervalEnd = 0;
int lapCounter = 0;
unsigned long meanLapTime = 0;

// Display optimization
int displayedLaps = -1;
unsigned long displayedPause = 0;
unsigned long displayedInterval = 0;

// Pause tracking
unsigned long pauseBeginStamp = 0;
bool inPauseState = false;
unsigned long lastPauseUpdate = 0;
unsigned long uninterruptedPauseStart = 0;

// Performance phases
enum PerformancePhase { AT_START, IN_TRANSIT, AT_TURN, COMPLETING_LAP };
PerformancePhase athleticPhase = AT_START;

// Constants
const int PROXIMITY_LOW = 30;
const int PROXIMITY_HIGH = 40;
const unsigned long PAUSE_INTERVAL = 3000;
const unsigned long LAP_COMMENCE_DELAY = 1000;
const unsigned long OPERATION_COOLDOWN = 2000;
const unsigned long ABSENCE_TIMEOUT = 2000;
const unsigned long COMMUNICATION_TIMEOUT = 30000;
const unsigned long INPUT_DEBOUNCE = 2000;
const unsigned long PAUSE_UPDATE_RATE = 500;

// Helper functions
long calculateDistance(int trigger, int echo) {
  digitalWrite(trigger, LOW);
  delayMicroseconds(2);
  digitalWrite(trigger, HIGH);
  delayMicroseconds(10);
  digitalWrite(trigger, LOW);
  long interval = pulseIn(echo, HIGH, 30000);
  return interval * 0.034 / 2;
}

bool objectInZone(long measurement) {
  return (measurement >= PROXIMITY_LOW && measurement <= PROXIMITY_HIGH);
}

void refreshVisuals() {
  // Lap counter display
  if (lapCounter != displayedLaps) {
    screen.setCursor(0, 0);
    screen.print("Count: ");
    screen.print(lapCounter);
    screen.print("    ");
    displayedLaps = lapCounter;
  }
  
  // Current interval display
  if (athleticPhase == AT_START && currentInterval != displayedInterval) {
    screen.setCursor(0, 1);
    screen.print("Time: ");
    screen.print(currentInterval / 1000.0, 1);
    screen.print("s    ");
    displayedInterval = currentInterval;
  }
  
  // Pause duration display
  if (pauseAccumulated != displayedPause) {
    screen.setCursor(16, 0);
    screen.print("Rest: ");
    screen.print(pauseAccumulated / 1000.0, 1);
    screen.print("s    ");
    displayedPause = pauseAccumulated;
  }
}

void showInfoMessage(String info) {
  screen.setCursor(16, 1);
  screen.print("                ");
  screen.setCursor(16, 1);
  screen.print(info);
}

void indicatePause() {
  screen.setCursor(16, 1);
  screen.print("Resting...      ");
}

void clearPauseIndicator() {
  screen.setCursor(16, 1);
  screen.print("                ");
}

void presentSummary() {
  screen.clear();
  screen.setCursor(0, 0); screen.print("Session Summary");
  screen.setCursor(0, 1); screen.print("Laps: "); screen.print(lapCounter);
  screen.setCursor(16, 0); screen.print("Time: ");
  if (lapCounter > 0) {
    screen.print(meanLapTime / 1000.0, 1);
    screen.print("s");
  } else {
    screen.print("0.0s");
  }
  screen.setCursor(16, 1); screen.print("Rest: "); screen.print(pauseAccumulated / 1000.0, 1); screen.print("s");
}

void wirelessTransmit(String content) {
  LoRa.beginPacket();
  LoRa.print(content);
  LoRa.endPacket();
}

void configureWireless() {
  LoRa.setSignalBandwidth(125E3);
  LoRa.setSpreadingFactor(9);
  LoRa.setCodingRate4(7);
  LoRa.setSyncWord(0x34);
}

// Setup function
void setup() {
  Serial.begin(115200);
  pinMode(SONIC_TRIG, OUTPUT);
  pinMode(SONIC_ECHO, INPUT);
  pinMode(FUNCTION_BUTTON, INPUT_PULLUP);

  screen.begin();
  screen.backlight();
  screen.print("Pool Lap Tracker");

  LoRa.setPins(LORA_CS, LORA_RST, LORA_DIO);
  if (!LoRa.begin(433E6)) {
    screen.setCursor(0, 1);
    screen.print("Radio Error");
    while (1);
  }
  
  configureWireless();
  delay(1500);
  screen.clear();
  screen.print("Press button to start");
}

// Main loop
void loop() {
  // Button handling
  bool buttonStatus = digitalRead(FUNCTION_BUTTON);
  if (priorButtonState == HIGH && buttonStatus == LOW) {
    if (!operationalMode) {
      // Start monitoring session
      operationalMode = true;
      lapCounter = 0;
      aggregateTime = 0;
      currentInterval = 0;
      meanLapTime = 0;
      pauseAccumulated = 0;
      athleticPhase = AT_START;
      detectionCommence = 0;
      lastAbsenceTime = 0;
      lastCommunicationTime = 0;
      lastIntervalEnd = 0;
      pauseBeginStamp = 0;
      inPauseState = false;
      lastPauseUpdate = 0;
      uninterruptedPauseStart = 0;
      displayedLaps = -1;
      displayedPause = 0;
      displayedInterval = 0;
      screen.clear();
      screen.print("Started!");
      wirelessTransmit("COMMENCE");
      delay(1000);
      screen.clear();
      screen.setCursor(0, 0); screen.print("Count: 0");
      screen.setCursor(0, 1); screen.print("Time: 0.0s");
      screen.setCursor(16, 0); screen.print("Rest: 0.0s");
    } else {
      // End monitoring session
      operationalMode = false;
      wirelessTransmit("TERMINATE");
      presentSummary();
      
      // Wait for user confirmation
      bool awaitingConfirmation = true;
      while (awaitingConfirmation) {
        buttonStatus = digitalRead(FUNCTION_BUTTON);
        if (buttonStatus == LOW) {
          awaitingConfirmation = false;
          delay(300);
        }
        delay(50);
      }
      
      // Display completion notice
      screen.clear();
      screen.setCursor(0, 0);
      screen.print("Ended!");
      delay(2000);
      
      // Return to standby state
      screen.clear();
      screen.print("Press button to start");
    }
  }
  priorButtonState = buttonStatus;

  if (!operationalMode) return;

  // Process incoming wireless communications
  if (LoRa.parsePacket()) {
    String incomingMessage = "";
    while (LoRa.available()) {
      incomingMessage += (char)LoRa.read();
    }
    
    if (incomingMessage == "TURN_SIGNAL") {
      lastCommunicationTime = millis();
      
      if (athleticPhase == IN_TRANSIT) {
        athleticPhase = AT_TURN;
        showInfoMessage("At end point");
        delay(1500);
        refreshVisuals();
      }
    } else if (incomingMessage.startsWith("PAUSE_DATA:")) {
      String pauseData = incomingMessage.substring(11);
      unsigned long additionalPause = pauseData.toInt();
      pauseAccumulated += additionalPause;
      refreshVisuals();
      Serial.print("Rest added: "); Serial.print(additionalPause); 
      Serial.print("ms, Total: "); Serial.print(pauseAccumulated); Serial.println("ms");
    }
  }

  // Handle communication timeout
  unsigned long presentMoment = millis();
  if (athleticPhase == IN_TRANSIT && presentMoment - intervalStart > COMMUNICATION_TIMEOUT) {
    if (lastCommunicationTime < intervalStart) {
      athleticPhase = COMPLETING_LAP;
      showInfoMessage("Signal error!");
      delay(1000);
    }
  }

  // Read sensor data
  long proximityReading = calculateDistance(SONIC_TRIG, SONIC_ECHO);
  presentMoment = millis();
  bool objectPresent = objectInZone(proximityReading);

  // Lap completion handling
  if (objectPresent && (athleticPhase == AT_TURN || athleticPhase == COMPLETING_LAP)) {
    currentInterval = presentMoment - intervalStart;
    lapCounter++;
    aggregateTime += currentInterval;
    meanLapTime = aggregateTime / lapCounter;
    athleticPhase = AT_START;
    lastIntervalEnd = presentMoment;
    refreshVisuals();
    showInfoMessage("Completed!");
    delay(1000);
    refreshVisuals();
    
    // Clear pause state if active
    if (inPauseState) {
      inPauseState = false;
      clearPauseIndicator();
    }
  }

  // Lap initiation handling
  if (objectPresent && athleticPhase == AT_START) {
    if (detectionCommence == 0) {
      detectionCommence = presentMoment;
    }
    
    unsigned long detectionPeriod = presentMoment - detectionCommence;
    if (detectionPeriod >= LAP_COMMENCE_DELAY && detectionPeriod < PAUSE_INTERVAL) {
      intervalStart = presentMoment;
      athleticPhase = IN_TRANSIT;
      detectionCommence = 0;
      lastAbsenceTime = 0;
      lastCommunicationTime = 0;
      showInfoMessage("Started!");
      delay(1500);
      refreshVisuals();
      
      // Clear pause state if active
      if (inPauseState) {
        inPauseState = false;
        clearPauseIndicator();
      }
    }
  } else {
    if (athleticPhase == AT_START) {
      detectionCommence = 0;
    }
  }

  // Pause detection handling
  if (objectPresent) {
    // Initialize pause detection
    if (!inPauseState && pauseBeginStamp == 0 && detectionCommence == 0) {
      pauseBeginStamp = presentMoment;
    }
    
    // Check for pause state activation
    if (!inPauseState && pauseBeginStamp != 0 && (presentMoment - pauseBeginStamp >= PAUSE_INTERVAL)) {
      inPauseState = true;
      uninterruptedPauseStart = presentMoment;
      indicatePause();
      Serial.println("Rest started");
    }
    
    // Update pause duration during pause state
    if (inPauseState) {
      unsigned long currentPauseDuration = presentMoment - uninterruptedPauseStart;
      pauseAccumulated = currentPauseDuration;
      lastPauseUpdate = presentMoment;
      
      // Periodic display refresh
      if (presentMoment - lastPauseUpdate >= PAUSE_UPDATE_RATE) {
        refreshVisuals();
        lastPauseUpdate = presentMoment;
      }
    }
    
    // Reset absence tracking
    lastAbsenceTime = 0;
  } else {
    // Handle object absence
    if (lastAbsenceTime == 0) {
      lastAbsenceTime = presentMoment;
    }
    
    // Process pause termination
    if (lastAbsenceTime != 0 && (presentMoment - lastAbsenceTime > INPUT_DEBOUNCE)) {
      // Finalize pause period if active
      if (inPauseState) {
        unsigned long pauseDuration = presentMoment - uninterruptedPauseStart;
        pauseAccumulated += pauseDuration;
        inPauseState = false;
        pauseBeginStamp = 0;
        clearPauseIndicator();
        Serial.print("Rest ended: "); Serial.print(pauseDuration/1000.0); Serial.println("s");
        Serial.print("Total rest: "); Serial.print(pauseAccumulated/1000.0); Serial.println("s");
      }
      
      // Reset pause timer if not in pause state
      if (pauseBeginStamp != 0 && !inPauseState) {
        pauseBeginStamp = 0;
      }
    }
  }

  // Cancel pause timer if lap detection initiates
  if (detectionCommence != 0 && pauseBeginStamp != 0 && !inPauseState) {
    pauseBeginStamp = 0;
  }

  delay(150);
}

// Secondary Box

#include <SPI.h>
#include <LoRa.h>

// Sensor pins
const int distanceTrigger = 27;
const int distanceEcho = 26;

// LoRa pins
#define LORA_SELECT_PIN 5
#define LORA_RESET_PIN 17
#define LORA_INTERRUPT_PIN 16

// Detection parameters
const int DETECTION_LOW = 25;
const int DETECTION_HIGH = 45;
const unsigned long REST_THRESHOLD = 3000;
const unsigned long SIGNAL_COOLDOWN = 10000;
const unsigned long DETECTION_DEBOUNCE = 2000;

// System states
enum SensorMode { WAITING, FOUND, PAUSED, LEFT };
SensorMode currentMode = WAITING;

bool sessionActive = false;
unsigned long detectionInitiated = 0;
unsigned long restInitiated = 0;
unsigned long lastTransmissionTime = 0;
unsigned long lastStableReading = 0;

// Distance measurement
long measureProximity() {
  digitalWrite(distanceTrigger, LOW);
  delayMicroseconds(2);
  digitalWrite(distanceTrigger, HIGH);
  delayMicroseconds(10);
  digitalWrite(distanceTrigger, LOW);
  long durationValue = pulseIn(distanceEcho, HIGH, 30000);
  if (durationValue == 0) return 9999;
  return durationValue * 0.0343 / 2.0;
}

// Data transmission
void transmitData(String payload) {
  LoRa.beginPacket();
  LoRa.print(payload);
  LoRa.endPacket();
  Serial.println("Sent: " + payload);
}

// LoRa setup
void setupWireless() {
  LoRa.setSignalBandwidth(125E3);
  LoRa.setSpreadingFactor(9);
  LoRa.setCodingRate4(7);
  LoRa.setSyncWord(0x34);
}

// Setup function
void setup() {
  pinMode(distanceTrigger, OUTPUT);
  pinMode(distanceEcho, INPUT);

  Serial.begin(115200);
  
  LoRa.setPins(LORA_SELECT_PIN, LORA_RESET_PIN, LORA_INTERRUPT_PIN);
  if (!LoRa.begin(433E6)) {
    Serial.println("LoRa failed");
    while (1);
  }
  
  setupWireless();
  Serial.println("Secondary Unit Ready");
}

// Main loop
void loop() {
  // Process incoming commands
  if (LoRa.parsePacket()) {
    String commandData = "";
    while (LoRa.available()) {
      commandData += (char)LoRa.read();
    }
    
    if (commandData == "COMMENCE") {
      sessionActive = true;
      currentMode = WAITING;
      detectionInitiated = 0;
      restInitiated = 0;
      lastTransmissionTime = 0;
      Serial.println("Session active");
    } else if (commandData == "TERMINATE") {
      if (currentMode == PAUSED) {
        unsigned long restDuration = millis() - restInitiated;
        transmitData("PAUSE_DATA:" + String(restDuration));
        Serial.print("Final rest: "); Serial.print(restDuration/1000.0); Serial.println("s");
      }
      sessionActive = false;
      Serial.println("Session ended");
    }
  }

  if (!sessionActive) {
    delay(100);
    return;
  }

  // Read sensor data
  long proximityValue = measureProximity();
  unsigned long currentTime = millis();
  bool objectDetected = (proximityValue >= DETECTION_LOW && proximityValue <= DETECTION_HIGH);

  // State machine
  switch (currentMode) {
    case WAITING:
      if (objectDetected) {
        currentMode = FOUND;
        detectionInitiated = currentTime;
        lastStableReading = currentTime;
        Serial.println("Turn detected");
        
        // Send signal with cooldown check
        if (currentTime - lastTransmissionTime > SIGNAL_COOLDOWN) {
          transmitData("TURN_SIGNAL");
          lastTransmissionTime = currentTime;
          Serial.println("Signal sent");
        }
      }
      break;
      
    case FOUND:
      if (objectDetected) {
        lastStableReading = currentTime;
        // Check for transition to rest state
        if (currentTime - detectionInitiated >= REST_THRESHOLD) {
          currentMode = PAUSED;
          restInitiated = currentTime;
          Serial.println("Rest state active");
        }
      } else {
        // Check if swimmer left
        if (currentTime - lastStableReading > DETECTION_DEBOUNCE) {
          currentMode = LEFT;
          Serial.println("Swimmer left");
        }
      }
      break;
      
    case PAUSED:
      if (objectDetected) {
        lastStableReading = currentTime;
        // Still resting
      } else {
        // Check if swimmer left
        if (currentTime - lastStableReading > DETECTION_DEBOUNCE) {
          // Rest period ended
          unsigned long restPeriod = currentTime - restInitiated;
          transmitData("PAUSE_DATA:" + String(restPeriod));
          Serial.print("Rest time: "); Serial.print(restPeriod/1000.0); Serial.println("s");
          
          currentMode = LEFT;
          Serial.println("Swimmer left during rest");
        }
      }
      break;
      
    case LEFT:
      if (objectDetected) {
        // Swimmer returned
        currentMode = FOUND;
        detectionInitiated = currentTime;
        lastStableReading = currentTime;
        Serial.println("Swimmer returned");
        
        // Send signal if cooldown expired
        if (currentTime - lastTransmissionTime > SIGNAL_COOLDOWN) {
          transmitData("TURN_SIGNAL");
          lastTransmissionTime = currentTime;
          Serial.println("Return signal sent");
        }
      } else {
        // Stay in LEFT state
        if (currentTime - lastStableReading > 5000) {
          currentMode = WAITING;
          Serial.println("Back to waiting");
        }
      }
      break;
  }

  delay(150);
}
