#include <EEPROM.h>

struct Timestamp {
  int val = 0;

  int getTime() {
    if (val > 255) {
      val = 0;
      return val;
    }
    
    return val++;
  }
};

struct dbEntry {
  int timestamp;
  int temperature;
};

struct Database {
  Timestamp ts;
  int index = 2;
  dbEntry table[100];

  void addEntry(int temperature){
    if (index >= 100) {
      index = 2;
    }
    
    int timestamp = ts.getTime();
    dbEntry entry = dbEntry{timestamp, temperature};
    int addr = index;
    
    table[index] = entry;

    // write to EEPROM, addr 0 and 1 reserved, addr i = timestamp, addr i+1 = temperature
    EEPROM.write(addr, timestamp);
    EEPROM.write(addr+1, temperature);
    Serial.print("Store (" + String(timestamp) + ", " + String(temperature) + ") on EEPROM");
    Serial.println(" at " + String(addr));

    EEPROM.write(0, index);
    index += 2;
  }

  int getIndex(){
    return index;
  }

  void loadTable() {
    int tableSize = EEPROM.read(0);
    Serial.println("Loading table...");

    for(auto i = 2; i < tableSize; i+=2){
      int val1 = EEPROM.read(i);
      int val2 = EEPROM.read(i+1);
      Serial.println("(" + String(val1) + ", " + String(val2) + ")");
    }

    Serial.println("...Done!");
  }
  
};

Database db;

void setup() {
  Serial.begin(9600);
  while (!Serial);

  db.loadTable();
}

void loop()
{
  int temperature = getTemperatureInternal();
  db.addEntry(temperature);

  int addr = db.getIndex()-2;
  int val1 = EEPROM.read(addr);
  int val2 = EEPROM.read(addr+1);
  Serial.print("Read (" + String(val1) + ", " + String(val2) + ") on EEPROM");
  Serial.println(" at " + String(addr));

  delay(1000);
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
