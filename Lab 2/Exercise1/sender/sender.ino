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

double getTemperatureInternal() {
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
  int offset = -5; // Temperature accuracy is +- 10
  t = (t - 273 + offset); // Convert from Kelvin to Celcius plus Offset

  return t;
}
