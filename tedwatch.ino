/*TED MODEL: 1001 MTU
100-130V~, 1W
50/60Hz
J4 is an 8-pin connector
2 Vcc  +side of C2 and C5 5.00v
3 GND  -side of C2 and C5
7 weird pulses, saved a logic capture, 0.43ms hi,50us lo,73us hi, 13us lo

pin 21 (4th from last) of U1 pulses low every 0.00118 watt-hours for 4uS
pin 22 (3rd from last) of U1 will also be low if direction is the other way
*/

#define VERSIONSTR "giant whatwatt display with YTD watt-hour numeric display"
#define BAUDRATE 57600

#include "ht1632c.h"
#include "font1.h"
#define HT1632_CS       6   // Chip Select (pin 1)
#define HT1632_CLK      4   // Clk pin (pin 2)
#define HT1632_WRCLK    12  // Write clock pin (pin 5)
// Sun May 14 13:00 --
#define HT1632_DATA     13  // Data pin (pin 7)
#define DISPLAYRATE 2000 // how many milliseconds to show each text display before switching

#define ENERGYPULSE     0.0012 // this many watt-hours have been used
float wattHours; // stores total counted energy
float wattage = 0; // what is our present measured wattage
unsigned long lastWattCalc = 0; // when's the last time we calculated wattage
unsigned long backupTimer = 0;
float lastWattCalcWattHours = 0.0; // what our energy count was last time we calculated wattage
#define WATTCALCTIME    2000 // how many milliseconds between recalculating wattage

#include <EEPROM.h>
#define WATTHOURS_EEPROM_ADDRESS 20 // long-term memory in EEPROM
#define BACKUP_INTERVAL 30000 // how often to write our energy to the EEPROM
#define WATTHOUR_RESET_PIN      7

ISR(PCINT0_vect) { // fire an interrupt when PB0 changes state
  if (PINB & _BV(PB0)) wattHours += ENERGYPULSE; // this pulse means ENERGYPULSE energy was used
}

void setup() {
  Serial.begin(BAUDRATE);
  Serial.println(VERSIONSTR);
  digitalWrite( WATTHOUR_RESET_PIN, HIGH );  // we want to read with pullup enabled
  load_watthours();
  PCICR |= _BV(PCIE0); //  Pin Change Interrupt enable on PCINT0
  PCMSK0 |= _BV(PCINT0); // enable PCINT0 for PB0
}

void loop() {
  Serial.print(wattHours);
  Serial.print("\t");
  wattage = (wattHours - lastWattCalcWattHours) * 3600;
  Serial.println(wattage);
  lastWattCalcWattHours = wattHours;
  delay(1000);
  storeEnergy();
}

void storeEnergy() {
  if( digitalRead( WATTHOUR_RESET_PIN ) ) {  // reset switch is not resetting
    if( millis() - backupTimer >= BACKUP_INTERVAL ) {  // store wattHours into eeprom
      store_watthours();
      backupTimer = millis();
    }
  } else {  // reset switch is resetting
    Serial.println("CLEAR WH COUNTER");
    reset_watthours();
    backupTimer = millis();
  }
}

union float_and_byte {
	float f;
	unsigned char bs[sizeof(float)];
} fab;

void store_watthours() {
  Serial.println( "Storing wattHours." );
  fab.f = wattHours;
  for(int i=0; i<sizeof(float); i++ )
    EEPROM.write( WATTHOURS_EEPROM_ADDRESS+i, fab.bs[i] );
}

void load_watthours() {
  Serial.print( "Loading watthours bytes 0x" );
  bool blank = true;
  for(int i=0; i<sizeof(float); i++ ) {
    fab.bs[i] = EEPROM.read( WATTHOURS_EEPROM_ADDRESS+i );
    Serial.print( fab.bs[i], HEX );
    if( blank && fab.bs[i] != 0xff )  blank = false;
  }
  wattHours = blank ? 0 : fab.f;
  Serial.print( ", so wattHours is " );
  Serial.print( wattHours );
  Serial.println( "." );
}

void reset_watthours() {
  load_watthours();
  if (wattHours != 0.0) {
    Serial.println("reset_watthours(): store_watthours()");
    wattHours = 0;
    store_watthours();
  } else {
    Serial.println("reset_watthours(): watthours already reset!");
  }
}
