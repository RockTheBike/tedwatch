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

#define DISPLAYRATE 1000 // how many milliseconds to show each text display before switching

#define ENERGYPULSE     0.0012 // this many watt-hours have been used
volatile float wattHours; // stores total counted energy
float wattage = 0; // what is our present measured wattage
unsigned long lastWattCalcTime = 0; // when's the last time we calculated wattage
unsigned long lastDisplay = 0; // when's the last time we did printDisplay
unsigned long backupTimer = 0;
float lastWattCalcWattHours = 0.0; // what our energy count was last time we calculated wattage
#define WATTCALCTIME    100 // how many milliseconds between recalculating wattage

#include <EEPROM.h>
#define WATTHOURS_EEPROM_ADDRESS 20 // long-term memory in EEPROM
#define BACKUP_INTERVAL 30000 // how often to write our energy to the EEPROM
#define WATTHOUR_RESET_PIN      6
#define POWER_STRIP_PIN         7
#define POWER_STRIP_PIXELS      40 // number of pixels in whatwatt power column
#define WATTHOUR_DISPLAY_PIN    4
#define WATTHOUR_DISPLAY_PIXELS (8*28) // actually 27 wide but leftmost doesn't exist
// bottom right is first pixel, goes up 8, left 1, down 8, left 1...
// https://www.aliexpress.com/item/8-32-Pixel/32225275406.html
#define ENERGYPULSE_PIN         8 // pin 8 = PB0 aka PCINT0_vect
#include <Adafruit_NeoPixel.h>
#include "font1.h"
uint32_t fontColor = Adafruit_NeoPixel::Color(64,64,25);
uint32_t backgroundColor = Adafruit_NeoPixel::Color(0,0,1);
Adafruit_NeoPixel wattHourDisplay = Adafruit_NeoPixel(WATTHOUR_DISPLAY_PIXELS, WATTHOUR_DISPLAY_PIN, NEO_GRB + NEO_KHZ800);
Adafruit_NeoPixel powerStrip = Adafruit_NeoPixel(POWER_STRIP_PIXELS, POWER_STRIP_PIN, NEO_GRB + NEO_KHZ800);

// scale the logarithmic displays
#define MIN_POWER 1
#define MAX_POWER 10000

ISR(PCINT0_vect) { // fire an interrupt when PB0 changes state
  if (PINB & _BV(PB0)) wattHours += ENERGYPULSE; // this pulse means ENERGYPULSE energy was used
}

void setup() {
  Serial.begin(BAUDRATE);
  Serial.println(VERSIONSTR);
  digitalWrite( WATTHOUR_RESET_PIN, HIGH );  // we want to read with pullup enabled
  load_watthours(); // read wattHours from EEPROM
  PCICR |= _BV(PCIE0); //  Pin Change Interrupt enable on PCINT0
  PCMSK0 |= _BV(PCINT0); // enable PCINT0 for PB0
  wattHourDisplay.begin();
  wattHourDisplay.show();
  powerStrip.begin(); // Initialize all pixels to 'off'
  powerStrip.show(); // Initialize all pixels to 'off'
}

void loop() {
  updateDisplay();
  printDisplay();
  updateWattage();
  // wattage = (millis()/100)%10000; // for testing powerStrip
  updatePowerStrip();
  storeEnergy();
}

void printDisplay() {
  if (millis() - lastDisplay > DISPLAYRATE) {
    Serial.print(wattHours);
    Serial.print("\t\t\t");
    Serial.println(wattage);
    lastDisplay = millis();
  }
}

void updateWattage() {
  if (millis() - lastWattCalcTime > WATTCALCTIME) {
    wattage = (wattHours - lastWattCalcWattHours) * 3600000 / (millis() - lastWattCalcTime);
    lastWattCalcWattHours = wattHours;
    lastWattCalcTime = millis();
  }
}

void updatePowerStrip(){
  float ledstolight;
  ledstolight = logPowerRamp(wattage);
  if( ledstolight > POWER_STRIP_PIXELS ) ledstolight=POWER_STRIP_PIXELS;
  unsigned char hue = ledstolight/POWER_STRIP_PIXELS * 170.0;
  uint32_t color = Wheel(powerStrip, hue<1?1:hue);
  static const uint32_t dark = Adafruit_NeoPixel::Color(0,0,0);
  doFractionalRamp(powerStrip, 0, POWER_STRIP_PIXELS, ledstolight, color, dark);
  powerStrip.show();
}

float logPowerRamp( float p ) {
  float l = log(p/MIN_POWER)*POWER_STRIP_PIXELS/log(MAX_POWER/MIN_POWER);
  return l<0 ? 0 : l;
}

// Input a value 0 to 255 to get a color value. The colours transition rgb back to r.
uint32_t Wheel(const Adafruit_NeoPixel& strip, byte WheelPos) {
  if (WheelPos < 85) {
    return strip.Color(255 - WheelPos * 3, WheelPos * 3, 0);
  } else if (WheelPos < 170) {
    WheelPos -= 85;
    return strip.Color(0, 255 - WheelPos * 3, WheelPos * 3);
  } else {
    WheelPos -= 170;
    return strip.Color(WheelPos * 3, 0, 255 - WheelPos * 3);
  }
}

uint32_t dim(uint32_t c){  // a full RGB color
#if ENABLE_DIMMING
  return Adafruit_NeoPixel::Color(
  dim( (uint8_t)( (c>>16) & 0xff ) ),
  dim( (uint8_t)( (c>>8 ) & 0xff ) ),
  dim( (uint8_t)( (c>>0 ) & 0xff ) ) );
#else
  return c;
#endif
}

void doFractionalRamp(const Adafruit_NeoPixel& strip, uint8_t offset, uint8_t num_pixels, float ledstolight, uint32_t firstColor, uint32_t secondColor){
  for( int i=0,pixel=offset; i<=num_pixels; i++,pixel++ ){
    uint32_t color;
    if( i<(int)ledstolight )  // definitely firstColor
        color = firstColor;
    else if( i>(int)ledstolight )  // definitely secondColor
        color = secondColor;
    else  // mix the two proportionally
        color = weighted_average_of_colors( firstColor, secondColor, ledstolight-(int)ledstolight);
    strip.setPixelColor(pixel, dim(color));
  }
}

uint32_t weighted_average_of_colors( uint32_t colorA, uint32_t colorB, float fraction ){
  // TODO:  weight brightness to look more linear to the human eye
  uint8_t RA = (colorA>>16) & 0xff;
  uint8_t GA = (colorA>>8 ) & 0xff;
  uint8_t BA = (colorA>>0 ) & 0xff;
  uint8_t RB = (colorB>>16) & 0xff;
  uint8_t GB = (colorB>>8 ) & 0xff;
  uint8_t BB = (colorB>>0 ) & 0xff;
  return Adafruit_NeoPixel::Color(
    RA*fraction + RB*(1-fraction),
    GA*fraction + GB*(1-fraction),
    BA*fraction + BB*(1-fraction) );
}

void updateDisplay() {
  char buf[]="    "; // stores the number we're going to display
  //sprintf(buf,"%4d",millis()/100);// for testing display
  sprintf(buf,"%4d",(int)(wattHours / 100));
  writeWattHourDisplay(buf);
}

void writeWattHourDisplay(char* text) {
#define DISPLAY_CHARS   4 // number of characters in display
#define FONT_W 7 // width of font
#define FONT_H 8 // height of font
  for (int textIndex=0; textIndex<DISPLAY_CHARS; textIndex++) {
    char buffer[FONT_H][FONT_W]; // array of horizontal lines, top to bottom, left to right
    for(int fontIndex=0; fontIndex<sizeof(charList); fontIndex++){ // charList is in font1.h
      if(charList[fontIndex] == text[textIndex]){ // if fontIndex is the index of the desired letter
        int pos = fontIndex*FONT_H; // index into CHL where the character starts
        for(int row=0;row<FONT_H;row++){ // for each horizontal row of pixels
          memcpy_P(buffer[row], (PGM_P)pgm_read_word(&(CHL[pos+row])), FONT_W); // copy to buffer from flash
        }
      }
    }
    for (int fontXIndex=0; fontXIndex<FONT_W; fontXIndex++) {
      for (int fontYIndex=0; fontYIndex<FONT_H; fontYIndex++) {
        uint32_t pixelColor = buffer[fontYIndex][FONT_W-1-fontXIndex]=='0' ? fontColor : backgroundColor; // here is where the magic happens
        if ((FONT_W*(DISPLAY_CHARS-1-textIndex) + fontXIndex) & 1) { // odd columns are top-to-bottom
          wattHourDisplay.setPixelColor((FONT_H*FONT_W)*(DISPLAY_CHARS-1-textIndex) + fontXIndex*FONT_H +           fontYIndex ,pixelColor);
        } else { // even columns are bottom-to-top
          wattHourDisplay.setPixelColor((FONT_H*FONT_W)*(DISPLAY_CHARS-1-textIndex) + fontXIndex*FONT_H + (FONT_H-1-fontYIndex),pixelColor);
        }
      }
    }
  }
  wattHourDisplay.setPixelColor((FONT_W-1)*FONT_H+0,fontColor); // light up the decimal point
  wattHourDisplay.setPixelColor((FONT_W  )*FONT_H+7,backgroundColor); // keep decimal point visible
  wattHourDisplay.setPixelColor((FONT_W-2)*FONT_H+7,backgroundColor); // keep decimal point visible
  wattHourDisplay.show(); // send the update out to the LEDs
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
