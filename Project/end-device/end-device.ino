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
void TaskDatabaseController(void);
void TaskLoRaTransmitter(void);
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
  int BUFFER_LENGTH = EEPROM.length();
  int BUFFER_MAX_DATA_BLOCKS = (BUFFER_LENGTH-1-BUFFER_START_ADDRESS)/(int)sizeof(Data);
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
      read_ptr += BUFFER_LENGTH-BUFFER_START_ADDRESS;
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
    EEPROM.put(write_ptr, d);
    write_ptr += (int)sizeof(d);

    if (write_ptr + (int)sizeof(d)-1 >= BUFFER_LENGTH) {
      write_ptr = BUFFER_START_ADDRESS;
    }

    EEPROM.put(0, write_ptr);

    if (data_blocks < BUFFER_MAX_DATA_BLOCKS) {
      data_blocks++;
      EEPROM.put(2, data_blocks);
    } else {
      read_ptr = write_ptr;
    }
  }

  Data readData(int idx) {
    int address = read_ptr+(idx*(int)sizeof(Data));

    if (address >= BUFFER_LENGTH) {
      address -= BUFFER_LENGTH-BUFFER_START_ADDRESS;
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
TaskHandle_t DatabaseControllerHandle;
TaskHandle_t LoRaTransmitterHandle;
TaskHandle_t CommandManagerHandle;
SemaphoreHandle_t SemaphoreHndl;
QueueHandle_t DatabaseQueue;
QueueHandle_t LoRaTransmitterQueue;


/*********
 * Setup *
 *********/

void setup() {
  DatabaseQueue = xQueueCreate(10, sizeof(String));
  LoRaTransmitterQueue = xQueueCreate(10, sizeof(int));
  db = CircularBuffer();
  
  Serial.begin(9600);

  if (DatabaseQueue != NULL || LoRaTransmitterQueue != NULL) {
    xTaskCreate(TaskLoRaReceiver, "LoRa Receiver", 128, NULL, 2, &LoRaReceiverHandle);
    xTaskCreate(TaskDatabaseController, "Database Manager", 128, NULL, 1, &DatabaseControllerHandle);
    xTaskCreate(TaskLoRaTransmitter, "LoRa Sender", 128, NULL, 1, &LoRaTransmitterHandle);
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
  USBCON |= bit(FRZCLK);
  PLLCSR &= ~bit(PLLE);
  USBCON &= ~bit(USBE);
}


void ultraLowPowerMode() {
  vTaskEndScheduler();
  LoRa.end();
  wdt_disable();  // Disable WDT

  if (!firstPackageReceived) {
    disableUSB();
  }

  for (byte i = 0; i <= 32; i++) {
    pinMode (i, OUTPUT);
    digitalWrite (i, LOW);
  }

  set_sleep_mode(SLEEP_MODE_PWR_DOWN);
  
  cli();
  ADCSRA = 0;  // disable ADC
  ACSR |= bit(ACD);  // disable analog comparator
  ACSR &= ~bit(ACIE);
  MCUCR |= bit(JTD);  // disable OCD
  TWCR &= ~bit(TWEN);  // disable TWI
  SPCR &= ~bit(SPE);  // disable SPI
  power_all_disable();
  sleep_enable();
  sei();
  
  sleep_cpu();

  sleep_disable();
  power_all_enable();
}


int getTemperatureInternal() {
  ADMUX = bit(REFS1)|bit(REFS0)|bit(MUX2)|bit(MUX1)|bit(MUX0);
  ADCSRB |= bit(MUX5);

  delay(2);  // Wait for internal reference to settle

  // First reading
  ADCSRA |= bit(ADSC);
  while (bit_is_set(ADCSRA,ADSC));

  // Second reading
  ADCSRA |= bit(ADSC);
  while (bit_is_set(ADCSRA,ADSC));

  byte low = ADCL;
  byte high = ADCH;

  double t = (high << 8) | low;
  int offset = 5;  // 5 is the value after calibration at room temperature
  t = (t - 273 + offset);  // Convert from Kelvin to Celcius and add offset

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

      int delayBuffer = 200;
      vTaskResume(DatabaseControllerHandle);
      vTaskDelay(((sleepTime*1000)-delayBuffer)/portTICK_PERIOD_MS);  // Sleeping
      LoRa.idle();  // Put LoRa module back into standby mode
      ADCSRA |= bit(ADEN)|bit(ADPS2)|bit(ADPS1);  // enable ADC again
    }
  }
}


/**********************
 * DatabaseController *
 **********************/
 
void TaskDatabaseController(void) { 
  String valueFromQueue;
  Data dt;

  for(;;) {
    while (uxQueueMessagesWaiting(DatabaseQueue) > 0) {
      if (xQueueReceive(DatabaseQueue, &valueFromQueue, portMAX_DELAY) == pdPASS) {
        dt.temperature = getTemperatureInternal();
        dt.sleepTime = valueFromQueue.substring(4).toInt();
        
        if (xSemaphoreTake(SemaphoreHndl, portMAX_DELAY) == pdTRUE) {
          db.storeData(dt);
          xSemaphoreGive(SemaphoreHndl);  // release semaphore
        }
  
        xQueueSend(LoRaTransmitterQueue, &dt.temperature, portMAX_DELAY);
        vTaskResume(LoRaTransmitterHandle);
      }
    }

    vTaskSuspend(NULL);  // suspend when all messages processed
  }
}


/*******************
 * LoRaTransmitter *
 *******************/
 
void TaskLoRaTransmitter(void) {
  int valueFromQueue;
  
  for(;;) {
    LoRa.idle();
    
    while (uxQueueMessagesWaiting(LoRaTransmitterQueue) > 0) {
      if (xQueueReceive(LoRaTransmitterQueue, &valueFromQueue, portMAX_DELAY) == pdPASS) {
        LoRa.beginPacket();
        LoRa.print(valueFromQueue);
        LoRa.endPacket();
      }
    }

    vTaskSuspend(NULL);  // suspend when all messages processed
  }
}


/******************
 * CommandManager *
 ******************/
 
void TaskCommandManager(void) {
  for(;;) {
    if (firstPackageReceived) {
      Serial.end();
      disableUSB();
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
              delay(1);  // prevents reading eeprom too fast
            }
          }
          xSemaphoreGive(SemaphoreHndl);
        }
      } else if (command == "3") {
        Serial.println("Activating ultra low-power mode");
        delay(200);
        Serial.end();
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
  ADCSRA = 0;  // disable ADC
  LoRa.sleep();

  set_sleep_mode(SLEEP_MODE_IDLE);
  cli();
  sleep_enable();
  sei();
  sleep_cpu();

  sleep_reset(); 
}
