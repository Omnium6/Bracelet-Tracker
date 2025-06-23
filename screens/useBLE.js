import React, { useState, useRef } from 'react';
import { atob } from 'react-native-quick-base64';
import { PermissionsAndroid } from 'react-native';
import { BleManager, ScanMode, ScanCallbackType } from 'react-native-ble-plx';
import { Buffer } from 'buffer';
import { doc, updateDoc } from "firebase/firestore";
import { db } from '../firebaseConfig1';
import { updateConnection} from '../database';

// Opretter en ny BLEManger, så BLE kan køre
const bleManager = new BleManager();

// Definerer serivce og karakteristikker - en til at skrive og en til at læse data
const SERVICE_UUID = '6e400001-b5a3-f393-e0a9-e50e24dcca9e';
const SERVICE_UUID_LIST = [SERVICE_UUID];
const WRITE_CHARACTERISTIC_UUID = '6e400002-b5a3-f393-e0a9-e50e24dcca9e';
const CHARACTERISTIC_UUID = '6e400003-b5a3-f393-e0a9-e50e24dcca9e';

// Funktion til at dekode modtagne beskeder, nå de bliver sendt bliver de til en base64 string
function decodeBase64ToString(base64String) {
  try {
    return atob(base64String ?? ''); // atob laver base64 stringen om til en normal string
  } catch (e) {
    console.error('Failed to decode base64:', e);
    return '';
  }
}

export default function useBLE() {
  // Definerer alle konstanterne
  const [isConnected, setIsConnected] = useState(false);
  const [connectedDevice, setConnectedDevice] = useState(null);
  const [message, setMessage] = useState('');
  const [recievedTime, setRecievedTime] = useState(null);
  
  const manuallyDisconnectedRef = useRef(false);
  const manuallyDisconnected = manuallyDisconnectedRef.current;
  const currentDeviceId = useRef('');
  const previousIsConnectedRef = useRef(null);

  // FUnktion der spørger om tilladelse til at bruge telefonens lokation for at BLE kan køre
  async function requestPermissions() {
    const permission = await PermissionsAndroid.request(
      PermissionsAndroid.PERMISSIONS.ACCESS_FINE_LOCATION,
      {
        title: 'Bluetooth Location Permission',
        message: 'App requires location for BLE scanning',
        buttonNeutral: 'Later',
        buttonNegative: 'Cancel',
        buttonPositive: 'OK',
      }
    );

    return permission === PermissionsAndroid.RESULTS.GRANTED;
  }

  // Funktion der scanner efter BLE enheder, scanner efter enheder med navnet "FallDetector"
  async function startDeviceScanWrapper(deviceName = 'FallDetector') {
    const hasPermission = await requestPermissions(); // Kører request om tiladelse
    if (!hasPermission) {
      console.warn('Bluetooth permission denied');
      return;
    }

    console.log('Scanning with filters...');

    // Starter med at scanne
    bleManager.startDeviceScan(
      SERVICE_UUID_LIST,
      {
        allowDuplicates: false,
        callbackType: ScanCallbackType.AllMatches,
        scanMode: ScanMode.LowLatency,
      },
      async (error, device) => {
        if (error) {
          console.warn('Scan error:', error);
          return;
        }

        if (device && device.isConnectable && (device.name === deviceName || device.localName === deviceName)) { // Hvis der er en enhed og der kan forbindes til den og den har det rigtige navn, så skal den:
          const name = device.name ?? device.localName;
          console.log(`Found device: ${name}`);

          if (currentDeviceId.current === device.id) { 
            console.warn('Already connecting to this device. Ignoring...');
            return;
          }

          currentDeviceId.current = device.id;
          bleManager.stopDeviceScan(); // Stopper med at scanne
          await connectDevice(device); // Kalder på at den skal forbinde til enheden
        }
      }
    );

    // Hvis den ikke er blevet forbundet efter 10 sekunder stopper den med at scanne
    setTimeout(() => {
      bleManager.stopDeviceScan();
      console.log('Scan stopped after timeout.');
    }, 10000);
  }

  // Funktion der sender tiden til den forbundne enhed
  async function sendTimeToDevice(device) {
    if (!device) return;

    const now = new Date(); // Henter tiden
    const timeString = now.toLocaleTimeString(); // Sætter tiden til lokaltid
    const base64Time = Buffer.from(timeString).toString('base64'); // Laver stringen om til en base64 string

    // Sender værdien med den rigtige service, karakteristisk
    try {
      await device.writeCharacteristicWithResponseForService(
        SERVICE_UUID,
        WRITE_CHARACTERISTIC_UUID,
        base64Time
      );
      console.log('Tid sendt til Arduino:', timeString);
    } catch (error) {
      console.error('Fejl ved skrivning til Arduino:', error);
    }
  }

  // Funktion til at forbinde til enhed
  async function connectDevice(device) {
    try {
      console.log('Connecting to device:', device.name);
      const connected = await device.connect(); // Forbinder til enhed
      setConnectedDevice(connected);
      if (previousIsConnectedRef.current !== true) {
        setIsConnected(true);
        previousIsConnectedRef.current = true;

        await updateConnection(true);
      }

      // Finde enhedens Service og karakteristiker
      await connected.discoverAllServicesAndCharacteristics();
      await new Promise(resolve => setTimeout(resolve, 500)); // delay på 0.5 sekund
      await sendTimeToDevice(connected); // Sender tid til enhed

      // Kører hvis enheden afbryder forbindelsen
      bleManager.onDeviceDisconnected(connected.id, async () => {
        console.log('Device disconnected');
        setIsConnected(false);
        setConnectedDevice(null);
        currentDeviceId.current = '';

        if (previousIsConnectedRef.current !== false) {
          previousIsConnectedRef.current = false;
          await updateConnection(false);
        }

        // Hvis forbindelsen afbrydes starter den med at scanne igen
        if (!manuallyDisconnectedRef.current) {
          console.log('Forbindelsen tabt, starter scanning igen...');
          startDeviceScanWrapper();
        } else {
          console.log('Manuel frakobling – ingen autoscan.');
        }
      });

      // Holder øje med service og karakteristikker
      connected.monitorCharacteristicForService(
        SERVICE_UUID,
        CHARACTERISTIC_UUID,
        (error, characteristic) => {
          if (error) {
            console.warn('Monitor error:', error);
            return;
          }
          if (characteristic?.value) { // Hvis der kommer en ny værdi blvier den dekodet og gemt
            console.log(characteristic.value)
            const decoded = decodeBase64ToString(characteristic.value); // Dekoder værdien
            console.log('Message update:', decoded);
            setMessage(decoded); // Gemmer den nye værdi

            // Sætter og gemmer den tid værdien er blevet modtaget
            const currentTime = new Date().toLocaleTimeString('da-DK', {
              hour: '2-digit',
              minute: '2-digit',
              hour12: false
            });
            setRecievedTime(currentTime);
          }
        },
        'messagetransaction'
      );

      console.log('Connection established');
    } catch (error) {
      console.error('Connection failed:', error);
    }
  }

  // Funktion til at afbryde forbindelsen manuelt
  async function disconnectDevice() {
    if (connectedDevice) {
      const isDeviceConnected = await connectedDevice.isConnected();
      if (isDeviceConnected) {
        bleManager.cancelTransaction('messagetransaction');
        await bleManager.cancelDeviceConnection(connectedDevice.id); // Afbryder forbindelsen
        console.log('Device manually disconnected');
        setIsConnected(false);
        setConnectedDevice(null);
        manuallyDisconnectedRef.current = true;

        try {
          const fallValue = "No"

          await updateDoc(doc(db, "users", "user1"), { // Updaterer værdien "fall" til "No"
            fall: fallValue,
          });

        console.log("Fall:", fallValue);
      } catch (error) {
        console.log("Fejl ved opdatering:", error);
      }
      }
    }
  }

  // Funktion til at manuelt kunne forbinde igen
  function reconnectDevice() {
    manuallyDisconnectedRef.current = false;
    startDeviceScanWrapper(); // Scanner igen
  }

  return {
    isConnected,
    connectedDevice,
    message,
    recievedTime,
    manuallyDisconnected,
    scanDevices: startDeviceScanWrapper, // Erstatter
    reconnectDevice,
    connectDevice,
    disconnectDevice,
  };
}
