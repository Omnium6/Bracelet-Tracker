float calculateSVM(float x, float y, float z) { //Calculates the length of acceleration vector
  return sqrt(x * x + y * y + z * z);
}

void setupFall() {
  pinMode(buttonIntPin, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(buttonIntPin), buttonISR, CHANGE);
  IMU.begin();
}

void buttonISR() { //Function run when button state is changed
  int buttonState = digitalRead(buttonIntPin); 

  if (buttonState == LOW) {
    buttonPressStartTime = millis(); // save time button was pressed
    buttonPressed = true;        
    printedOnce = false;         
  } 
  else { //If button is no longer pressed 
    buttonPressed = false;
  }
}

// Function for fall detection
bool checkFall() {
  if (IMU.accelerationAvailable()) {
    IMU.readAcceleration(accX, accY, accZ);
  } 
  else { 
    return false; //Exits funtion if IMU is unavailable
  }
  float svmAccel = calculateSVM(accX, accY, accZ); //Save total acceleration vector length

  switch (currentState) { //State machine for fall detection
    case STATE_IDLE:
      if (svmAccel < FREEFALL_THRESHOLD_G) { //if acc is lower than this threshold a free fall is suspected indicating start of fall
        currentState = STATE_FREEFALL_DETECTED;
        eventTimer = millis(); // time saved as impact has to come within a certain time
      }
      break; // Exits fall function if no free fall is detected

    case STATE_FREEFALL_DETECTED:
      if (svmAccel > IMPACT_THRESHOLD_G) { //Looks for an acceleration greater than  IMPACT_THRESHOLD_G indicting user has hit the ground
        currentState = STATE_IMPACT_DETECTED_DELAY;
        eventTimer = millis();
      } else if (millis() - eventTimer > IMPACT_WINDOW_MS) { //If impact is not detected within time window stae returns to idle
        currentState = STATE_IDLE;
      }
      break;

    case STATE_IMPACT_DETECTED_DELAY: // The function waits for a time window for the fall to be over
      if (millis() - eventTimer >= POST_IMPACT_DELAY_MS) {
        currentState = STATE_INACTIVITY_ANALYSIS;
        eventTimer = millis();
      }
      break;

    case STATE_INACTIVITY_ANALYSIS: // The fall is assumed to be over and now little movement is expected if a fall has occured

      if (svmAccel < INACTIVITY_SVM_MIN_G || svmAccel > INACTIVITY_SVM_MAX_G) {
        currentState = STATE_IDLE;
      }
      else if (millis() - eventTimer >= INACTIVITY_MEASUREMENT_DURATION_MS) { //If to much movement is detected the fall is canceled 
        currentState = STATE_IDLE;
        return true;
      }
      break;
  }
  return false;
}
