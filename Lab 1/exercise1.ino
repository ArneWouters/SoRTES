#include <arduino-timer.h> // https://github.com/contrem/arduino-timer

String ledStatus = "";
int blinkRate = 0;
auto timer = timer_create_default();

bool toggle_led(void *) {
  digitalWrite(LED_BUILTIN, !digitalRead(LED_BUILTIN)); // toggle the LED
  
  if (ledStatus == "off") {
    digitalWrite(LED_BUILTIN, LOW);
    return false;
  }
  
  return true;
}

void setup() {
  // put your setup code here, to run once:
  
  pinMode(LED_BUILTIN, OUTPUT);
  
  Serial.begin(9600);
  while (!Serial);
  Serial.println("Enter LED status (on/off):");
}

void loop() {
  // put your main code here, to run repeatedly:

  if (Serial.available() > 0) {
    // read the incoming byte:
    ledStatus = Serial.readString();
    ledStatus.trim();

    if (ledStatus == "on") {
      Serial.println("Enter the blink rate (1-60 sec):");
      while(!Serial.available());
      blinkRate = Serial.parseInt();
      
      if(!(blinkRate >= 1 && blinkRate <= 60)) {
        Serial.println("Error: Invalid rate, using default rate.");
        blinkRate = 1;
      }
      
      Serial.println("You have selected LED on. Blink rate is " + String(blinkRate) + " sec");
      timer.every(blinkRate*1000, toggle_led);
            
    } else if (ledStatus == "off") {
      Serial.println("You have selected LED off.");
    }
  }

  timer.tick();
}
