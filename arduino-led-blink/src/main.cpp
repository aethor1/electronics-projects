#include <Arduino.h>

long PreviousTime = millis();
long CurrentTime;
long diff;


void setup() {
  pinMode(7,OUTPUT);
}

// void loop() {
//   CurrentTime = millis();
//   diff = CurrentTime - PreviousTime;
//     //digitalWrite(7, LOW);

 
//   if (diff >= 1000 && diff <= 2000) {
//     digitalWrite(7, HIGH);
//     delay(1000);
//     digitalWrite(7, LOW);
//     PreviousTime = CurrentTime;

//   }

// }

void loop() {
  delay(1000);
  digitalWrite(7,HIGH);
  delay(1000);
  digitalWrite(7,LOW);
}
