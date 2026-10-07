#define BLYNK_TEMPLATE_ID "TMPL4n-fKX9oR"
#define BLYNK_TEMPLATE_NAME "Smart Fridge Gadget"
#define BLYNK_AUTH_TOKEN "cprdHUm3yu01R9SLxpSHqoRkQ4QfH8e7"

#define BLYNK_NO_BUILTIN
#define gpioNumberToDigitalPin(p) (p)

char ssid[] = "Pixel 7"; 
char pass[] = "newpasswordhaha123!";

#include <WiFi.h>
#include <WiFiClient.h>
#include <BlynkSimpleEsp32.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include "HX711.h"
#include "HUSKYLENS.h"

const int DT_PIN = 2;  
const int SCK_PIN = 3; 
float calibration_factor = 411.70; 

HX711 scale;
LiquidCrystal_I2C lcd(0x27, 16, 2); 
HUSKYLENS huskylens;

float currentWeight = 0.0; 
float previousTotalWeight = 0.0; 
float preRemoveTotalWeight = 0.0; 
float calculatedItemWeight = 0.0; 

int pendingID = -1; 
int systemState = 0; 
unsigned long stateTimer = 0;
bool isAdding = true; 

int currentGazeID = -1;
unsigned long gazeStartTime = 0;

int lastScannedID = 5; 

float inventory[10] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0}; 
int itemCount[10] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0}; 

String itemNames[10] = {
  "None", "None", "Item 1", "Item 2", "Item 3", "Item 4", "Item 5", "Item 6", "Item 7", "Item 8"
};

String defaultNames[10] = {
  "None", "None", "Item 1", "Item 2", "Item 3", "Item 4", "Item 5", "Item 6", "Item 7", "Item 8"
};

// Send structured inventory list to Blynk Virtual Pin V12 for the website
void sendDataToWebsite() {
  String json = "[";
  bool firstItem = true;
  
  for (int i = 2; i < 10; i++) {
    if (itemCount[i] > 0) {
      if (!firstItem) {
        json += ", "; 
      }
      json += "{\"name\": \"" + itemNames[i] + "\", \"count\": " + String(itemCount[i]) + ", \"weight\": " + String(inventory[i], 0) + "}";
      firstItem = false;
    }
  }
  json += "]";

  Blynk.virtualWrite(V12, json);
  Serial.println("Sent to Website: " + json);
}

void updateApp() {
  for (int i = 2; i < 10; i++) {
    if (inventory[i] > 0 && itemCount[i] > 0) {
      String displayData = String(itemCount[i]) + "x " + itemNames[i] + ": " + String(inventory[i], 0) + "g";
      Blynk.virtualWrite(i, displayData); 
    } else {
      Blynk.virtualWrite(i, " "); 
    }
  }

  sendDataToWebsite();
}

BLYNK_WRITE(V10) {
  String customName = param.asStr(); 
  
  if (lastScannedID >= 2 && lastScannedID <= 9) {
    itemNames[lastScannedID] = customName; 
    updateApp();                       
    Blynk.virtualWrite(V10, ""); 
    
    Serial.print("\n[BLYNK] ID ");
    Serial.print(lastScannedID);
    Serial.print(" renamed to: ");
    Serial.println(customName);
  }
}

BLYNK_WRITE(V11) {
  if (param.asInt() == 1) { 
    lcd.clear();
    lcd.setCursor(0,0);
    lcd.print("HARD RESET...");
    
    scale.tare(); 
    
    for (int i = 2; i < 10; i++) {
      inventory[i] = 0.0; 
      itemCount[i] = 0; 
      itemNames[i] = defaultNames[i]; 
    }
    
    currentWeight = 0.0;
    previousTotalWeight = 0.0; 
    preRemoveTotalWeight = 0.0;
    calculatedItemWeight = 0.0;
    pendingID = -1;
    
    updateApp(); 
    changeState(0); 
    
    Serial.println("\n[SYSTEM] TRUE HARD RESET Triggered!");
  }
}

void printInventoryToSerial(String action) {
  Serial.println("\n==================================");
  Serial.print("ACTION: ");
  Serial.print(action);
  Serial.print(" ");
  Serial.print(itemNames[pendingID]);
  Serial.print(" (");
  Serial.print(calculatedItemWeight, 1);
  Serial.println("g)");
  Serial.println("==================================\n");
}

void changeState(int newState) {
  systemState = newState;
  stateTimer = millis();
  currentGazeID = -1; 
  lcd.clear();
}

void setup() {
  Serial.begin(115200);
  Wire.begin();
  
  lcd.init();
  lcd.backlight();
  
  scale.begin(DT_PIN, SCK_PIN);
  scale.set_scale(calibration_factor); 
  scale.tare(); 
  
  lcd.setCursor(0,0);
  lcd.print("Connecting Wi-Fi");
  Blynk.begin(BLYNK_AUTH_TOKEN, ssid, pass);
  
  lcd.clear();
  lcd.print("Init Camera...");
  while (!huskylens.begin(Wire)) delay(1000);
  
  for(int i = 2; i < 10; i++) Blynk.virtualWrite(i, " ");
  Blynk.virtualWrite(V10, ""); 
  
  changeState(0);
}

void loop() {
  
  Blynk.run(); 
  
  if (scale.is_ready()) {
    currentWeight = scale.get_units(5); 
    if (currentWeight > -5.0 && currentWeight < 5.0) currentWeight = 0.0;
  }

  // --- IDLE ---
  if (systemState == 0) {
    lcd.setCursor(0, 0);
    lcd.print("Total: ");
    lcd.print(currentWeight, 0);
    lcd.print("g     "); 
    
    if (currentWeight < (previousTotalWeight - 40.0)) {
      preRemoveTotalWeight = previousTotalWeight;
      changeState(4); 
    }
    
    else if (currentWeight < 5.0) {
      bool ghostFound = false;
      
      for(int i = 2; i < 10; i++) {
        if (itemCount[i] > 0) ghostFound = true; 
      }
      
      if (ghostFound) {
        for(int i = 2; i < 10; i++) {
          inventory[i] = 0.0;
          itemCount[i] = 0; 
        }
        previousTotalWeight = 0.0;
        updateApp(); 
        Serial.println("\n[AUTO-CLEAR] Scale is empty. Wiped ghost items.");
      }
    }
        
    huskylens.request();
    bool foundValidID = false;
    int seenID = -1;

    while (huskylens.available()) {
      HUSKYLENSResult result = huskylens.read();
      if (result.command == COMMAND_RETURN_BLOCK && result.ID > 1) { 
        foundValidID = true;
        seenID = result.ID;
      }
    }

    if (foundValidID) {
      if (currentGazeID == seenID) {
        if (millis() - gazeStartTime > 1000) { 
          pendingID = seenID;
          changeState(1); 
        }
      } else {
        currentGazeID = seenID;
        gazeStartTime = millis();
      }
    } else {
      currentGazeID = -1;
    }
  } 
  
  // --- WAITING TO ADD ---
  else if (systemState == 1) {
    lcd.setCursor(0, 0);
    lcd.print("Place ");
    lcd.print(itemNames[pendingID]);

    if (currentWeight > (previousTotalWeight + 10.0)) {
      isAdding = true;
      changeState(2); 
    }
    
    if (millis() - stateTimer > 10000) {
      pendingID = -1;
      changeState(0);
    }
  } 
  
  // --- CALCULATING ADD ---
  else if (systemState == 2) {
    lcd.setCursor(0, 0);
    lcd.print("Calculating...  ");
    
    if (millis() - stateTimer > 4000) {
      calculatedItemWeight = abs(currentWeight - previousTotalWeight);
      
      inventory[pendingID] += calculatedItemWeight; // ADD weight
      itemCount[pendingID]++; // INCREASE count
      
      lastScannedID = pendingID; 
      
      updateApp(); 
      printInventoryToSerial("[+] ADDED");
      changeState(3); 
    } 
  } 

  // --- ITEM GRABBED ---
  else if (systemState == 4) {
    lcd.setCursor(0, 0);
    lcd.print("Grabbed! Scan it");

    huskylens.request();
    bool foundValidID = false;
    int seenID = -1;

    while (huskylens.available()) {
      HUSKYLENSResult result = huskylens.read();
      if (result.command == COMMAND_RETURN_BLOCK && result.ID > 1) { 
        foundValidID = true;
        seenID = result.ID;
      }
    }

    if (foundValidID) {
      if (currentGazeID == seenID) {
        if (millis() - gazeStartTime > 1000) { 
          pendingID = seenID;
          isAdding = false; 
          
          calculatedItemWeight = preRemoveTotalWeight - currentWeight;
          
          inventory[pendingID] -= calculatedItemWeight; // SUBTRACT weight
          itemCount[pendingID]--; // DECREASE count
          
          if (inventory[pendingID] < 5.0 || itemCount[pendingID] <= 0) {
             inventory[pendingID] = 0.0;
             itemCount[pendingID] = 0;
          }
          
          updateApp(); 
          printInventoryToSerial("[-] REMOVED");
          changeState(3); 
        }
      } else {
        currentGazeID = seenID;
        gazeStartTime = millis();
      }
    } else {
      currentGazeID = -1;
    }
    
    if (millis() - stateTimer > 10000) {
      previousTotalWeight = currentWeight; 
      changeState(0); 
    }
  }

  // --- SCREEN DISPLAY ---
  else if (systemState == 3) {
    lcd.setCursor(0, 0);
    if (isAdding) lcd.print("+");
    else lcd.print("-");
    
    lcd.print(itemNames[pendingID]); 
    lcd.print(": ");
    lcd.print(calculatedItemWeight, 0); 
    lcd.print("g     ");
    
    if (millis() - stateTimer > 4000) {
      previousTotalWeight = currentWeight; 
      pendingID = -1; 
      changeState(0);
    }
  }
}