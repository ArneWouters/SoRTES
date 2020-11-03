
struct Timestamp {
  int val = 0;

  int getTime() {
    return val++;
  }
};

struct DbEntry {
  int timestamp;
  int temperature;
};

struct Database {
  Timestamp ts;
  int index = 0;
  DbEntry table[1000];

  void addEntry(int temperature){
    if (index >= 1000) {
      index = 0;
    }
    
    int timestamp = ts.getTime();
    DbEntry entry = DbEntry{timestamp, temperature};   
    table[index] = entry;

    index ++;
  }

  void print() {
    Serial.println("Printing database...");
    Serial.println("-------------------");

    for (int i = 0; i < index; i++) {
      DbEntry entry = table[i];

      Serial.print("Time: ");
      Serial.print(entry.timestamp);
      Serial.print(" - Temperature: ");
      Serial.println(entry.temperature);
    }

    Serial.println("-------------------");
  }

};

Database db;

void setup() {
  Serial.begin(9600);
}

void loop()
{
  int temperature = (int) getTemperatureInternal();
  db.addEntry(temperature);

  if (Serial.available() > 0) {
    // read the incoming string:
    String input = Serial.readString();
    input.trim();

    if (input == "p") {
      db.print();
    }
  }

  delay(3000);
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
