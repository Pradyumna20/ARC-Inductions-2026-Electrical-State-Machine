/*
ARC Inductions 2026-2027 - Electrical State Machine
Task 1: Arduino Master-Slave Monitoring System - SCREEN board
Name: Pradyumna Pathak
ID: 2025AAPS0291H

Tinkercad link: <paste your Tinkercad project link here before submitting>
*/

#include <Wire.h>            // the I2C link to the master
#include <LiquidCrystal.h>   // the pin-wired LCD
#include <IRremote.h>        // the IR receiver. Tinkercad ships version 2, which is what this code expects

const int irPin = 2;                     // the IR receiver's signal pin
LiquidCrystal lcd(12, 11, 5, 4, 3, 6);   // which pins the LCD is on: RS, E, D4, D5, D6, D7 (see CONNECTIONS.md)

const int myAddress = 8;   // this board listens for I2C messages sent to this number (master uses the same one)

// same 6 states, same order as the master file
enum State { STANDBY, MONITORING, GAS_ALERT, BLACKOUT, HEAT_EMERGENCY, MULTI_FAULT };

// same button codes as the master file
const byte noButton = 0, onButton = 1, resetButton = 2, switchButton = 3;

/*
the actual hex numbers the Tinkercad remote spits out for the 3 buttons
I'm using. these are the usual Tinkercad defaults but they might not be
yours, so: run the sim, open the Serial Monitor, press each button, and
copy whatever number it prints into the right line here.
*/
const unsigned long onCode     = 0xFFA25D;   // POWER button    -> turn the system on
const unsigned long resetCode  = 0xFF629D;   // STOP/FUNC button -> clear a heat emergency
const unsigned long switchCode = 0xFF6897;   // the "0" button   -> swap light / gas view

IRrecv ir(irPin);          // the IR receiver
decode_results reading;    // the last thing it decoded gets dropped in here

/*
these next few are shared between the main loop and the two functions
that fire off on their own whenever the I2C link does something.
"volatile" just tells the compiler they can change at any moment so
don't take shortcuts with them.
*/
volatile byte lastButton = noButton;   // last remote button. master reads it, then it goes back to noButton
volatile byte theState   = STANDBY;    // the state the master last sent us
volatile byte gasView    = 0;          // 0 = show light on the screen, 1 = show gas
volatile int  lightNum   = 0;          // last light number from the master
volatile int  gasNum     = 0;          // last gas number from the master

// promise the compiler these exist further down (Tinkercad needs this)
void whenDataArrives(int count);
void whenAsked();
void checkRemote();
void updateScreen();

void setup() {
  Serial.begin(9600);
  lcd.begin(16, 2);      // 16 characters across, 2 rows
  ir.enableIRIn();        // start listening for the remote

  Wire.begin(myAddress);            // a number in here = this board takes orders, it's not the boss
  Wire.onReceive(whenDataArrives);  // run this by itself whenever the master sends us something
  Wire.onRequest(whenAsked);        // run this by itself whenever the master asks us for something
}

void loop() {
  checkRemote();     // did the remote get pressed
  updateScreen();    // redraw the LCD if anything changed
}

/*
fires on its own the moment the master sends its 6 bytes. unpack them
back into our variables. keep it quick, it barges in on whatever loop()
was doing.
*/
void whenDataArrives(int count) {
  if (count < 6) {                          // message came in broken, bin it
    while (Wire.available()) Wire.read();
    return;
  }
  theState = Wire.read();                      // byte 1: the state
  gasView  = Wire.read();                      // byte 2: which view
  lightNum = (Wire.read() << 8) | Wire.read(); // bytes 3+4: light, high half then low half
  gasNum   = (Wire.read() << 8) | Wire.read(); // bytes 5+6: gas, same deal
}

/*
fires on its own when the master asks us for the last button. send it,
then wipe it so the same press can't get sent twice.
*/
void whenAsked() {
  Wire.write(lastButton);
  lastButton = noButton;
}

/*
checks if the remote got pressed, and if it did, turns whatever code it
sent into one of our button numbers. codes we don't recognise just get
ignored. it also prints the raw code every time, which is how you find
the numbers to put in onCode / resetCode / switchCode up top.
*/
void checkRemote() {
  if (!ir.decode(&reading)) return;   // nothing new from the remote, leave

  unsigned long code = reading.value;
  if      (code == onCode)     lastButton = onButton;
  else if (code == resetCode)  lastButton = resetButton;
  else if (code == switchCode) lastButton = switchButton;

  Serial.print("button code: 0x");    // print it so you can read off the real values
  Serial.println(code, HEX);
  ir.resume();                        // tell the receiver it's ok to catch the next one
}

/*
puts the right words on the LCD for whatever state we're in. it only
actually redraws when something's changed since last time, otherwise the
screen flickers like mad. the exact phrases are the ones from the task
sheet.
*/
void updateScreen() {
  /*
  grab our own copy of the shared values with interrupts switched off for
  a split second. if a new message landed halfway through us reading them
  we could end up with the high half of the old number and the low half
  of the new one, which would be a garbage value.
  */
  noInterrupts();
  byte st    = theState;
  byte asGas = gasView;
  int  light = lightNum;
  int  gas   = gasNum;
  interrupts();

  // remember what we drew last time so we can tell if anything changed
  static int oldState = -1, oldView = -1, oldNumber = -1;

  // the monitoring and gas screens show a live number, so also redraw when that number moves
  int  shownNumber = asGas ? gas : light;
  bool numberMoved = (st == MONITORING || st == GAS_ALERT) && shownNumber != oldNumber;

  if (st == oldState && asGas == oldView && !numberMoved) return;   // nothing changed, leave the screen alone
  oldState = st; oldView = asGas; oldNumber = shownNumber;

  lcd.clear();
  switch (st) {
    case STANDBY:
      lcd.print("AWAITING RITUAL");
      break;

    case MONITORING:
      lcd.print("MONITORING");
      lcd.setCursor(0, 1);                                  // move to the second row
      if (asGas) { lcd.print("GAS:   "); lcd.print(gas); }
      else       { lcd.print("LIGHT: "); lcd.print(light); }
      break;

    case GAS_ALERT:
      lcd.print("TOXIC PURGE");
      lcd.setCursor(0, 1);
      lcd.print("GAS:   "); lcd.print(gas);
      break;

    case BLACKOUT:
      lcd.print("NOCTIS PROTOCOL");
      break;

    case HEAT_EMERGENCY:
      lcd.print("COOKED");
      lcd.setCursor(0, 1);
      lcd.print("IR RESET TO EXIT");
      break;

    case MULTI_FAULT:
      // the task's full line is "MULTIPLE PROBLEMS DETECTED", too long for
      // a 16-wide screen, so it's split across the two rows
      lcd.print("MULTIPLE");
      lcd.setCursor(0, 1);
      lcd.print("PROBLEMS DETECT!");
      break;
  }
}
