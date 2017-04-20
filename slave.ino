// Author: Xing Tong; xingtongrpi@163.com
// Author: Antonio Fiol-Mahon; afiolmahon@gmail.com
// Member of Team 3: Pencil Pushers
// This coding is intended for Arduino UNO R3; compatibility issue may arise for different model
#include <Arduino.h>

// AVR library
#ifdef __AVR__
  #include <avr/power.h>    // power management
  #include <avr/pgmspace.h> // flash memory access
  #include <avr/sleep.h>    // sleep access
#endif

// library for RF24 Communication; https://github.com/TMRh20/RF24
#include <nRF24L01.h>
#include <printf.h>
#include <RF24.h>
#include <RF24_config.h>
#include <SPI.h>

// library for Adafruit NeoPixel; https://github.com/adafruit/Adafruit_NeoPixel
#include <Adafruit_NeoPixel.h>

// =====Device Identity===== =====CHANGE THIS=====
#define DEVICE_ID     0 // for student device, range from 0 to 4 (30 in future), for teacher device, 32
#define DEVICE_CLASS  0 // range from 0 to 127

// =====Pin-out lists=====
// 08:  Switch 0
#define SWITCH0 8
// GND: NRF24 GND 
// 5V:  NRF24 VCC
// 09:  NRF24 CE
// 10:  NRF24 CSN
// 11:  NRF24 MOSI

// 12:  NRF24 MISO
// 13:  NRF24 SCK
#define PIN_CE  9
#define PIN_CSN 10
RF24 radio ( PIN_CE, PIN_CSN );

// 14(A0): LED Strip data line
#define LED_DATA 14 // Digital Output for LED Control
#define NUM_LEDS 7  // Number of LEDS connected
#define BRIGHTNESS 50

Adafruit_NeoPixel strip = Adafruit_NeoPixel(NUM_LEDS, LED_DATA, NEO_GRB + NEO_KHZ800);

// list of pipe corresponding to each master-slave connection; TODO: populate the list to >30 elements
const uint64_t INQURIES_PIPE[2] PROGMEM = { 0xC2C2C2C2C2LL, 0xF0F0F0F008LL };
// RESPONSE_PIPE is used for slave to send info to masters
const uint64_t RESPONSE_PIPE[2] PROGMEM = { 0xC2C2C2C2C2LL, 0xF0F0F0F00ALL };

// global variables used in sending and receiving; pl = payload
unsigned char payload[8] = { 0, 0, 0, 0, 0, 0, 0, 0 };
unsigned char pl_class;
unsigned char pl_recipient;
unsigned char pl_data;
unsigned char pl_reserved;

// utility functions for lighting up leds
void LED_Change( uint32_t c ) {
  for(int i=0; i<strip.numPixels(); i++ ) {
    strip.setPixelColor(i, c);
  }
  strip.show();
}

void radio_setup(void) { // Configure RF24
  radio.begin();
  radio.setRetries(15,15);
  radio.setDataRate(RF24_250KBPS);
  radio.setPALevel( RF24_PA_MAX ); // operating at maximum power
  radio.setPayloadSize(8); // NEED THIS to work with raspi
  radio.setChannel(0x60);  // NEED THIS to work with raspi
  radio.startListening(); // Begin Listening
  radio.printDetails();
}

void led_setup(void) {
 // set up LED strip
  strip.setBrightness(BRIGHTNESS);
  strip.begin();
  strip.show(); // Initialize all pixels to 'off'
  delay(10);
}


bool rf_read() {
    for (int i = 0; i < 4; ++i) {
      payload[i] = 0; // Clear the buffer
    }
    pl_class      = 0; // VOID the data before listening
    pl_recipient  = 0;
    pl_data       = 0;
    pl_reserved   = 0;
    radio.openReadingPipe( 1, INQURIES_PIPE[DEVICE_ID] );  // note that pipe 0 is used by writing pipe
    radio.startListening();
    // WAIT until a radio signal is available
    for (int j = 0; j < 40; ++j ) {
      if ( radio.available() ) {
        radio.read( payload, 8 );
        pl_class      = payload[0];
        pl_recipient  = payload[1];
        pl_data       = payload[2]; // 1: inqury; 64: light up
        pl_reserved   = payload[3];
        //Serial.print("[MESG_DEBUG]: received inqury: reserved "); Serial.println(pl_reserved);
        break;
      }
      delay(2);
    }
    radio.stopListening();
    radio.closeReadingPipe(1);
    if ( payload[0] == 0 && payload[1] == 0 && payload[2] == 0 && payload[3] == 0 ) {
      return false;
    } else { // Print Received Data
      Serial.print("IN >>>  [");
      for (int i = 0; i < 4; ++i) {
        Serial.print(payload[i]);       Serial.print( ' ');
      }
      Serial.println(" ]");
      return true;
    }
}

void rf_write(int op, int operand) {
  radio.openWritingPipe( RESPONSE_PIPE[DEVICE_ID] );
  payload[0] = pl_class;
  payload[1] = pl_recipient;
  payload[2] = op;
  payload[3] = operand;
  for (int j = 0; j < 100; ++j ) {
    bool write_status = radio.write( payload , 8 );
    if ( write_status == true ) {
      Serial.print("<<< OUT [");
      for (int i = 0; i < 4; ++i) {
        Serial.print(payload[i]);       Serial.print( ' ');
      }
      Serial.println(" ]");
      break;
    }
    delay(2);
  }
  Serial.print("OUT FAILED [");
  for (int i = 0; i < 4; ++i) {
    Serial.print(payload[i]);       Serial.print( ' ');
  }
  Serial.println(" ]");
}

// perform set-ups and other one-time task
void setup(void) {
  // set up serial
  Serial.begin(115200); // monitor should be set to the same baud rate as well
  printf_begin();
  radio_setup(); // Setup Radio
  led_setup();   // Setup LED strip
  pinMode(SWITCH0, INPUT_PULLUP); // Initialize Button
  LED_Change(strip.Color(100, 0, 0));
}

void loop(void) {
  if (rf_read()) {
    // POST-RX PROCESSING
    // Operation Code Definition
    /* 10: [MASTER] Open Question
     * 16: [MASTER] Check button
     * 17: [SLAVE ] Button On
     * 18: [SLAVE ] Button Off
     * 32: [MASTER] Light Up LED
     * 33: [MASTER] Light Off LED
     * 34: [MASTER] ACKL, Change LED
     */
    switch (pl_data) { // Opcode Processing
      case 10:
        LED_Change(strip.Color(0, 0, 100));
        break;
    
      case 16: // Check Button
        if ( digitalRead(SWITCH0) == LOW ) {
          rf_write(17, 0); // Response is Button On
        } else {
          rf_write(18, 0); // Response is Button Off
        }
        break;
        
      case 32: // Turn LED On
        LED_Change( strip.Color(0, 200, 0) );
        break;
        
      case 33: // Turn LED Off
        LED_Change( strip.Color(200, 0, 0) );
        break;
    }
  }
}
