
/* 
ARC Inductions 2026-2027 - Electrical State Machine 
Task 1: Arduino Master-Slave Monitoring System - SCREEN board 
Name: Pradyumna Pathak 
ID: 2025AAPS0291H 
 
Tinkercad link: https://www.tinkercad.com/things/8yMoD9Ju7rH-electrical-state-machine 
*/ 
 
// these are the exact headers used in the SEDS Tinkercad project 
#include <Wire.h> 
#include <Adafruit_LiquidCrystal.h> 
// tinkercad related error, had to fix by suppressing some invalid file headers in tinkercad, basically used some other version
#define DECODE_NEC 
#define EXCLUDE_UNIVERSAL_PROTOCOLS 
#define EXCLUDE_EXOTIC_PROTOCOLS 
#include <IRremote.h> 
 
const int irPin = 2;              // the IR receiver's signal pin 
Adafruit_LiquidCrystal lcd(0x20); // I2C LCD at address 0x20
 
const int myAddress = 8;   // this board listens for I2C messages sent to this number (master uses the same one) 
 
// same 6 states, same order as the master file 
enum State { STANDBY, MONITORING, GAS_ALERT, BLACKOUT, HEAT_EMERGENCY, MULTI_FAULT }; 
 
// same button codes as the master file 
const byte noButton = 0, onButton = 1, resetButton = 2, switchButton = 3; 
 
/* use the appropriate remote codes needed to configure, i used 3 buttons by manually checking the codes
*/ 

const unsigned long onCode     = 0xFFA25D;  
const unsigned long resetCode  = 0xFF629D;  
const unsigned long switchCode = 0xFF6897;  

 
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

  /* 
  The LCD uses I2C. It has its own I2C address, 0x20, while this
  Arduino has address 8 for communication with the Master.
  */ 
  Wire.begin(myAddress);            
  Wire.onReceive(whenDataArrives);  
  Wire.onRequest(whenAsked);        
 
  lcd.begin(16, 2);          // initialize a 16-column, 2-row LCD
  lcd.setBacklight(HIGH);    // turn the LCD backlight on
  lcd.clear();               // clear any previous characters
  IrReceiver.begin(irPin);   // start listening for the remote
} 
 
void loop() { 
  checkRemote();     // check whether a remote button was pressed
  updateScreen();    // update the LCD when the displayed information changes
} 
 
/* 
fires on its own the moment the master sends its 6 bytes. unpack them 
back into our variables. keep it quick, it barges in on whatever loop() 
was doing. 
*/ 
void whenDataArrives(int count) { 
  if (count < 6) {                          
    while (Wire.available()) Wire.read(); 
    return; 
  } 
  theState = Wire.read();                      
  gasView  = Wire.read();                      
  lightNum = (Wire.read() << 8) | Wire.read(); 
  gasNum   = (Wire.read() << 8) | Wire.read(); 
} 
 
/* 
fires on its own when the master asks us for the last button. send it, 
then wipe it so the same press can't be sent twice. 
*/ 
void whenAsked() { 
  Wire.write(lastButton); 
  lastButton = noButton; 
} 
 
/* 
checks if the remote got pressed, and if it did, turns whatever code it 
sent into one of our button numbers. 
The hex codes above must match the codes produced by the actual 
Tinkercad remote buttons. The Serial Monitor prints the received code 
so the appropriate code can be entered above. 
*/ 
void checkRemote() { 
  if (!IrReceiver.decode()) return; 
 
  unsigned long code = IrReceiver.decodedIRData.decodedRawData; 
 
  if      (code == onCode)     lastButton = onButton; 
  else if (code == resetCode)  lastButton = resetButton; 
  else if (code == switchCode) lastButton = switchButton; 
 
  Serial.print("button code: 0x");    
  Serial.println(code, HEX); 
  IrReceiver.resume();                 
} 
 
/* 
puts the right words on the LCD for whatever state we're in. it only 
actually redraws when something's changed since last time, otherwise 
the screen is left alone to prevent unnecessary flickering. 
*/ 
void updateScreen() { 

  /* 
  Copy the shared values before using them so that the display code
  works with one consistent set of values.
  */ 
  noInterrupts(); 
  byte st    = theState; 
  byte asGas = gasView; 
  int  light = lightNum; 
  int  gas   = gasNum; 
  interrupts(); 
 
  // remember what was displayed last time
  static int oldState = -1, oldView = -1, oldNumber = -1; 
 
  // redraw live sensor values when they change
  int shownNumber = asGas ? gas : light; 
  bool numberMoved = (st == MONITORING || st == GAS_ALERT) && shownNumber != oldNumber; 
 
  if (st == oldState && asGas == oldView && !numberMoved) return; 
 
  oldState = st; 
  oldView = asGas; 
  oldNumber = shownNumber; 
 
  // clear the LCD before displaying the new state
  lcd.clear(); 
 
  switch (st) { 

    case STANDBY: 
      lcd.print("AWAITING RITUAL"); 
      break; 
 
    case MONITORING: 
      lcd.print("MONITORING"); 
      lcd.setCursor(0, 1); 
      if (asGas) { 
        lcd.print("GAS:   "); 
        lcd.print(gas); 
      } 
      else { 
        lcd.print("LIGHT: "); 
        lcd.print(light); 
      } 
      break; 
 
    case GAS_ALERT: 
      lcd.print("TOXIC PURGE"); 
      lcd.setCursor(0, 1); 
      lcd.print("GAS:   "); 
      lcd.print(gas); 
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
      // full message is longer than 16 characters, so it is split across two rows
      lcd.print("MULTIPLE"); 
      lcd.setCursor(0, 1); 
      lcd.print("PROBLEMS DETECT!"); 
      break; 
  } 
}