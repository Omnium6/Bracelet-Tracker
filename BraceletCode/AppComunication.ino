void setupBLE() {
  //Creates BLE objects
  pCustomService = new BLEService("6e400001-b5a3-f393-e0a9-e50e24dcca9e");
  pTextCharacteristic = new BLECharacteristic("6e400003-b5a3-f393-e0a9-e50e24dcca9e", BLERead | BLENotify, 10);
  pTimeSetCharacteristic = new BLECharacteristic(TIME_SET_CHAR_UUID, BLEWrite, 8); 

  
  BLE.setEventHandler(BLEConnected, onBLEConnected);        //Sets eventhandler when BLE connection is made
  BLE.setEventHandler(BLEDisconnected, onBLEDisconnected);  //Sets eventhandler when BLE connection is lost



  if (!BLE.begin()) { //Initializes BLE
    while (1);
  }
  
  BLE.setLocalName("FallDetector");
  BLE.setAdvertisedService(*pCustomService);
  pCustomService->addCharacteristic(*pTextCharacteristic);
  pCustomService->addCharacteristic(*pTimeSetCharacteristic);
  BLE.addService(*pCustomService);

  pTimeSetCharacteristic->setEventHandler(BLEWritten, onTimeSetWritten); // evnet handler for when time is recived


  // Start advertising
  BLE.advertise();
}

void sendMessageBLE() { //Function for sending a message using BLE
  BLEDevice central = BLE.central(); //sets central device

  if (central) { //Cheks if a central is pressent
    while (central.connected()){ //Make sure central is ready to recive message

      if (!messageSent && pTextCharacteristic->subscribed()) { //If messge has not been sent and device is listening
        pTextCharacteristic->writeValue((uint8_t)'y');  // Sends message 'y' to connected device
        messageSent = true;
      } 
      if (messageSent) {
        break; //Exits while loop when message has been sent
      }
    } 
  }
}

void onBLEConnected(BLEDevice central) { // was used for debugging
  // Denne funktion kører, når en enhed forbinder
  //Serial.print("Enhed forbundet med succes: ");
  //Serial.println(central.address());
  //central.adress();
}

void onBLEDisconnected(BLEDevice central) { //when connection is lost start advertising again
  BLE.advertise();
  messageSent = false;
}


void onTimeSetWritten(BLEDevice central, BLECharacteristic characteristic) { //Called when time is recived from device
  timeRecived = true;

  const uint8_t* data = characteristic.value(); //read recived value
  int len = characteristic.valueLength();
  
  //Converts time to string so it can be written to display
  char timeStr[len + 1]; 
  memcpy(timeStr, data, len);
  timeStr[len] = '\0'; 

  int receivedHour = -1, receivedMinute = -1, receivedSecond = -1;
  if (sscanf(timeStr, "%d.%d.%d", &receivedHour, &receivedMinute, &receivedSecond) == 3) { //If all 3 time types are loaded continue
    
    // Check if parsed values are possible
    if (receivedHour >= 0 && receivedHour <= 23 && 
        receivedMinute >= 0 && receivedMinute <= 59 &&
        receivedSecond >= 0 && receivedSecond <= 59) {
      
      calculateTimeOffset(receivedHour, receivedMinute, receivedSecond); //Change time offset to the new time
      
      calculateTime(); 
      previousMinute = minute;
      
      if (!fallConfirmed && !alarmIsActive) { //If fall is not active update display  with new time
          updateDisplayTime();
      }
    }
  } 
}
