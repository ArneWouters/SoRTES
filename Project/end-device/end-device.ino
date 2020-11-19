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


/********
 * Data *
 ********/

struct Data {
  int temperature;  // temperature of the internal sensor
  int sleepTime; // delay in seconds for the next beacon
};


/***********
 * Globals *
 ***********/

bool firstPackageReceived = false;
int addr = 2;
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
  // Disable WDT
  wdt_disable();
  
  DatabaseQueue = xQueueCreate(10, sizeof(String));
  LoRaSenderQueue = xQueueCreate(10, sizeof(int));
  
  Serial.begin(9600);

  EEPROM.get(0, addr);
  if (addr < 2 || addr > EEPROM.length()-1) {
    addr = 2;
  }

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

void printData(Data data) {
  Serial.print("Temperature: ");
  Serial.print(data.temperature);
  Serial.print(" - Next beacon: ");
  Serial.print(data.sleepTime);
  Serial.println(" seconds");
}

void disableUSB() {
  cli();
  // Disable USB interrupts
  UDIEN  = 0b11111101;
  UEIENX = 0b11011111;
  
  // Stopping USB subsystems explicitly since power_all_disable() will not do that
  USBCON |=  bit(FRZCLK); // Freeze USB clock 
  PLLCSR &= ~bit(PLLE); // Disable USB PLL
  USBCON &= ~bit(OTGPADE);// Disable the OTG pad regulator
  USBCON &= ~bit(VBUSTE); // Disable the VBUS transition enable bit
  UHWCON &= ~bit(UVREGE); // Disable USB pad regulator
  USBINT &= ~bit(VBUSTI); // Clear the IVBUS Transition Interrupt flag
  USBCON &= ~bit(USBE); // Disable USB interface
  UDCON  |=  bit(DETACH); // Physically detach USB (by disconnecting internal pull-ups on D+ and D-)
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

  // disable ADC
  ADCSRA = 0;
  
  power_all_disable();
  sleep_enable();
  sei();
  
  sleep_cpu();

  sleep_disable();
  power_all_enable();
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
  int offset = 5; // Temperature accuracy is +- 10, 5 is the value after calibration at room temperature
  t = (t - 273 + offset); // Convert from Kelvin to Celcius plus Offset

  return t;
}

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
      Serial.println("Activating ultra low-power mode");
      delay(200);
      ultraLowPowerMode();
    }
      
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

      int sleepTime = s.substring(4).toInt();
      xQueueSend(DatabaseQueue, &s, portMAX_DELAY);
      beaconCounter++;

      if (!firstPackageReceived) {
        firstPackageReceived = true;
      }

      // Resuming DatabaseManager
      vTaskResume(DatabaseManagerHandle);

      // Sleeping
      vTaskDelay(((sleepTime*1000)-0)/portTICK_PERIOD_MS);

      LoRa.idle();
    }
  }
}

void TaskDatabaseManager(void) {
  if (xSemaphoreTake(SemaphoreHndl, portMAX_DELAY) == pdTRUE) {
    EEPROM.get(0, addr);
    
    xSemaphoreGive(SemaphoreHndl);
  }
  
  String valueFromQueue;
  Data dt;

  for(;;) {
    while (uxQueueMessagesWaiting(DatabaseQueue) > 0) {
      if (xQueueReceive(DatabaseQueue, &valueFromQueue, portMAX_DELAY) == pdPASS) {
        if (xSemaphoreTake(SemaphoreHndl, portMAX_DELAY) == pdTRUE) {
          dt.temperature = getTemperatureInternal();
          dt.sleepTime = valueFromQueue.substring(4).toInt();
          EEPROM.put(addr, dt);
          addr += (int)sizeof(dt);
  
          if (addr > EEPROM.length()-1) {
            // reset database when memory is full
            addr = 2;
            EEPROM.put(addr, dt);
            addr += (int)sizeof(dt);
          }
          
          EEPROM.put(0, addr);

          // release handle, all data stored
          xSemaphoreGive(SemaphoreHndl);
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
        if (addr <= 2) {
          Serial.println("Unable to print entry");
        } else {
          if (xSemaphoreTake(SemaphoreHndl, portMAX_DELAY) == pdTRUE) {
            Data data;
            EEPROM.get(addr-(int)sizeof(Data), data);
            Serial.print("Last value | ");
            printData(data);

            xSemaphoreGive(SemaphoreHndl);
          }
        }

      } else if (command == "2") {
        if (addr > 2) {
          int address = 2;
          int i = 0;
          Data data;

          if (xSemaphoreTake(SemaphoreHndl, portMAX_DELAY) == pdTRUE) {
            while (address+(int)sizeof(Data) <= addr) {
              Serial.print(i);
              Serial.print("| ");
              EEPROM.get(address, data);
              printData(data);
              address += (int)sizeof(Data);
              i++;
            }

            xSemaphoreGive(SemaphoreHndl);
          }
        }
        
      } else if (command == "3") {
        Serial.println("Activating ultra low-power mode");
        delay(200);
        ultraLowPowerMode();
        
      } else if (command == "4") {
        if (xSemaphoreTake(SemaphoreHndl, portMAX_DELAY) == pdTRUE) {
          Serial.println("Resetting database");
          addr = 2;
          EEPROM.put(0, addr);

          xSemaphoreGive(SemaphoreHndl);
        }

      } else {
        Serial.println("Invalid command.");
      }
    }
  }
}

void vApplicationIdleHook(void) {
  // disable ADC
  ADCSRA = 0;
  LoRa.sleep();

  set_sleep_mode( SLEEP_MODE_IDLE );
  cli();
  sleep_enable();
  sei();
  sleep_cpu();

  sleep_reset(); 
}
