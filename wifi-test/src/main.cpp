#include <Arduino.h>



void setup() {
  pinMode(18, INPUT_PULLDOWN);
  pinMode(LED_BUILTIN, OUTPUT);
}

void loop() {
  if (digitalWrite(18,HIGH)){
    digitalWrite(LED_BUILTIN, HIGH);
  }
   else {
    digitalWrite(LED_BUILTIN, LOW);
  }

}

