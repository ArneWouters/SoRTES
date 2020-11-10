#include <SPI.h>
#include <LoRa.h>
#include <EEPROM.h>
#include <Arduino_FreeRTOS.h>
#include <semphr.h>

#define SCK 15
#define MISO 14
#define MOSI 16
#define SS 8
#define RST 4
#define DI0 7
#define BAND 8693E5
#define INCLUDE_vTaskSuspend 1

/*********
 * Tasks *
 *********/
 
void TaskMonitorRadio(void *pvParameters);
void TaskMonitorSerialPort(void *pvParameters);


/*************
 * Functions *
 *************/

void printLastValue();
void printAllValues();
void enterLowPowerMode();
int getTemperatureInternal();


/************
 * Database *
 ************/

struct DataBlock {
  int temperature;  // temperature of the internal sensor
  int sleepTime; // delay in seconds for the next beacon
};


/***********
 * Globals *
 ***********/

bool firstPackageReceived = false;
//Database db = Database();
SemaphoreHandle_t SemaphoreHndl;
int address; // address of the last value written


/*********
 * Setup *
 *********/

void setup() {
  EEPROM.put(0, address);
  Serial.begin(9600);
  while (!Serial);

  xTaskCreate(TaskMonitorRadio, "Monitor LoRa", 128, NULL, 0, NULL);
  xTaskCreate(TaskMonitorSerialPort, "Monitor Serial Port", 128, NULL, 0, NULL);

  SemaphoreHndl = xSemaphoreCreateBinary();
  if (SemaphoreHndl != NULL) {
    xSemaphoreGive(SemaphoreHndl);
  }
}

void loop() {}

/***************
 * Definitions *
 ***************/

void storeDataBlock(int temperature, int sleepTime) {
  // check if enough space left, reset addr if not
  if (sizeof(DataBlock) + address > EEPROM.length()) {
    address = 2;
  }

  DataBlock data;
  data.temperature = temperature;
  data.sleepTime = sleepTime;

  Serial.print("Store Address: ");
  Serial.println(address);
  Serial.print(data.temperature);
  Serial.print(" - ");
  Serial.println(data.sleepTime);
 
  EEPROM.put(address, data);
  address += sizeof(data);
  EEPROM.put(0, address);
}

void printDataBlock(int addr) {
  if (addr < 2 || sizeof(DataBlock) + addr > EEPROM.length()) {
      Serial.println("Unable to print entry");
      return;
    }

    DataBlock data;
    EEPROM.get(addr, data);

    Serial.print("Read Address: ");
    Serial.println(addr);
    Serial.print(data.temperature);
    Serial.print(" - ");
    Serial.println(data.sleepTime);
}

void printLastValue() {
  Serial.println("@printLastValue");
  printDataBlock(address-(int)sizeof(DataBlock));
}

void printAllValues() {
  Serial.println("@printAllValues");
//  db.printAll();
//  int address = addr-(int)sizeof(DatabaseEntry);
//
//    while (address >= 2) {
//      printEntry(address);
//      address = address - (int)sizeof(DatabaseEntry);
//    }
}

void enterLowPowerMode() {
  Serial.println("@enterLowPowerMode");
}

void debugFunction() {
  Serial.println("@addRandomData");
  storeDataBlock(random(-100, 101), random(10));
}

int getTemperatureInternal() {
  // For more details check Section 24 in the datasheet
  // Sources: 
  //   - https://syntheticphysical.wordpress.com/2014/01/23/atmega32u2-and-lm36dz-temperature-responses/
  //   - https://www.avrfreaks.net/comment/2580401#comment-2580401

  // Set the internal reference and mux for the ATmega32U4.
  ADMUX = (1<<REFS1) | (1<<REFS0) | (1<<MUX2) | (1<<MUX1) | (1<<MUX0);
  ADCSRB |= (1 << MUX5); // enable the ADC

  delay(2); // Wait for internal reference to settle

  // Start the conversion
  ADCSRA |= bit(ADSC); // First reading

  // Detect end-of-conversion
  while (bit_is_set(ADCSRA,ADSC));

  ADCSRA |= bit(ADSC); // Second reading

  // Detect end-of-conversion
  while (bit_is_set(ADCSRA,ADSC));

  byte low = ADCL;
  byte high = ADCH;

  double t = (high << 8) | low;
  int offset = 5; // Temperature accuracy is +- 10
  t = (t - 273 + offset); // Convert from Kelvin to Celcius plus Offset

  return t;
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
    if (packetSize >= 5) {
      // received a packet with size 5 or more
      Serial.print("Received packet '");

      String s = "";
  
      // read packet
      for (int i = 0; i < packetSize; i++) {
        s += (char)LoRa.read();
      }
      
      Serial.print(s);
      Serial.println("'");

//      String sender = s.substring(0,2);
//      int id = s.substring(2,4).toInt();

      if (xSemaphoreTake(SemaphoreHndl, portMAX_DELAY) == pdTRUE) {
        int sleepTime = s.substring(4).toInt();
        int temperature = getTemperatureInternal();
//        db.addEntry(temperature, sleepTime);
//        db.printAll();
  
        if (!firstPackageReceived) {
          firstPackageReceived = true;
        }

        xSemaphoreGive(SemaphoreHndl);
      }
    }
  }
}

void TaskMonitorSerialPort(void *pvParameters) {
  (void) pvParameters;

  for(;;) {
    if (firstPackageReceived) {
      Serial.println("Disabled Monitor Serial Port.");
      vTaskDelete(NULL);
    }
    
    if (Serial.available() > 0) {
      String command = Serial.readString();
      command.trim();

      if (xSemaphoreTake(SemaphoreHndl, portMAX_DELAY) == pdTRUE) {

        if (command == "1") {
          printLastValue();
          
        } else if (command == "2") {
          printAllValues();
          
        } else if (command == "3") {
          enterLowPowerMode();
          
        } else if (command == "4") {
          Serial.println("Resetting database");
          address = 2;
          EEPROM.put(0, address);
          
        } else if (command == "5") {
          debugFunction();
          
        } else {
          Serial.println("Invalid command.");
        }

        xSemaphoreGive(SemaphoreHndl); 
      }
    }
  }
}
