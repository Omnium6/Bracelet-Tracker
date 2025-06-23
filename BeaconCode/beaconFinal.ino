// include libraries
#include <WiFi.h>    
#include <WiFiClientSecure.h> 
#include <FirebaseClient.h>   
#include "BLEDevice.h"       

// network and Firebase credentials
#define WIFI_SSID "XXX"
#define WIFI_PASSWORD "XXX"

#define WEB_API_KEY "XXX" 
#define DATABASE_URL "XXX"
#define USER_EMAIL "XXX"
#define USER_PASS "XXX"

// Define BLE characteristics
static BLEUUID serviceUUID("6e400001-b5a3-f393-e0a9-e50e24dcca9e");
static BLEUUID charUUID("6e400003-b5a3-f393-e0a9-e50e24dcca9e");

static BLERemoteCharacteristic *pRemoteCharacteristic = nullptr;  // Pointer to the BLE remote characteristic, initialized to null
static BLEAdvertisedDevice *myDevice = nullptr; // Pointer to the discovered BLE advertised device, initialized to null

static boolean doConnect = false;  
static boolean connected = false;  
static boolean doScanBLE = true;     
volatile bool LEDOn = false;         

// Counter for reconection attempts
int scanCounter = 0; 

volatile bool unexpectedDisconnectOccurred = false;

// Firebase init components
UserAuth user_auth(WEB_API_KEY, USER_EMAIL, USER_PASS);
FirebaseApp app;
WiFiClientSecure ssl_client;
using AsyncClient = AsyncClientClass;
AsyncClient aClient(ssl_client);
RealtimeDatabase Database;

// Statemachin deffenitions
enum OpState {
  STATE_INIT_BLE,
  STATE_BLE_SCANNING,
  STATE_BLE_CONNECTING,
  STATE_BLE_LISTENING,
  STATE_STOPPING_BLE,
  STATE_WIFI_ACTIVATING,
  STATE_FIREBASE_UPLOADING,
  STATE_AWAITING_FIREBASE_ACK,
  STATE_WIFI_DEACTIVATING
};
OpState currentState = STATE_INIT_BLE; // Begining state


volatile bool bleMessageForProcessing = false;
String messageToUploadToFirebase = "";

// WIFI and firebase
unsigned long wifiConnectStartTime = 0;
const unsigned long WIFI_CONNECT_TIMEOUT_MS = 20000; // 20 sekunder timeout
volatile bool firebaseUploadOperationDone = false;
volatile bool firebaseUploadSuccess = false;
bool wifiAttemptStarted = false; // Flag for at styre WiFi-forbindelsesforsøg

// LED control
unsigned long ledTurnedOnTimestamp = 0;


// BLE callback clases and functions
class MyAdvertisedDeviceCallbacks : public BLEAdvertisedDeviceCallbacks {
  void onResult(BLEAdvertisedDevice advertisedDevice) { //When device is found
    if (advertisedDevice.haveServiceUUID() && advertisedDevice.isAdvertisingService(serviceUUID)) { //If device advertising is the correct device
      Serial.println("BLE Scan: Vores målenhed fundet!");
      BLEDevice::getScan()->stop();  //Stop scanning

      if (myDevice != nullptr) { // if its not the first time connection established
        delete myDevice;         // Delte previous device
        myDevice = nullptr;
      }
      myDevice = new BLEAdvertisedDevice(advertisedDevice); //Add new device
      doConnect = true;    
      doScanBLE = false;   
    }
  }
};

class MyClientCallback : public BLEClientCallbacks {
  void onConnect(BLEClient *pclient) { //when conncetion is made
    Serial.println("BLE Klient: Forbundet til server.");
    doScanBLE = false; //Stop scanning
    scanCounter = 0; 
    connected = true; 
  }

  void onDisconnect(BLEClient *pclient) { //when disconnected
    Serial.println("BLE Klient: Forbindelse tabt.");
    connected = false;
    pRemoteCharacteristic = nullptr; 
    
    // Check if lost connection is expected
    if (!bleMessageForProcessing) {
      Serial.println(">>> Uventet forbindelsestab detekteret! Sætter flag.");
      unexpectedDisconnectOccurred = true;
    } else {
      Serial.println(">>> Forventet forbindelsestab (efter modtagelse af 'y').");
    }
    doScanBLE = true; //After disconnect start scanning
  }
};

MyAdvertisedDeviceCallbacks g_advertisedDeviceCallbacks;
MyClientCallback g_clientCallbacks;             

// Callback when message is recived
static void notifyCallback(BLERemoteCharacteristic *pBLERemoteCharacteristic_cb, uint8_t *pData, size_t length, bool isNotify) {
  String dataStr = ""; //create string for storing value
  for (int i = 0; i < length; i++) { dataStr += (char)pData[i]; } //Read in data from pData to dataStr
  Serial.print("BLE Notify: Modtaget data: "); Serial.println(dataStr);

  if (!bleMessageForProcessing && length == 1 && dataStr == "y") { //If message not previously been recived and message == 'y'
    Serial.println("BLE Notify: Besked 'y' modtaget!");
    messageToUploadToFirebase = dataStr; //Set recived message to the one being uploaded to firebase
    bleMessageForProcessing = true;       
    
    digitalWrite(LED_BUILTIN, HIGH);   //LED for debugging
    LEDOn = true;
    ledTurnedOnTimestamp = millis();     
}

// Firebase Callback functions
void processData(AsyncResult &aResult) {
  if (!aResult.isResult()) {
    firebaseUploadSuccess = false; 
    firebaseUploadOperationDone = true;
    Firebase.printf("Firebase CB: Intet resultat? UID: %s\n", aResult.uid().c_str());
    return;
  }
  
  if (aResult.isError()) {
    Firebase.printf("Firebase Fejl: UID: %s, msg: %s, code: %d\n", aResult.uid().c_str(), aResult.error().message().c_str(), aResult.error().code());
    firebaseUploadSuccess = false;
  } else {
    Firebase.printf("Firebase Data: UID: %s, Payload: %s, succes.\n", aResult.uid().c_str(), aResult.c_str());
    firebaseUploadSuccess = true;
  }
  firebaseUploadOperationDone = true; 
}

// BLE and WIFI functions
bool connectToServer() { //Function for connecting to BLE
  if (myDevice == nullptr) { // if it is a nullptr means no device has been defined and therfore can't be conected to
    Serial.println("BLE Connect: Fejl - myDevice er null.");
    return false;
  }
  Serial.print("BLE: Forsøger at danne forbindelse til ");
  Serial.println(myDevice->getAddress().toString().c_str());

  BLEClient *pClient = BLEDevice::createClient(); 
  Serial.println("BLE: - Klient oprettet");
  pClient->setClientCallbacks(&g_clientCallbacks);

  if (!pClient->connect(myDevice)) { //if the client can not connect to myDevice
    Serial.println("BLE: - Kunne ikke forbinde til server.");
    delete pClient; //Remove client to avoid errors
    return false;
  }
  // 'connected' flaget sættes nu i onConnect callback for at være præcis
  // Serial.println("BLE: - Forbundet til server"); (Flyttet til onConnect)

  if(pClient->setMTU(250)){ //MTU Maximum Transmission Unit, the largest message which can be sent
    Serial.println("BLE: - MTU sat til 250");
  } else {
    Serial.println("BLE: - Kunne ikke sætte MTU.");
  }

  BLERemoteService *pRemoteService = pClient->getService(serviceUUID); //Tries to obtain ServiceUUID
  if (pRemoteService == nullptr) {
    Serial.print("BLE: - Kunne ikke finde service UUID: "); Serial.println(serviceUUID.toString().c_str());
    pClient->disconnect(); delete pClient; return false; //Disconnect and remove client
  }
  Serial.println("BLE: - Service fundet");

  pRemoteCharacteristic = pRemoteService->getCharacteristic(charUUID); 
  if (pRemoteCharacteristic == nullptr) {
    Serial.print("BLE: - Kunne ikke finde karakteristik UUID: "); Serial.println(charUUID.toString().c_str());
    pClient->disconnect(); delete pClient; return false;
  }
  Serial.println("BLE: - Karakteristik fundet");

  if (pRemoteCharacteristic->canNotify()) { //Checks if notfications are supported
    pRemoteCharacteristic->registerForNotify(notifyCallback); 
    Serial.println("BLE: - Registreret for notifikationer.");
  } else {
     Serial.println("BLE: - Karakteristik understøtter ikke notifikationer.");
     pClient->disconnect(); delete pClient; return false; 
  }
  
  return true;
}

void startBLE() {
  Serial.println("System: Starter BLE...");
  if (!BLEDevice::getInitialized()) {  //If BLE hs not been initialized initialize
    BLEDevice::init("");
  }
  BLEScan *pBLEScan = BLEDevice::getScan();
  if (pBLEScan == nullptr) { //Check if device was found in scan
      Serial.println("System: Fejl - Kunne ikke hente BLE scanner objekt.");
      return; 
  }
  pBLEScan->setAdvertisedDeviceCallbacks(&g_advertisedDeviceCallbacks);
  pBLEScan->setInterval(1349);
  pBLEScan->setWindow(449);
  pBLEScan->setActiveScan(true);
  connected = false; 
  doConnect = false;
  doScanBLE = true;   
  unexpectedDisconnectOccurred = false; // Nulstil flag ved hver BLE-start
  Serial.println("System: BLE initialiseret, klar til scanning.");
}

void stopBLE() { //Function to stop BLE so WIFI can be properly initialized
  Serial.println("System: Stopper BLE...");
  if (BLEDevice::getInitialized()) { 
    BLEScan* pScan = BLEDevice::getScan(); 
    if (pScan != nullptr) {
      pScan->stop();
      Serial.println("BLE scan stop-kommando sendt.");
    }
    BLEDevice::deinit(true); 
    Serial.println("BLE de-initialiseret.");
  }
  connected = false;
  doConnect = false;
  doScanBLE = false;
  pRemoteCharacteristic = nullptr; 
  if (myDevice != nullptr) {
    delete myDevice;
    myDevice = nullptr;
  }
}

void startWiFi() {
  Serial.println("System: Starter WiFi...");
  WiFi.mode(WIFI_STA); //Sets ESP to station mode so it can connect to an acces point
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD); 
  wifiConnectStartTime = millis(); 
  Serial.println("System: WiFi.begin() kaldt.");
}

void stopWiFi() { //Stops wifi so BLE can run properly
  Serial.println("System: Stopper WiFi...");
  if (WiFi.isConnected()) {
    WiFi.disconnect(true); 
    Serial.println("WiFi disconnected.");
  }
  WiFi.mode(WIFI_OFF); 
  Serial.println("WiFi radio slukket.");
}


// ----- SETUP -----
void setup() {
  Serial.begin(115200);
  Serial.println("\nSystem: Starter ESP32...");

  pinMode(LED_BUILTIN, OUTPUT);
  digitalWrite(LED_BUILTIN, LOW); 

  Serial.println("Firebase: Forbereder komponenter...");
  ssl_client.setInsecure(); //This makes it so SSL certificates are not nedded for easier portotyping
  initializeApp(aClient, app, getAuth(user_auth), processData, "authTask");
  app.getApp<RealtimeDatabase>(Database);
  Database.url(DATABASE_URL);
  Serial.println("Firebase: Komponenter forberedt.");

  currentState = STATE_INIT_BLE; 
  Serial.println("System: Setup fuldført. Går til tilstandsmaskine.");
}

// ----- LOOP -----
void loop() {
  if (currentState == STATE_AWAITING_FIREBASE_ACK || 
      (currentState == STATE_FIREBASE_UPLOADING && WiFi.status() == WL_CONNECTED && !app.ready())) {
    if (WiFi.status() == WL_CONNECTED) {
        app.loop(); 
    }
  }
  //State machine for controlling code run through
  switch (currentState) { 
    case STATE_INIT_BLE:
      Serial.println("Tilstand: STATE_INIT_BLE");
      startBLE(); 
      currentState = STATE_BLE_SCANNING;
      break;

    case STATE_BLE_SCANNING:
    {
      static unsigned long lastScanCycleStartTime = 0; 
      const unsigned long PAUSE_BETWEEN_SCAN_CYCLES_MS = 2000;

      if (doConnect) { //Callback function has found a device to connect to
        Serial.println("BLE Scan: Enhed fundet via callback, går til STATE_BLE_CONNECTING.");
        currentState = STATE_BLE_CONNECTING;
      } else if (doScanBLE) {
        Serial.println("BLE Scan: 'doScanBLE' er true. Starter ny 5-sekunders scanning...");
        // Din løsning med genstart efter et mislykket scanningsforsøg
        if(scanCounter >= 1) { // The ESP can have trouble reconnecting to devices so it will restart after too many failed attempts which solves the issue
          Serial.println("Kunne ikke genforbinde efter et forsøg. Genstarter ESP for at sikre ren tilstand...");
          ESP.restart();
        }
        scanCounter++;
        
        BLEScan* pBLEScan_instance = BLEDevice::getScan();
        if(pBLEScan_instance != nullptr) {
            pBLEScan_instance->start(5, false); //Passive scan for 5 seconds for devices
        } else {
            Serial.println("BLE Scan: Fejl - Kunne ikke hente scanner objekt. Geninitialiserer BLE.");
            currentState = STATE_INIT_BLE;
        }
        doScanBLE = false;
        lastScanCycleStartTime = millis();
      } else {
        if (millis() - lastScanCycleStartTime > (5000 + PAUSE_BETWEEN_SCAN_CYCLES_MS)) {
          Serial.println("BLE Scan: Scanningscyklus afsluttet uden resultat. Sætter doScanBLE for ny scanning.");
          doScanBLE = true;
        }
      }
      break;
    }

    case STATE_BLE_CONNECTING:
      Serial.println("Tilstand: STATE_BLE_CONNECTING");
      if (connectToServer()) { 
        // Succes håndteres af onConnect callback, som sætter 'connected = true'.
        // Vi går direkte til LISTENING for at vente på, at flaget bliver sandt.
        currentState = STATE_BLE_LISTENING;
      } else {
        Serial.println("BLE Connect: Forbindelsesforsøg mislykkedes.");
        doScanBLE = true; 
        currentState = STATE_BLE_SCANNING;
      }
      doConnect = false; 
      break;

    case STATE_BLE_LISTENING: //here in normal operation when connected to device but nothing has been recived
      // Førsteprioritet: Håndter en modtaget 'y' besked
      if (bleMessageForProcessing) { //If a message has been recived stop BLE
        Serial.println("BLE Listen: Besked modtaget, skifter til STATE_STOPPING_BLE for at uploade.");
        currentState = STATE_STOPPING_BLE;
        break; // Gå ud af casen med det samme
      }

      // Andenprioritet: Tjek om forbindelsen er tabt
      if (!connected) { 
        Serial.println("BLE Listen: Forbindelse tabt.");
        
        // NY LOGIK: Tjek om det var uventet
        if (unexpectedDisconnectOccurred) { //If connection was lost unexpectedly i.e. the user left the range of the beacon
            messageToUploadToFirebase = "Connection_Lost"; //Sets messgae for firebase
            bleMessageForProcessing = true;
            unexpectedDisconnectOccurred = false; //reset trigger flag
            
            currentState = STATE_STOPPING_BLE; //Stop BLE to init wifi
        } else { //Lost connection was not unexpected restart scanning
            doScanBLE = true;
            currentState = STATE_BLE_SCANNING;
        }

        if (LEDOn) { //debugging led
            digitalWrite(LED_BUILTIN, LOW); LEDOn = false;
        }
        break; 
      }
      break;

    case STATE_STOPPING_BLE:
      Serial.println("Tilstand: STATE_STOPPING_BLE");
      stopBLE();
      currentState = STATE_WIFI_ACTIVATING;
      break;

    case STATE_WIFI_ACTIVATING:
    {
      if (!wifiAttemptStarted) { //if it has not yet been attempted to start wifi
        Serial.println("Tilstand: STATE_WIFI_ACTIVATING - Starter WiFi-forbindelsesforsøg...");
        startWiFi(); 
        wifiAttemptStarted = true;
      }

      wl_status_t currentWiFiStatus = WiFi.status(); //Cheks if wifi has been connected

      if (currentWiFiStatus == WL_CONNECTED) { //If wifi is connected go to next state
        Serial.println("WiFi Activate: Forbundet til WiFi!");
        Serial.print("IP Adresse: "); Serial.println(WiFi.localIP());
        currentState = STATE_FIREBASE_UPLOADING;
        wifiAttemptStarted = false; 
      } else if (currentWiFiStatus == WL_CONNECT_FAILED || currentWiFiStatus == WL_CONNECTION_LOST) {
        Serial.print("WiFi Activate: Forbindelse fejlede/mistet. Går tilbage til BLE-tilstand.");
        stopWiFi();
        if (LEDOn) { digitalWrite(LED_BUILTIN, LOW); LEDOn = false; }
        bleMessageForProcessing = false;
        currentState = STATE_INIT_BLE;
        wifiAttemptStarted = false; 
      } else if (wifiAttemptStarted && (millis() - wifiConnectStartTime > WIFI_CONNECT_TIMEOUT_MS)) {
        Serial.println("WiFi Activate: Timeout! Kunne ikke forbinde til WiFi.");
        stopWiFi(); 
        if (LEDOn) { digitalWrite(LED_BUILTIN, LOW); LEDOn = false; } 
        bleMessageForProcessing = false; 
        currentState = STATE_INIT_BLE; 
        wifiAttemptStarted = false; 
      }
      break;
    }

    case STATE_FIREBASE_UPLOADING:
      Serial.println("Tilstand: STATE_FIREBASE_UPLOADING");
      if (WiFi.status() == WL_CONNECTED && app.ready()) {
        firebaseUploadOperationDone = false; 
        firebaseUploadSuccess = false;
        
        String firebaseSti; // select correct path according to the recived messgae
        if (messageToUploadToFirebase == "y") {
            firebaseSti = "/users/user1/signal"; // path for normal alarm
        } else { 
            firebaseSti = "/users/user1/status"; // path if connection lost unexpected
        }
        
        Database.set<String>(aClient, firebaseSti, messageToUploadToFirebase, processData, "RTDB_Event"); //Upload message to firebase
        currentState = STATE_AWAITING_FIREBASE_ACK;
      } else if (WiFi.status() != WL_CONNECTED) {
        Serial.println("Firebase Upload: WiFi ikke forbundet! Går tilbage for at aktivere WiFi.");
        wifiAttemptStarted = false;
        currentState = STATE_WIFI_ACTIVATING; 
      } else {
         Serial.println("Firebase Upload: Firebase app ikke klar endnu. Venter...");
         if(WiFi.status() == WL_CONNECTED) app.loop();
         delay(200);
      }
      break;

    case STATE_AWAITING_FIREBASE_ACK: //wait for conformation that message has been recived by firebase
      if (firebaseUploadOperationDone) {
        if (firebaseUploadSuccess) {
          Serial.println("Firebase ACK: Upload succesfuld!");
        } else {
          Serial.println("Firebase ACK: Upload mislykkedes.");
        }
        bleMessageForProcessing = false;
        currentState = STATE_WIFI_DEACTIVATING;
      } else if (WiFi.status() != WL_CONNECTED) {
         Serial.println("Firebase ACK: WiFi tabt mens der ventes på svar");
         firebaseUploadOperationDone = true;
         firebaseUploadSuccess = false;   
      }
      break;

    case STATE_WIFI_DEACTIVATING: //deactivate wifi so BLE can be reconnected
      Serial.println("Tilstand: STATE_WIFI_DEACTIVATING");
      stopWiFi();
      
      if (LEDOn) {
        unsigned long ledOnDuration = millis() - ledTurnedOnTimestamp;
        if (ledOnDuration < 1000) {
          delay(1000 - ledOnDuration); 
        }
        digitalWrite(LED_BUILTIN, LOW);
        LEDOn = false;
        Serial.println("LED: Slukket efter Firebase cyklus.");
      }
      currentState = STATE_INIT_BLE; 
      break;

    default:
      Serial.println("Ukendt tilstand! Nulstiller til STATE_INIT_BLE.");
      stopWiFi();
      stopBLE(); 
      currentState = STATE_INIT_BLE;
      break;
  }
  delay(50);
}