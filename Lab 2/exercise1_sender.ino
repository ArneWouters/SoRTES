#include <SPI.h>
#include <LoRa.h>

#define SCK 15
#define MISO 14
#define MOSI 16
#define SS 8
#define RST 4
#define DI0 7
#define BAND 868E6
#define PABOOST true


int counter = 0;

void setup() {
  Serial.begin(9600);
  while (!Serial);

  Serial.println("LoRa Sender");
  LoRa.setPins(SS,RST,DI0);

  if (!LoRa.begin(BAND)) {
    Serial.println("Starting LoRa failed!");
    while (1);
  }
}

void loop() {
  Serial.print("Sending packet: ");
  Serial.println(counter);

  // send packet
  LoRa.beginPacket();
  LoRa.print("Temperature: ");
  LoRa.print(getTemperatureInternal());
  LoRa.endPacket();

  counter++;

  delay(random(1,5)*1000);
}

int8_t getTemperatureInternal() {
  // found at https://www.avrfreaks.net/comment/2580401#comment-2580401
  /* Temperature  °C -45°C  +25°C  +85°C
     Voltage      mV 242 mV 314 mV 380 mV  */

  // Select Temp Channel and 2.56V Reference
  ADMUX = (1<<REFS1) | (1<<REFS0) | (1<<MUX2) | (1<<MUX1) | (1<<MUX0);
  ADCSRB |= (1<<MUX5);
  delay(2); //wait for internal reference to settle

  // start the conversion
  ADCSRA |= bit(ADSC);

  // ADSC is cleared when the conversion finishes
  while (ADCSRA & bit(ADSC));

  uint8_t low  = ADCL;
  uint8_t high = ADCH;

  //discard first reading
  ADCSRA |= bit(ADSC);
  while (ADCSRA & bit(ADSC));
  low  = ADCL;
  high = ADCH;
  int a = (high << 8) | low;

  //return temperature in C
  return a - 272;
}
