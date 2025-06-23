#include <Arduino.h>
#include <rtos.h>

//EPD%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
#include "GUI_Paint.h"
#include "DEV_Config.h"
#include "EPD_1in02d.h"
#include "fonts.h"
#include "imagedata.h"

//FALD Detektion%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
#include <Arduino_BMI270_BMM150.h> // IMU Sensor Library for Arduino Nano 33 BLE Rev.2
#include <math.h>

// BlueTooth%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
#include <ArduinoBLE.h>

//Farver%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
#define EPD_WHITE 0x00
#define EPD_BLACK 0xff

// char array for display img and display size
unsigned char image_temp[1280]={0};
Paint paith(image_temp, 80, 128);
//%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%

//Time Handling %%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
unsigned long myTime;
unsigned long timeDelay;
unsigned int second;
unsigned int hour;
unsigned int minute;
unsigned int previousMinute;
unsigned long offset; //Used for setting time when program is powered up
unsigned int compileHour;
unsigned int compileMinute;
unsigned int compileSecond;
// save time as char arrray so it can be printed to display
char hour_str[5];
char minute_str[5];

// BLE variabler%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
// Define pointers for BLE variables
BLEService* pCustomService;
BLECharacteristic* pTextCharacteristic; //Sending message

BLECharacteristic* pTimeSetCharacteristic; // recive time
const char* TIME_SET_CHAR_UUID = "6e400002-b5a3-f393-e0a9-e50e24dcca9e";

static bool messageSent = false;

volatile bool timeRecived = false;

//Fald Detektion%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
// --- define threshold values ---
const float FREEFALL_THRESHOLD_G = 0.5;
const float IMPACT_THRESHOLD_G = 2.0;

// constants for fall algortihm
const unsigned long IMPACT_WINDOW_MS = 1000;
const unsigned long POST_IMPACT_DELAY_MS = 1000;
const unsigned long INACTIVITY_MEASUREMENT_DURATION_MS = 1200;
const float INACTIVITY_SVM_MIN_G = 0.75;
const float INACTIVITY_SVM_MAX_G = 1.35;

const int alarmDelay = 15000;

// variables for fall detection state machine
enum DetectionState {
  STATE_IDLE,
  STATE_FREEFALL_DETECTED,
  STATE_IMPACT_DETECTED_DELAY,
  STATE_INACTIVITY_ANALYSIS
};
DetectionState currentState = STATE_IDLE;
unsigned long eventTimer = 0;


float accX, accY, accZ; // save data from IMU

// Variabel til at gemme tidspunktet for, hvornår knappen sidst blev trykket ned
volatile unsigned long buttonPressStartTime = 0; 

// Flag til at indikere, om knappen er trykket ned
volatile bool buttonPressed = false; 

// Flag til at sikre, at "ja" kun udskrives én gang per tryk
volatile bool printedOnce = false;

// Tærskel for hvor længe knappen skal holdes nede (2000 millisekunder = 2 sekunder)
const unsigned long PRESS_DURATION_THRESHOLD = 2000; 

volatile bool fallConfirmed; // If true activates alarm

   
const int buttonIntPin = 3;  
const int buzzerPin = 5;   



// variables to save alarm time and state
unsigned long alarmStartTime = 0;
bool alarmIsActive = false;


void setup() {

  fallConfirmed= false;
  setupEPD();
  setupBLE();
  setupFall();
  getCompileTime();
  calculateTimeOffset(compileHour, compileMinute, compileSecond);
  diablePins();
}

void loop() {
  BLE.poll(); // lets connected devices know connection is still available
  if (checkFall()) { //Runs fall detection algorithm
    fallConfirmed = true;
  }

  if (fallConfirmed && !alarmIsActive) {
    // A fall has just been confirmed activates alarm
    EPDAlarm();
    alarmIsActive = true;
    alarmStartTime = millis();    // save alarm start time
  }

  // logic making it so alarm notfication is only sent after alarmDelay has passed
  if (alarmIsActive) {
    if (millis() - alarmStartTime > alarmDelay) {
      if (fallConfirmed) { 
        sendMessageBLE();
      }
      alarmIsActive = false; 
    }
  }

// Ensures button is pressed for a duration before it activates or deactivates alarm for fewer accidental presses
  if (buttonPressed && (millis() - buttonPressStartTime >= PRESS_DURATION_THRESHOLD) && !printedOnce) {
    printedOnce = true;
      if (alarmIsActive) { //Cheks if alarm should be enabled or disabled
        fallConfirmed = false;
        alarmIsActive = false;
        digitalWrite(buzzerPin, LOW);
        messageSent = false; // Cancels sending message
        updateDisplayTime(); // Updates display with time
        
      } else {
        //Alarm has been started by user
        fallConfirmed = true; 
      }
  }

  calculateTime(); 
  if (previousMinute != minute) { // Updates time if time has passed
    if(!fallConfirmed) {
      updateDisplayTime();
    }
    previousMinute = minute;
  }
  delay(50);
}

