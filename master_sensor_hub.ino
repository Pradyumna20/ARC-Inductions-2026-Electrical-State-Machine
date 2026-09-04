 /*
// ARC Inductions 2026-2027 - Electrical State Machine
// Task 1: Arduino Master-Slave Monitoring System - MASTER board
// Name: Pradyumna Pathak
// ID: 2025AAPS0291H

Tinkercad: https://www.tinkercad.com/things/8yMoD9Ju7rH-electrical-state-machine
 */

#include <Wire.h>   // the two-wire (I2C) link between the boards
#include <Servo.h>  // the vent servo

// pins on this board
const int lightPin  = A0;   // photoresistor, brighter light gives a bigger number
const int gasPin    = A1;   // gas sensor, needs the 1k resistor down to GND (see CONNECTIONS.md)
const int tempPin   = A2;   // temperature sensor
const int servoPin  = 9;
const int buzzerPin = 8;
// the I2C wires always sit on A4 and A5, don't have to name those anywhere

const int screenAddress = 8;   // the screen board picks up messages sent to this number

// numbers straight off the task sheet
const int   gasHigh   = 180;   // gas alert switches on once the reading goes over this
const int   gasSafe   = 130;   // and it won't switch off again until the reading drops under this
const float tempLimit = 45.0;  // over this many degrees C is a heat emergency

/*
a "blackout" is meant to be the light disappearing all of a sudden, not
the light slowly fading. so we keep a "normal light" number that snaps
straight up whenever it gets brighter, but only trickles down bit by
bit. that way a slow fade just drags the normal number down with it and
nothing happens, but a sudden drop leaves the normal number sitting up
high and the big gap gives it away.
*/
const int bigGap    = 250;   // gap this big between normal and now = blackout
const int smallGap  = 150;   // gap has to come back under this before we say the light is back
const int creep     = 2;     // how much the "normal light" number trickles down each step
const unsigned long creepEvery = 150;   // ms between each trickle-down step

// servo angles
const int ventShut = 0;
const int ventHalf = 90;
const int ventOpen = 180;    // task says the servo swings here on a heat emergency

const unsigned long checkEvery = 150;   // do the whole loop once every this many ms

/*
the 6 states, written as words so the code reads nicely. the screen
board has this exact same list in the exact same order, if you change
one change both.
*/
enum State {
  STANDBY,          // 0  just switched on, sitting there waiting for the remote's ON button
  MONITORING,       // 1  running normally, screen shows either light or gas
  GAS_ALERT,        // 2  gas reading went over 180
  BLACKOUT,         // 3  light vanished suddenly, remote gets ignored until it comes back
  HEAT_EMERGENCY,   // 4  over 45C, this one beats all the others, only the remote's reset button gets us out
  MULTI_FAULT       // 5  gas alert and blackout both happening at once
};
State state = STANDBY;   // always start here

// button codes, the screen board uses the same numbers
const byte noButton     = 0;
const byte onButton     = 1;   // switch the system on (only does anything in STANDBY)
const byte resetButton  = 2;   // clear a heat emergency once it's cooled off
const byte switchButton = 3;   // flip the screen between light and gas

// flags that stay how they are until something flips them
bool turnedOn = false;   // has the ON button been pressed even once yet
bool gasBad   = false;   // goes true when gas passes 180, back to false when it drops under 130
bool isDark   = false;   // true while we're in a blackout
bool tooHot   = false;   // true while we're in a heat emergency, only the reset button clears it
bool showGas  = false;   // false = screen shows light, true = screen shows gas

int normalLight = 0;              // the slowly-updating "normal light" level from the note above
unsigned long lastCreep = 0;      // last time we trickled normalLight down
unsigned long lastCheck = 0;      // last time we ran the main loop body

Servo vent;

/*
these lines just promise the compiler that these functions show up
further down. Tinkercad won't compile without them, the normal Arduino
program writes them for you behind the scenes.
*/
void  note(const char *text);
float readTemp();
byte  getButton();
void  updateFlags(int light, int gas, float temp);
void  doButton(byte button, float temp);
void  pickState();
void  runOutputs();
void  sendToScreen(int light, int gas);

void setup() {
  Serial.begin(9600);   // only used for printing a little log when big stuff happens
  Wire.begin();          // no number in here = this board is the one in charge of the I2C link
  pinMode(buzzerPin, OUTPUT);
  vent.attach(servoPin);
  vent.write(ventShut);  // start with the vent closed
}

void loop() {
  if (millis() - lastCheck < checkEvery) return;   // not time yet, come back later
  lastCheck = millis();

  // 1) read all three sensors
  int   light = analogRead(lightPin);
  int   gas   = analogRead(gasPin);
  float temp  = readTemp();

  // 2) update the on/off flags from those readings
  updateFlags(light, gas, temp);

  // 3) find out what the remote pressed. the screen board is the one
  //    actually listening to it, we just ask. if we're in a blackout we
  //    throw the button away and don't act on it.
  byte button = getButton();
  if (isDark) button = noButton;
  doButton(button, temp);

  // 4) turn all those flags into one single state
  pickState();

  // 5) make the servo and buzzer do the right thing for that state
  runOutputs();

  // 6) send the state and the two numbers over to the screen board
  sendToScreen(light, gas);
}

/*
just one spot for all the log lines so they all look the same, each one
gets the current time stuck on the front
*/
void note(const char *text) {
  Serial.print("[");
  Serial.print(millis());
  Serial.print(" ms] ");
  Serial.println(text);
}

/*
turns the temperature sensor's raw number into actual degrees C. this is
the standard formula for this sensor, nothing to do with our task.
*/
float readTemp() {
  int raw = analogRead(tempPin);
  float volts = raw * 5.0 / 1024.0;      // raw number back into a voltage
  return (volts - 0.5) * 100.0;          // and the voltage into degrees
}

/*
asks the screen board what the last remote button was. the screen board
wipes its copy the moment it hands it over, so one press only ever gets
dealt with once.
*/
byte getButton() {
  byte button = noButton;                     // assume nothing was pressed
  Wire.requestFrom(screenAddress, 1);         // ask for 1 byte
  if (Wire.available()) button = Wire.read(); // grab it if it turned up
  return button;
}

/*
looks at the fresh sensor readings and flips the sticky flags. nothing
in here picks the state, that's the next function, this just keeps the
flags up to date.
*/
void updateFlags(int light, int gas, float temp) {
  // gas: over 180 switches the alert on, and it stays on until we're back under 130
  if (gas > gasHigh)      gasBad = true;
  else if (gas < gasSafe) gasBad = false;

  // heat: switches on the instant it's too hot. nothing in here switches
  // it back off, only the reset button over in doButton() can do that.
  if (temp > tempLimit) {
    if (!tooHot) note("HEAT EMERGENCY - over 45C");   // only log it the first time
    tooHot = true;
  }

  // blackout stuff below. skip it entirely until the system's actually
  // been turned on, there's nothing to compare against before that.
  if (!turnedOn) {
    normalLight = light;   // keep it sitting on the current level for now
    return;
  }

  // every so often, nudge the "normal light" number
  if (millis() - lastCreep >= creepEvery) {
    lastCreep = millis();
    if (light > normalLight) normalLight = light;    // brighter now, jump straight up to it
    else                     normalLight -= creep;   // otherwise let it trickle down a bit
  }

  int gap = normalLight - light;   // how far below "normal" the light is right now
  if (!isDark && gap > bigGap) {
    isDark = true;
    note("BLACKOUT - light dropped suddenly");
  } else if (isDark && gap < smallGap) {
    isDark = false;
    note("blackout over - light is back");
  }
}

/*
does whatever the pressed button is meant to do. most buttons only
matter in one particular situation, everywhere else they do nothing.
*/
void doButton(byte button, float temp) {
  if (button == onButton) {
    if (!turnedOn) {                          // ignore it if we're already on
      turnedOn = true;
      normalLight = analogRead(lightPin);     // set "normal light" from whatever it is right now
      note("system turned on");
    }
  } else if (button == resetButton) {
    // only actually lets us out of a heat emergency once it's genuinely cooled back down
    if (tooHot && temp <= tempLimit) {
      tooHot = false;
      note("heat emergency reset");
    }
  } else if (button == switchButton) {
    if (turnedOn) showGas = !showGas;         // just swaps what the screen's showing
  }
}

/*
the order of these ifs IS the priority order. heat emergency is checked
first because the task says it beats everything, even standby. then
standby if we were never turned on. then gas+dark together, then each
of those on its own, and if none of that then we're just monitoring.
*/
void pickState() {
  if (tooHot)                  state = HEAT_EMERGENCY;
  else if (!turnedOn)          state = STANDBY;
  else if (gasBad && isDark)   state = MULTI_FAULT;
  else if (gasBad)             state = GAS_ALERT;
  else if (isDark)             state = BLACKOUT;
  else                         state = MONITORING;

  // log the multi-fault, but only once when we first drop into it
  static State lastNoted = STANDBY;
  if (state == MULTI_FAULT && lastNoted != MULTI_FAULT) {
    note("MULTI-FAULT - gas alert and blackout together");
  }
  lastNoted = state;
}

/*
moves the servo and drives the buzzer to match the state we just picked.
*/
void runOutputs() {
  // servo: wide open for heat, half open when gas is involved, shut otherwise
  if (state == HEAT_EMERGENCY)                         vent.write(ventOpen);
  else if (state == GAS_ALERT || state == MULTI_FAULT) vent.write(ventHalf);
  else                                                vent.write(ventShut);

  // buzzer
  if (state == HEAT_EMERGENCY || state == MULTI_FAULT) {
    // both of these are meant to be a solid non-stop sound. heat gets a
    // higher pitch so you can tell the two apart by ear.
    tone(buzzerPin, state == HEAT_EMERGENCY ? 1000 : 700);
  } else if (state == GAS_ALERT) {
    // just the one problem, so beep on-off-on-off instead of solid
    if ((millis() / 250) % 2 == 0) tone(buzzerPin, 700);
    else                           noTone(buzzerPin);
  } else {
    noTone(buzzerPin);   // standby / monitoring / blackout, stay quiet
  }
}

/*
sends 6 bytes over to the screen board: the state, which view to show,
then the light number and the gas number. light and gas can go up to
1023 which doesn't fit in one byte, so each gets split into a high half
and a low half and the screen board sticks them back together.
*/
void sendToScreen(int light, int gas) {
  Wire.beginTransmission(screenAddress);
  Wire.write((byte)state);
  Wire.write(showGas ? 1 : 0);
  Wire.write(highByte(light));
  Wire.write(lowByte(light));
  Wire.write(highByte(gas));
  Wire.write(lowByte(gas));
  Wire.endTransmission();
}


