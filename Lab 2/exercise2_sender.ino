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

  // send initial packet
  Serial.print("Sending initial packet: ");
  Serial.println(counter);
  LoRa.beginPacket();
  LoRa.print("Ping Pong");
  LoRa.endPacket();

  counter++;
}

void loop() {
  // try to parse packet
  int packetSize = LoRa.parsePacket();
  if (packetSize) {
    // received a packet
    Serial.print("Received packet '");
    String msg = "";

    // read packet
    while (LoRa.available()) {
      msg += (char)LoRa.read();
    }

    Serial.print(msg);
    
    // print RSSI of packet
    Serial.print("' with RSSI ");
    Serial.println(LoRa.packetRssi());

    delay(random(1,5)*1000);

    Serial.print("Sending packet: ");
    Serial.println(counter);

    // send packet back
    LoRa.beginPacket();
    LoRa.print(msg);
    LoRa.endPacket();

    counter++;
  }
}
