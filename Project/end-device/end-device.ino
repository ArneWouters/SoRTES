#include <SPI.h>
#include <LoRa.h>
#include <EEPROM.h>
#include <Arduino_FreeRTOS.h>
#include <semphr.h>
#include <queue.h>
#include <avr/sleep.h>
#include <avr/power.h>
#include <avr/wdt.h>

#define SCK 15
#define MISO 14
#define MOSI 16
#define SS 8
#define RST 4
#define DI0 7
#define BAND 8693E5
#define configUSE_IDLE_HOOK 1
#define configUSE_TICKLESS_IDLE 1
#define INCLUDE_vTaskSuspend 1
#define INCLUDE_vTaskDelay 1


/*********
 * Tasks *
 *********/

void TaskLoRaReceiver(void);
void TaskDatabaseManager(void);
void TaskLoRaSender(void);
void TaskCommandManager(void);
void vApplicationIdleHook(void);


/******************
 * CircularBuffer *
 ******************/

struct Data {
  int temperature;  // temperature of the internal sensor
  int sleepTime; // delay in seconds for the next beacon
};

class CircularBuffer {
private:
  int BUFFER_START_ADDRESS = 4;
  int BUFFER_MAX_DATA_BLOCKS = (EEPROM.length()-1-BUFFER_START_ADDRESS)/(int)sizeof(Data);
  int write_ptr;
  int read_ptr;
  int data_blocks;

public:
  CircularBuffer() {
    EEPROM.get(0, write_ptr);
    EEPROM.get(2, data_blocks);

    if (write_ptr < BUFFER_START_ADDRESS || data_blocks < 0) {
      write_ptr = BUFFER_START_ADDRESS;
      data_blocks = 0;
      EEPROM.put(0, write_ptr);
      EEPROM.put(2, data_blocks);
    }

    read_ptr = write_ptr - (data_blocks*(int)sizeof(Data));

    if (read_ptr < BUFFER_START_ADDRESS) {
      read_ptr += EEPROM.length()-BUFFER_START_ADDRESS;
    }
  }

  int getDataBlocks() {
    return data_blocks;
  }

  bool empty() {
    return data_blocks == 0;
  }

  void reset() {
    write_ptr = BUFFER_START_ADDRESS;
    read_ptr = BUFFER_START_ADDRESS;
    data_blocks = 0;
    EEPROM.put(0, write_ptr);
    EEPROM.put(2, data_blocks);
  }

  void storeData(Data &d) {
    if (write_ptr + (int)sizeof(d)-1 >= EEPROM.length()) {
      write_ptr = BUFFER_START_ADDRESS;
    }

    EEPROM.put(write_ptr, d);
    write_ptr += (int)sizeof(d);
    EEPROM.put(0, write_ptr);

    if (data_blocks < BUFFER_MAX_DATA_BLOCKS) {
      data_blocks++;
      EEPROM.put(2, data_blocks);
    }
  }

  Data readData(int idx) {
    int address = read_ptr+(idx*(int)sizeof(Data));

    if (address >= EEPROM.length()) {
      address -= EEPROM.length()+BUFFER_START_ADDRESS;
    }

    Data d;
    EEPROM.get(address, d);
    return d;
  }
};

/***********
 * Globals *
 ***********/

bool firstPackageReceived = false;
CircularBuffer db;
TaskHandle_t LoRaReceiverHandle;
TaskHandle_t DatabaseManagerHandle;
TaskHandle_t LoRaSenderHandle;
TaskHandle_t CommandManagerHandle;
SemaphoreHandle_t SemaphoreHndl;
QueueHandle_t DatabaseQueue;
QueueHandle_t LoRaSenderQueue;


/*********
 * Setup *
 *********/

void setup() {
  wdt_disable();  // Disable WDT
  
  DatabaseQueue = xQueueCreate(10, sizeof(String));
  LoRaSenderQueue = xQueueCreate(10, sizeof(int));
  db = CircularBuffer();
  
  Serial.begin(9600);

  if (DatabaseQueue != NULL || LoRaSenderQueue != NULL) {
    xTaskCreate(TaskLoRaReceiver, "LoRa Receiver", 128, NULL, 2, &LoRaReceiverHandle);
    xTaskCreate(TaskDatabaseManager, "Database Manager", 128, NULL, 1, &DatabaseManagerHandle);
    xTaskCreate(TaskLoRaSender, "LoRa Sender", 128, NULL, 1, &LoRaSenderHandle);
    xTaskCreate(TaskCommandManager, "Command Manager", 128, NULL, 2, &CommandManagerHandle);
  }

  SemaphoreHndl = xSemaphoreCreateBinary();
  if (SemaphoreHndl != NULL) {
    xSemaphoreGive(SemaphoreHndl);
  }
}


void loop() {}


/*************
 * Functions *
 *************/

void printData(Data &d) {
  Serial.print("Temperature: ");
  Serial.print(d.temperature);
  Serial.print(" - Next beacon: ");
  Serial.print(d.sleepTime);
  Serial.println(" seconds");
}


void disableUSB() {
  cli();
  // Power Off the USB interface because power_all_disable() will not do that
  USBCON |=  bit(FRZCLK);
  PLLCSR &= ~bit(PLLE);
  USBCON &= ~bit(OTGPADE);
  USBCON &= ~bit(VBUSTE);
  UHWCON &= ~bit(UVREGE);
  USBINT &= ~bit(VBUSTI);
  USBCON &= ~bit(USBE);
  UDCON  |=  bit(DETACH);
  sei();
}


void ultraLowPowerMode() {
  vTaskEndScheduler();
  LoRa.end();

  if (!firstPackageReceived) {
    disableUSB();
  }

  for (byte i = 0; i <= 32; i++) {
    pinMode (i, OUTPUT);
    digitalWrite (i, LOW);
  }

  set_sleep_mode(SLEEP_MODE_PWR_DOWN);
  cli();

  ADCSRA = 0; // disable ADC
  
  power_all_disable();
  sleep_enable();
  sei();
  
  sleep_cpu();

  sleep_disable();
  power_all_enable();
}


int getTemperatureInternal() {
  // Set the internal reference and mux .
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
  int offset = 5; // 5 is the value after calibration at room temperature
  t = (t - 273 + offset); // Convert from Kelvin to Celcius and add offset

  return t;
}


/****************
 * LoRaReceiver *
 ****************/

void TaskLoRaReceiver(void) {
  Serial.println("Starting LoRa Receiver");
  LoRa.setPins(SS,RST,DI0);

  if (!LoRa.begin(BAND)) {
    Serial.println("Starting LoRa failed!");
  }

  int beaconTreshold = 20;
  int beaconCounter = 0;

  for(;;) {
    // check if 20 beacons received
    if (beaconCounter == beaconTreshold) {
      ultraLowPowerMode();
    }
      
    int packetSize = LoRa.parsePacket();
    if (packetSize >= 5) {
      // received a packet with size 5 or more
      String s = "";
  
      // read packet
      for (int i = 0; i < packetSize; i++) {
        s += (char)LoRa.read();
      }

      int sleepTime = s.substring(4).toInt();
      xQueueSend(DatabaseQueue, &s, portMAX_DELAY);
      beaconCounter++;

      if (!firstPackageReceived) {
        firstPackageReceived = true;
      }

      // Resuming DatabaseManager
      vTaskResume(DatabaseManagerHandle);

      // Sleeping
      vTaskDelay(((sleepTime*1000)+300)/portTICK_PERIOD_MS);

      // Put LoRa moduel back into standby mode
      LoRa.idle();
    }
  }
}


/*******************
 * DatabaseManager *
 *******************/
 
void TaskDatabaseManager(void) { 
  String valueFromQueue;
  Data dt;

  for(;;) {
    while (uxQueueMessagesWaiting(DatabaseQueue) > 0) {
      if (xQueueReceive(DatabaseQueue, &valueFromQueue, portMAX_DELAY) == pdPASS) {
        dt.temperature = getTemperatureInternal();
        dt.sleepTime = valueFromQueue.substring(4).toInt();
        
        if (xSemaphoreTake(SemaphoreHndl, portMAX_DELAY) == pdTRUE) {
          db.storeData(dt);
          xSemaphoreGive(SemaphoreHndl); // release handle, all data stored
        }
  
        xQueueSend(LoRaSenderQueue, &dt.temperature, portMAX_DELAY);
  
        // Resuming LoRaSender
        vTaskResume(LoRaSenderHandle);
      }
    }

    // suspend if all messages processed
    vTaskSuspend(NULL);
  }
}


/**************
 * LoRaSender *
 **************/
 
void TaskLoRaSender(void) {
  int valueFromQueue;
  
  for(;;) {
    LoRa.idle();
    
    while (uxQueueMessagesWaiting(LoRaSenderQueue) > 0) {
      if (xQueueReceive(LoRaSenderQueue, &valueFromQueue, portMAX_DELAY) == pdPASS) {
        LoRa.beginPacket();
        LoRa.print(valueFromQueue);
        LoRa.endPacket();
      }
    }

    // suspend if all messages processed
    vTaskSuspend(NULL);
  }
}


/******************
 * CommandManager *
 ******************/
 
void TaskCommandManager(void) {
  for(;;) {
    if (firstPackageReceived) {
      disableUSB();
      Serial.println("Disabled CommandManager");
      vTaskDelete(NULL);
    }
    
    if (Serial.available() > 0) {
      String command = Serial.readString();
      command.trim();

      if (command == "1") {
        if (xSemaphoreTake(SemaphoreHndl, portMAX_DELAY) == pdTRUE) {
          int idx = db.getDataBlocks()-1;

          if (db.empty()) {
            Serial.println("Database is empty.");
          } else {
            Data d = db.readData(idx);
            Serial.print("Last value | ");
            printData(d);
          }
          xSemaphoreGive(SemaphoreHndl);
        }
      } else if (command == "2") {
        if (xSemaphoreTake(SemaphoreHndl, portMAX_DELAY) == pdTRUE) {
          if (db.empty()) {
            Serial.println("Database is empty.");
          } else {
            Data d;
            
            for (int i = 0; i < db.getDataBlocks(); i++) {
              Serial.print(i);
              Serial.print("| ");
              d = db.readData(i);
              printData(d);
            }
          }
          xSemaphoreGive(SemaphoreHndl);
        }
      } else if (command == "3") {
        Serial.println("Activating ultra low-power mode");
        delay(200);
        ultraLowPowerMode();
        
      } else if (command == "4") {
        if (xSemaphoreTake(SemaphoreHndl, portMAX_DELAY) == pdTRUE) {
          Serial.println("Resetting database");
          db.reset();

          xSemaphoreGive(SemaphoreHndl);
        }
      } else {
        Serial.println("Invalid command.");
      }
    }
  }
}


/***********************
 * ApplicationIdleHook *
 ***********************/
 
void vApplicationIdleHook(void) {
  ADCSRA = 0; // disable ADC
  LoRa.sleep();

  set_sleep_mode(SLEEP_MODE_IDLE);
  cli();
  sleep_enable();
  sei();
  sleep_cpu();

  sleep_reset(); 
}
