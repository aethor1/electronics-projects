#include <Arduino.h>
#include <ESP32Servo.h>

// D27 = GPIO27 (PIR OUT), D18 = GPIO18 (servo signal).
// BOOT is GPIO0. EN is a reset button, so it cannot be read here.
constexpr uint8_t PIR_PIN = 27;
constexpr uint8_t SERVO_PIN = 18;
constexpr uint8_t BOOT_PIN = 0;
constexpr uint8_t STATUS_LED_PIN = LED_BUILTIN;  // Blue LED on this board (GPIO2).

constexpr int HOME_ANGLE = 90;
constexpr int FORWARD_ANGLE = HOME_ANGLE + 40;
constexpr unsigned long PIR_WARMUP_MS = 90000;
constexpr unsigned long MOVE_TIME_MS = 500;
constexpr unsigned long DEBOUNCE_MS = 30;
constexpr unsigned long READY_LED_ON_MS = 10000;
constexpr unsigned long PIR_PRINT_MS = 100;

Servo servo;
unsigned long startedAt = 0;
unsigned long phaseStartedAt = 0;
unsigned long buttonChangedAt = 0;
unsigned long lastPirPrintAt = 0;

// A short, non-blocking movement lets us notice button presses while moving.
enum class MotionPhase { Idle, MovingForward, MovingBack };
MotionPhase phase = MotionPhase::Idle;

bool lastButtonReading = HIGH;
bool stableButtonState = HIGH;
bool pirWasHigh = false;
uint8_t manualMovesQueued = 0;
uint8_t pirMovesQueued = 0;

void readBootButton(unsigned long now) {
  const bool reading = digitalRead(BOOT_PIN);

  if (reading != lastButtonReading) {
    buttonChangedAt = now;
    lastButtonReading = reading;
  }

  // Queue one movement per press. Releasing the button does nothing.
  if (now - buttonChangedAt >= DEBOUNCE_MS && reading != stableButtonState) {
    stableButtonState = reading;
    if (stableButtonState == LOW && manualMovesQueued < 10) {
      ++manualMovesQueued;
      Serial.println("BOOT pressed: manual movement queued");
    }
  }
}

void readPir(unsigned long now) {
  // Ignore PIR entirely for the first 90 seconds after startup/reset.
  if (now - startedAt < PIR_WARMUP_MS) return;

  const bool pirIsHigh = digitalRead(PIR_PIN) == HIGH;
  if (pirIsHigh != pirWasHigh) {
    pirWasHigh = pirIsHigh;
    // HIGH triggers once; the later transition back to LOW triggers once.
    // If a movement is underway, the next one waits in the queue.
    if (pirMovesQueued < 10) ++pirMovesQueued;
    Serial.println(pirIsHigh ? "PIR HIGH: movement queued"
                             : "PIR LOW: movement queued");
  }
}

void printPirState(unsigned long now) {
  // Show the raw PIR pin even during warm-up; only readPir() gates servo actions.
  if (now - lastPirPrintAt < PIR_PRINT_MS) return;
  lastPirPrintAt = now;
  Serial.print("PIR: ");
  Serial.println(digitalRead(PIR_PIN) == HIGH ? 1 : 0);
}

void updateReadyLed(unsigned long now) {
  // After warm-up, light the blue LED for 10 seconds, then turn it off.
  if (now - startedAt < PIR_WARMUP_MS) {
    digitalWrite(STATUS_LED_PIN, LOW);
    return;
  }
  const unsigned long readyFor = now - startedAt - PIR_WARMUP_MS;
  digitalWrite(STATUS_LED_PIN, readyFor < READY_LED_ON_MS ? HIGH : LOW);
}

void startMovement(unsigned long now) {
  servo.write(FORWARD_ANGLE);
  phase = MotionPhase::MovingForward;
  phaseStartedAt = now;
  Serial.println("Moving 40 degrees forward");
}

void updateMovement(unsigned long now) {
  if (phase == MotionPhase::MovingForward &&
      now - phaseStartedAt >= MOVE_TIME_MS) {
    servo.write(HOME_ANGLE);
    phase = MotionPhase::MovingBack;
    phaseStartedAt = now;
    Serial.println("Moving 40 degrees back");
  } else if (phase == MotionPhase::MovingBack &&
             now - phaseStartedAt >= MOVE_TIME_MS) {
    phase = MotionPhase::Idle;
  }
}

void setup() {
  Serial.begin(115200);
  pinMode(PIR_PIN, INPUT);
  pinMode(BOOT_PIN, INPUT_PULLUP);
  pinMode(STATUS_LED_PIN, OUTPUT);
  digitalWrite(STATUS_LED_PIN, LOW);

  // Start at neutral; each action goes 90 -> 130 -> 90 degrees.
  servo.setPeriodHertz(50);
  servo.attach(SERVO_PIN, 500, 2400);
  servo.write(HOME_ANGLE);

  startedAt = millis();
  Serial.println("Ready for BOOT presses; PIR warm-up started (90 seconds)");
}

void loop() {
  const unsigned long now = millis();
  readBootButton(now);
  printPirState(now);
  readPir(now);
  updateReadyLed(now);
  updateMovement(now);

  if (phase == MotionPhase::Idle) {
    // Manual button presses have priority over queued PIR movements.
    if (manualMovesQueued > 0) {
      --manualMovesQueued;
      startMovement(now);
    } else if (pirMovesQueued > 0) {
      --pirMovesQueued;
      startMovement(now);
    }
  }
}

