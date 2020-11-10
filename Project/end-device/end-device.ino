#include <SPI.h>
#include <LoRa.h>
#include <Arduino_FreeRTOS.h>

#define SCK 15
#define MISO 14
#define MOSI 16
#define SS 8
#define RST 4
#define DI0 7
#define BAND 8693E5
#define PABOOST true


void TaskMonitorRadio(void *pvParameters);
void TaskMonitorSerialPort(void *pvParameters);

bool killMonitorSerialPort = false;

void setup() {
  Serial.begin(9600);
  while (!Serial);

  xTaskCreate(TaskMonitorRadio, "Monitor LoRa", 128, NULL, 0, NULL);
  xTaskCreate(TaskMonitorSerialPort, "Monitor Serial Port", 128, NULL, 0, NULL);
}

void loop() {}

void printLastValues() {
  Serial.println("@printLastValues");
}

void printAllValues() {
  Serial.println("@printAllValues");
}

void enterLowPowerMode() {
  Serial.println("@enterLowPowerMode");
}

void TaskMonitorRadio(void *pvParameters) {
  (void) pvParameters;

  Serial.println("Starting LoRa Receiver");
  LoRa.setPins(SS,RST,DI0);

  if (!LoRa.begin(BAND)) {
    Serial.println("Starting LoRa failed!");
    return;
  }

  for(;;) {
    int packetSize = LoRa.parsePacket();
    if (packetSize) {
      // received a packet
      Serial.print("Received packet '");
  
      // read packet
      while (LoRa.available()) {
        Serial.print((char)LoRa.read());
      }

      Serial.println("'");

      if (!killMonitorSerialPort) {
        killMonitorSerialPort = true;
        Serial.println("Disabled Monitor Serial Port.");
      }
    }
  }
}

void TaskMonitorSerialPort(void *pvParameters) {
  (void) pvParameters;

  for(;;) {
    if (killMonitorSerialPort) {
      vTaskDelete(NULL);
    }
    
    if (Serial.available() > 0) {
      String command = Serial.readString();
      command.trim();

      if (command == "1") {
        printLastValues();
      } else if (command == "2") {
        printAllValues();
      } else if (command == "3") {
        enterLowPowerMode();
      } else {
        Serial.println("Invalid command.");
      }
    }
  }
}
