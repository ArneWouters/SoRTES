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

struct Node {
  int timestamp;
  int temperature;
  Node* next = nullptr;

  Node(int ts, int temp) {
    timestamp = ts;
    temperature = temp;
  }
};

struct LinkedList {
  Node* head = nullptr;

  void addNode(Node* node) {
    if (!head) {
      head = node;
      
    } else {
      Node* p = head;
      
      while(p->next) {
        p = p->next;
      }

      p->next = node;
    }
  }

  int getSize() {
    int counter = 0;

    if (head) {
      counter++;
      Node* p = head;
      
      while(p->next) {
        p = p->next;
        counter++;
      }
    }
    
    return counter;
  }
};

LinkedList lst;
Timestamp ts;
int addr = 2;

void setup() {
  Serial.begin(9600);
  while (!Serial);

  loadData();
}

void loop()
{
  int temperature = getTemperatureInternal();
  int timestamp = ts.getTime();

  Node* n = new Node(timestamp, temperature);
  lst.addNode(n);
  
  EEPROM.write(addr, timestamp);
  EEPROM.write(addr+1, temperature);
  Serial.print("Store (" + String(timestamp) + ", " + String(temperature) + ") on EEPROM");
  Serial.println(" at " + String(addr));
  

  int val1 = EEPROM.read(addr);
  int val2 = EEPROM.read(addr+1);
  Serial.print("Read (" + String(val1) + ", " + String(val2) + ") on EEPROM");
  Serial.println(" at " + String(addr));

  EEPROM.write(0, addr);
  addr += 2;

  Serial.print("Current list size: ");
  Serial.println(lst.getSize());
  delay(5000);
}

void loadData() {
  int dataSize = EEPROM.read(0);
  addr = dataSize + 2;
  Serial.println("Loading data...");
  
  for(auto i = 2; i <= dataSize; i += 2){
    int val1 = EEPROM.read(i);
    int val2 = EEPROM.read(i+1);
    Serial.println("(" + String(val1) + ", " + String(val2) + ")");
    ts.val = val1+1;

    Node* n = new Node(val1, val2);
    lst.addNode(n);
  }

  Serial.print("Current list size: ");
  Serial.println(lst.getSize());

  Serial.println("...Done!");
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
