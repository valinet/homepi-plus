/*
 * D2 - Optocoupler 1 3.3V (ThinkPad)
 * D4 - Optocoupler 2 3.3V (PC)
 * D5 - Optocoupler 3 3.3V (Speakers)
 * 
 * A4 - SDA (DDC/CI)
 * A5 - SCL (DDC/CI)
 * 
 * D10 - CS (EtherCard)
 * D11 - MOSI (EtherCard)
 * D12 - MISO (EtherCard)
 * D13 - SCK (EtherCard)
 * 
 * A3 - HDMI switch On / Off
 * A6 - Relay 1 (Desklamp)
 * A7 - Relay 2
 * 
 * D3 - IR (+) pin
 * A1 - IR (-) Lightbulb
 * A2 - IR (-) Nightbulb
 */
 
#include <avr/wdt.h>
#include <EEPROM.h>
// i2c
#include "src/ArduinoCore-avr/libraries/Wire/src/Wire.h"
// IR
#include "src/IRLib2/IRLibProtocols/IRLibSendBase.h"
#include "src/IRLib2/IRLibProtocols/IRLib_P01_NEC.h"
// enc28j60
#include "src/EtherCard/src/EtherCard.h"
// MDNS
#define USE_MDNS
#ifdef USE_MDNS
#include "src/EtherCard-MDNS/EC_MDNSResponder.h"
#endif
// TOTP
#include "src/TOTP-Arduino/src/TOTP.h"
// header file containing secret for TOTP
#include "C:\KEYS\key.h" // contains KEY define

int reset_pin = 9;
int latchPin = 4;
int clockPin = 2;
int dataPin = 5;
int oePin = 6;
byte port0;
byte port1;
#define CTL_PIN_TP0              1     //2
#define CTL_PIN_PC0              2     //4
#define CTL_PIN_AUDIO0           3     //5
#define CTL_PIN_HDMI0            4     //A3
byte pin_hdmi = 0;
#define CTL_PIN_DESKLAMP0        5     //A6
byte pin_desklamp = 0;
#define CTL_PIN_RELAY0           6     //A7
byte pin_relay = 0;
#define CTL_PIN_IR_LIGHTBULB0    7     //A1
#define CTL_PIN_IR_NIGHTBULB1    1     //A2
#define CTL_PIN_AUDIO_INPUT1     2    //A0
#define CTL_PIN_DELOCK1          3
byte pin_ainput = 0;

#define STR_FAILED "[FAILED] "

#define DDC_MAXIMUM_RETRY 2
#define DDC_BRIGHTNESS 0x10
#define DDC_VOLUME 0x62
#define DDC_INPUT 0x60
#define DDC_INPUT_VGA 1
#define DDC_INPUT_DVI 3
#define DDC_INPUT_DP 15
#define DDC_INPUT_HDMI 17
#define DDC_DELAY 50

#define HTTP_PORT 80
#define ETHER_BUFLEN 300
#define ETHER_MINCHUNKLEN ETHER_BUFLEN - 20
#define POST_VAL_SIZE 6

static byte mymac[] = { 0x74, 0x69, 0x69, 0x2D, 0x30, 0x31 }; //0x31
const static uint8_t ip[] = {192,168,105,24}; //24
const static uint8_t gateway[] = {192,168,105,1};
const static uint8_t mask[] = {255,255,255,0};
const static uint8_t dns[] = {8,8,8,8}; // Google DNS resolver
const static uint8_t ntpIp[] = {216,239,35,12}; // Google NTP server
#define HOSTNAME "homepi"
#define SERVICE_NAME "_valinet._tcp"

#define HOSTNAME_LEN 30
char hostname[HOSTNAME_LEN];
#define HOSTADDR_LEN 40
char hostaddr[HOSTADDR_LEN];

byte Ethernet::buffer[ETHER_BUFLEN];
BufferFiller buffer;

IRsendNEC irsend;
#define OSRAM_LIGHT_ON 0xFFE01F
#define OSRAM_LIGHT_OFF 0xFF609F

#define DELOCK_POWER 0xFF00FF
#define DELOCK_CH1 0xFF10EF
#define DELOCK_CH2 0xFF906F
#define DELOCK_CH3 0xFF50AF
#define DELOCK_CH4 0xFF30CF
#define DELOCK_CH5 0xFF708F
#define DELOCK_PREV 0xFF28D7
#define DELOCK_NEXT 0xFF6897

// NTP requests are to port 123
const unsigned int NTP_REMOTEPORT = 123;
// Local UDP port to use           
const unsigned int NTP_LOCALPORT = 8888;             
// NTP time stamp is in the first 48 bytes of the message
const unsigned int NTP_PACKET_SIZE = 48;             

// generate a key using http://www.lucadentella.it/OTP/
uint8_t hmacKey[] = KEY;
TOTP totp = TOTP(hmacKey, 16);
#define NUM_DIGITS 8

// keeps track of millis() so that the clock is
// resyncronized with the NTP server once it overflows
unsigned long prev_millis = 10000000;
// time between last millis() reset and last sync
// with the NTP server
unsigned long millis_offset = 0;
// UNIX time obtained from the NTP server
unsigned long epoch = 0;

// number of allowed web requests from the public Internet
// the counter resets with each new TOTP code generated
#define MAX_ATTEMPTS 2
uint8_t attempts = 0;
// previous TOTP code generated (helps in resetting above
// counter)
unsigned int last_code = 0;

#define EEPROM_DESKLAMP 0
#define RESPRING_DELAY 60 // in seconds

void(*reset) (void) = 0;

////void print_time()
////{
  ////char buf[20];
  ////snprintf(buf, 20, "[%15d] ", millis());
  ////Serial.print(buf);
////}

// taken from NTP example in EtherCard
void sendNTPpacket(const uint8_t* remoteAddress) {
  // buffer to hold outgoing packet
  char packetBuffer[ NTP_PACKET_SIZE];
  // set all bytes in the buffer to 0
  memset(packetBuffer, 0, NTP_PACKET_SIZE);
  // Initialize values needed to form NTP request
  // (see URL above for details on the packets)
  packetBuffer[0] = 0b11100011;   // LI, Version, Mode
  packetBuffer[1] = 0;            // Stratum, or type of clock
  packetBuffer[2] = 6;            // Polling Interval
  packetBuffer[3] = 0xEC;         // Peer Clock Precision
  // 8 bytes of zero for Root Delay & Root Dispersion
  packetBuffer[12]  = 49;
  packetBuffer[13]  = 0x4E;
  packetBuffer[14]  = 49;
  packetBuffer[15]  = 52;

  // all NTP fields have been given values, now
  // you can send a packet requesting a timestamp:
  ether.sendUdp(
    packetBuffer, 
    NTP_PACKET_SIZE, 
    NTP_LOCALPORT, 
    remoteAddress, 
    NTP_REMOTEPORT
  );
}

void udpReceiveNtpPacket(
  uint8_t dest_ip[IP_LEN], 
  uint16_t dest_port, 
  uint8_t src_ip[IP_LEN], 
  uint16_t src_port, 
  const char *packetBuffer, 
  uint16_t len
)
{
  // the timestamp starts at byte 40 of the received packet and is four bytes,
  // or two words, long. First, extract the two words:
  unsigned long highWord = word(packetBuffer[40], packetBuffer[41]);
  unsigned long lowWord = word(packetBuffer[42], packetBuffer[43]);
  // combine the four bytes (two words) into a long integer
  // this is NTP time (seconds since Jan 1 1900):
  unsigned long secsSince1900 = highWord << 16 | lowWord;
  // Unix time starts on Jan 1 1970. In seconds, that's 2208988800:
  const unsigned long seventyYears = 2208988800UL;
  // subtract seventy years:
  epoch = secsSince1900 - seventyYears;
  millis_offset = millis();

  // print Unix time:
  Serial.print("Unix time = ");
  Serial.println(epoch);

  ether.udpServerPauseListenOnPort(NTP_LOCALPORT);
}

void shiftOut(int myDataPin, int myClockPin, byte myDataOut) {
  int i=0;
  int pinState;
  pinMode(myClockPin, OUTPUT);
  pinMode(myDataPin, OUTPUT);
  digitalWrite(myDataPin, 0);
  digitalWrite(myClockPin, 0);
  for (i = 7; i >= 0; i--)
  {
    digitalWrite(myClockPin, 0);
    if (myDataOut & (1 << i)) 
    {
      pinState= 1;
    } 
    else 
    {
      pinState= 0;
    }
    digitalWrite(myDataPin, pinState);
    digitalWrite(myClockPin, 1);
    digitalWrite(myDataPin, 0);
  }
  digitalWrite(myClockPin, 0);
}

void shift_ports()
{
  digitalWrite(latchPin, 0);
  shiftOut(dataPin, clockPin, port0);
  shiftOut(dataPin, clockPin, port1);
  digitalWrite(latchPin, 1);
}

void ddc_set(byte what, byte val)
{
  Wire.beginTransmission(0x37);
  byte error = Wire.endTransmission();
  if (error != 0) {
    return;
  }
  byte ddc[] = {
    0x51,        // subdevice
    0x04 | 0x80, // length (no of bytes from next to checksum) | 0x80
    0x03,        // opcode - write
    what,        // vcpcode to write
    0x00,        // value hibyte
    val,         // value lobyte
    (0x37 << 1) ^ 0x51 ^ (0x04 | 0x80) ^ 0x03 // checksum
  };
  ddc[6] ^= what ^ val;
  Wire.beginTransmission(0x37);
  Wire.write(ddc, 7);
  Wire.endTransmission();
  delay(DDC_DELAY);
}

char ddc_get(byte what)
{
  byte ddc[] = {
    0x51,        // subdevice
    0x02 | 0x80, // length (no of bytes from next to checksum) | 0x80
    0x01,        // opcode - read
    what,        // vcpcode to read
    (0x37 << 1) ^ 0x51 ^ (0x02 | 0x80) ^ 0x01 // checksum
  };
  ddc[4] ^= what;
  Wire.beginTransmission(0x37);
  Wire.write(ddc, 5);
  Wire.endTransmission();
  delay(DDC_DELAY);
  Wire.requestFrom(0x37, 2);
  if (Wire.available() <= 2)
  {
    if (Wire.read() == (0x37 << 1))
    {
      byte ret = 0, chk = 0, chk_rv = 0;
      byte len = Wire.read();
      chk = chk ^ ((0x37 << 1) + 1) ^ 0x51 ^ len;
      if (len & (1 << 7))
      {
        len &= ~(1 << 7);
      }
      Wire.requestFrom(0x37, len + 1);
      if (Wire.available() <= len + 1)
      {
        for (int i = 0; i < len + 1; ++i)
        {
          byte b = Wire.read();
          if (i == len) chk_rv = b;
          else chk = chk ^ b;
          if (i == len - 1) ret = b; 
        }
        if (chk == chk_rv)
        {
          return ret;
        }
      }
    }
  }
  return -1;
}
int mcusr = 0;
void setup() 
{ 
  mcusr = MCUSR;
  MCUSR = 0;
  wdt_disable();
  wdt_enable(WDTO_8S);
  
  digitalWrite(reset_pin, HIGH);
  delay(50);
  pinMode(reset_pin, OUTPUT);
  digitalWrite(reset_pin, HIGH);
  
  Serial.begin(9600);
  while (!Serial);
  Serial.println("\n");
  Serial.print("MCUSR: ");
  Serial.println(mcusr);
  Serial.println(millis());  
  
  digitalWrite(SDA, LOW);
  digitalWrite(SCL, LOW);

  pinMode(latchPin, OUTPUT);
  port0 = 0;
  port1 = 0;
  port0 &= ~(1 << CTL_PIN_TP0);
  port0 &= ~(1 << CTL_PIN_PC0);
  port0 &= ~(1 << CTL_PIN_AUDIO0);
  port0 &= ~(1 << CTL_PIN_HDMI0);
  pin_hdmi = 0;
  if (!mcusr || (mcusr & _BV(WDRF)))
  {
    pin_desklamp = EEPROM.read(EEPROM_DESKLAMP);
    if (pin_desklamp)
    {
      port0 |= (1 << CTL_PIN_DESKLAMP0);
    }
    else
    {
      port0 &= ~(1 << CTL_PIN_DESKLAMP0);
    }
  }
  else
  {
    pin_desklamp = EEPROM.read(EEPROM_DESKLAMP);
    if (pin_desklamp)
    {
      EEPROM.write(EEPROM_DESKLAMP, 0);
    }
    port0 &= ~(1 << CTL_PIN_DESKLAMP0);
    pin_desklamp = 0;
  }
  port0 &= ~(1 << CTL_PIN_RELAY0);
  pin_relay = 0;
  port0 |= (1 << CTL_PIN_IR_LIGHTBULB0);
  port1 |= (1 << CTL_PIN_IR_NIGHTBULB1);
  port1 &= ~(1 << CTL_PIN_AUDIO_INPUT1);
  pin_ainput = 0;
  port1 |= (1 << CTL_PIN_DELOCK1);
  shift_ports();
  pinMode(oePin, OUTPUT);
  digitalWrite(oePin, LOW);

  if (mcusr && !(mcusr & _BV(WDRF)))
  {
    port0 &= ~(1 << CTL_PIN_IR_LIGHTBULB0);
    shift_ports();
    irsend.send(OSRAM_LIGHT_OFF, 32);
    delay(50);
    irsend.send(OSRAM_LIGHT_OFF, 32);
    delay(50);
    irsend.send(OSRAM_LIGHT_OFF, 32);
    port0 |= (1 << CTL_PIN_IR_LIGHTBULB0);
    shift_ports();
    
    port1 &= ~(1 << CTL_PIN_IR_NIGHTBULB1);
    shift_ports();
    irsend.send(OSRAM_LIGHT_OFF, 32);
    delay(50);
    irsend.send(OSRAM_LIGHT_OFF, 32);
    delay(50);
    irsend.send(OSRAM_LIGHT_OFF, 32);
    port1 |= (1 << CTL_PIN_IR_NIGHTBULB1);
    shift_ports();
    
    port1 &= ~(1 << CTL_PIN_DELOCK1);
    shift_ports();
    irsend.send(DELOCK_POWER, 32);
    port1 |= (1 << CTL_PIN_DELOCK1);
    shift_ports();
  }
  else
  {
    delay(1000);
  }
  
  // enable i2c
  Wire.begin();
  Wire.setWireTimeout();
  
  if (ether.begin(sizeof Ethernet::buffer, mymac) == 0)
  {
  }

  ether.staticSetup(ip, gateway, dns, mask);
  /*while (!ether.dhcpSetup())
  {
    delay(1000);
  }*/

  ether.hisport = HTTP_PORT;
  ether.printIp("EtherCard: IP - ", ether.myip);

  int timeout = 0;
  while (!ether.isLinkUp() && timeout < 2000)
  {
    if (!(timeout % 20))
    {
      Serial.print("timeout");
      Serial.print(1);
      Serial.print(": ");
      Serial.println(timeout);
    }
    timeout++;
    delay(1);
  }
  if (!ether.isLinkUp())
  {
    digitalWrite(reset_pin, LOW);
    delay(10);
    reset();
  }
  
  timeout = 1;
  while(ether.clientWaitingGw() && timeout < 3000)
  {
    if (!(timeout % 20))
    {
      Serial.print("timeout");
      Serial.print(2);
      Serial.print(": ");
      Serial.println(timeout);
    }
    ether.packetLoop(ether.packetReceive());
    timeout++;
    delay(1);
  }
  if (ether.clientWaitingGw())
  {
    digitalWrite(reset_pin, LOW);
    delay(10);
    reset();
  }
  
  ether.printIp("NTP: ", ntpIp);
  ether.udpServerListenOnPort(&udpReceiveNtpPacket, NTP_LOCALPORT);
  sendNTPpacket(ntpIp);

  #ifdef USE_MDNS
  byte r = mdns.begin(
    ether, 
    HOSTNAME,
    SERVICE_NAME,
    HTTP_PORT,
    0x0000FFFF
  );
  if(r) {
    Serial.print("MDNS FAIL: ");
    Serial.println(r);
  } else {
    Serial.println("MDNS OK.");
  }
  //ENC28J60::enableBroadcast();
  ENC28J60::enableMulticast();
  //ether.enableMulticast();
  #endif

  wdt_reset();
}

// of course this can be minified but I leave it as is to have some extra space when time will require so
const char page[] PROGMEM =
    "   <meta name=\"HandheldFriendly\" content=\"true\" />\n"
    "   <meta name=\"viewport\" content=\"width=device-width, height=device-height, user-scalable=no\" />\n"
    "   <meta name=\"apple-mobile-web-app-capable\" content=\"yes\">\n"
    "   <link rel=\"apple-touch-icon\" href=\"https://gist.githubusercontent.com/valinet/f54f5d965b3bca744a1b247367351346/raw/a.png\">\n"
    "   <title>homepi+</title>\n"
    "   <style>"
    "   .cssradio label{margin-left: 10px;}.cssradio input[type=\"radio\"]{display:none}.cssradio input[type=\"checkbox\"]{display:none}.cssradio label{padding:10px;margin:15px 0 0 0;background:rgba(255,255,255,.8);border-radius:3px;display:inline-block;color:#000;cursor:pointer;border:1px solid #000;width:48%}.cssradio input:checked+label{background:#6d56ff;font-weight:700;color:#fff;border-color:#000}:root{color-scheme:light dark}*{box-sizing:border-box;font-family:\"Helvetica Neue\",serif}.slidercontainer{width:96%}.slider{-webkit-appearance:none;width:102%;height:15px;border-radius:5px;background:#d3d3d3;outline:none;opacity:.7;-webkit-transition:.2s;transition:opacity .2s}.slider::-webkit-slider-thumb{-webkit-appearance:none;appearance:none;width:25px;height:25px;border-radius:50%;background:#6d56ff;cursor:pointer}.slider::-moz-range-thumb{width:25px;height:25px;border-radius:50%;background:#6d56ff;cursor:pointer}.container{border-radius:5px;background-color:rgba(0,0,0,.1);padding:20px}.col-25{float:left;width:25%;margin-top:6px}.col-75{float:left;width:75%;margin-top:6px}.row:after{content:\"\";display:table;clear:both}.heading{// padding-top:14px;// padding-bottom:75px;position:relative}.widthel{width:750px}@media screen and (max-width:700px){.col-25,.col-75,input[type=submit]{width:100%;margin-top:0}.copyright{text-align:center}.container,.heading,.widthel{width:100%}}.iconAnchor{text-decoration:none}.iconImg{margin-top:10px;margin-right:15px;border-radius:50%;padding-top:4px;border:1px solid #000;width:40px;height:40px}.silentLink{color:inherit;text-decoration:inherit}"
    "   </style>\n"
    /*"   <style>\n"
    "     .cssradio input[type=\"radio\"]{display: none;}\n"
    "     .cssradio input[type=\"checkbox\"]{display: none;}\n"
    "     .cssradio label{padding: 10px; margin: 15px 10px 0 0;\n"
    "     background: rgba(255, 255, 255, 0.8); border-radius: 3px;\n"
    "     display: inline-block; color: black;\n"
    "     cursor: pointer; border: 1px solid #000; width: 46%}\n"
    "     .cssradio input:checked + label {\n"
    "     background: green; font-weight:bold; color: white; border-color: green;}\n"
    "     :root {\n"
    "       color-scheme: light dark;\n"
    "     }\n"
    "     \n"
    "     * {\n"
    "       box-sizing: border-box;\n"
    "       font-family: \"Helvetica Neue\", Helvetica Neue, serif;\n"
    "     }\n"
    "     \n"
    "     .slidercontainer {\n"
    "       width: 100%;\n"
    "     }\n"
    "     \n"
    "     .slider {\n"
    "       -webkit-appearance: none;\n"
    "       width: 102%;\n"
    "       height: 15px;\n"
    "       border-radius: 5px;\n"
    "       background: #d3d3d3;\n"
    "       outline: none;\n"
    "       opacity: 0.7;\n"
    "       -webkit-transition: .2s;\n"
    "       transition: opacity .2s;\n"
    "     }\n"
    "     \n"
    "     .slider::-webkit-slider-thumb {\n"
    "       -webkit-appearance: none;\n"
    "       appearance: none;\n"
    "       width: 25px;\n"
    "       height: 25px;\n"
    "       border-radius: 50%;\n"
    "       background: #4CAF50;\n"
    "       cursor: pointer;\n"
    "     }\n"
    "     \n"
    "     .slider::-moz-range-thumb {\n"
    "       width: 25px;\n"
    "       height: 25px;\n"
    "       border-radius: 50%;\n"
    "       background: #4CAF50;\n"
    "       cursor: pointer;\n"
    "     }\n"
    "     .container {\n"
    "       border-radius: 5px;\n"
    "       background-color: rgba(0, 0, 0, 0.1);\n"
    "       padding: 20px;\n"
    "     }\n"
    "     \n"
    "     .col-25 {\n"
    "       float: left;\n"
    "       width: 25%;\n"
    "       margin-top: 6px;\n"
    "     }\n"
    "     \n"
    "     .col-75 {\n"
    "       float: left;\n"
    "       width: 75%;\n"
    "       margin-top: 6px;\n"
    "     }\n"
    "     \n"
    "     .row:after {\n"
    "       content: \"\";\n"
    "       display: table;\n"
    "       clear: both;\n"
    "     }\n"
    "     \n"
    "     .heading {\n"
    "     //  padding-top: 14px;\n"
    "     //  padding-bottom: 75px;\n"
    "       position: relative;\n"
    "     }\n"
    "     \n"
    "     .widthel {\n"
    "       width: 750px;\n"
    "     }\n"
    "     \n"
    "     @media screen and (max-width: 700px) {\n"
    "       .col-25, .col-75, input[type=submit] {\n"
    "         width: 100%;\n"
    "         margin-top: 0;\n"
    "       }\n"
    "       .copyright {\n"
    "         text-align: center;\n"
    "       }\n"
    "       .container, .heading, .widthel {\n"
    "         width: 100%;\n"
    "       }\n"
    "     }\n"
    "     \n"
    "     .iconAnchor {\n"
    "       text-decoration: none;\n"
    "     }\n"
    "     \n"
    "     .iconImg {\n"
    "       margin-top: 10px;\n"
    "       margin-right: 15px;\n"
    "       border-radius: 50%;\n"
    "       padding-top: 4px;\n"
    "       border: 1px solid black;\n"
    "       width: 40px;\n"
    "       height: 40px;\n"
    "     }\n"
    "     .silentLink {\n"
    "       color: inherit;\n"
    "       text-decoration: inherit;\n"
    "     }\n"
    "   </style>\n"*/
    "  </head>\n"
    "  <body>\n"
    "   <div class=\"container widthel\">\n"
    "     <form id=\"fm\" method=get>\n"
    "       <a class=\"silentLink\" href=\"javascript:location.reload()\">\n"
    "         <h2 style=\"margin-top: 0\">homepi+</h2>\n"
    "       </a>\n"
    "       <input name=\"ra\" style=\"display: none\" value=\"0\">\n"
    "       <div class=\"cssradio\">\n"
    "         PC<br>\n"
    "         <input onclick=\"this.form.submit()\" type=\"radio\" id=\"pcOnOff\" name=\"pc\" value=\"onoff\"><label for=\"pcOnOff\">On / Off</label>&nbsp;\n"
    "         <input onclick=\"this.form.submit()\" type=\"radio\" id=\"pcForce\" name=\"pc\" value=\"force\"><label for=\"pcForce\">Force Off</label>\n"
    "       </div>\n"
    "       <br>\n"
    "       <div class=\"cssradio\">\n"
    "         ThinkPad<br>\n"
    "         <input onclick=\"this.form.submit()\" type=\"radio\" id=\"tpOnOff\" name=\"tp\" value=\"onoff\"><label for=\"tpOnOff\">On / Off</label>&nbsp;\n"
    "         <input onclick=\"this.form.submit()\" type=\"radio\" id=\"tpForce\" name=\"tp\" value=\"force\"><label for=\"tpForce\">Force Off</label>\n"
    "       </div>\n"
    "       <br>\n"
    "       <div class=\"cssradio\">\n"
    "         <a class=\"silentLink\" href=\"javascript:location.reload()\">Lightbulb</a><br>\n"
    "         <input onclick=\"this.form.submit()\" type=\"radio\" id=\"roomOn\" name=\"roomLight\" value=\"on\"><label for=\"roomOn\">On</label>&nbsp;\n"
    "         <input onclick=\"this.form.submit()\" type=\"radio\" id=\"roomOff\" name=\"roomLight\" value=\"off\"><label for=\"roomOff\">Off</label>\n"
    "       </div>\n"
    "       <br>\n"
    "       <div class=\"cssradio\">\n"
    "         <a class=\"silentLink\" href=\"javascript:location.reload()\">Night bulb</a><br>\n"
    "         <input onclick=\"this.form.submit()\" type=\"radio\" id=\"smOn\" name=\"smLight\" value=\"on\"><label for=\"smOn\">On</label>&nbsp;\n"
    "         <input onclick=\"this.form.submit()\" type=\"radio\" id=\"smOff\" name=\"smLight\" value=\"off\"><label for=\"smOff\">Off</label>\n"
    "       </div>\n"
    "       <br>\n"
    "       <div class=\"cssradio\">\n"
    "         <a class=\"silentLink\" href=\"javascript:location.reload()\">Monitor</a><br>\n"
    //"         <input onclick=\"if (document.getElementById('hdmi').checked) { document.getElementById('hdmi').checked = true; document.getElementById('hdmi').value = 'on'; document.forms[0].submit(); } else { document.getElementById('hdmi').checked = true; document.getElementById('hdmi').value = 'off'; document.forms[0].submit(); }\" type=\"checkbox\" id=\"hdmi\" name=\"hdmi\" value=\"0\"><label for=\"hdmi\">On / Off</label>\n"
    "         <input onclick=\"this.form.submit()\" type=\"radio\" id=\"nok\" name=\"nok\" value=\"1\"><label for=\"nok\">Audio</label>&nbsp;\n"
    "         <input onclick=\"this.form.submit()\" type=\"radio\" id=\"HDMITP\" name=\"src\" value=\"tp\"><label for=\"HDMITP\">ThinkPad</label>\n"
    "         <input onclick=\"this.form.submit()\" type=\"radio\" id=\"HDMIPC\" name=\"src\" value=\"pc\"><label for=\"HDMIPC\">PC</label>&nbsp;\n"
    "         <input onclick=\"this.form.submit()\" type=\"radio\" id=\"HDMIRP\" name=\"src\" value=\"ch\"><label for=\"HDMIRP\">Chromecast</label>\n"
    //"         <input onclick=\"this.form.submit()\" type=\"radio\" id=\"HDMIPS\" name=\"src\" value=\"ps\"><label for=\"HDMIPS\">PlayStation 3</label>\n"
    "       </div>\n"
    "       <br>\n"
    "       <div class=\"sliderContainer\">\n"
    "         Brightness:\n"
    "         <output id=\"brightnessValue\">0</output>\n"
    "         <br><br>\n"
    "         <input name=\"br\" type=\"range\" min=\"0\" max=\"100\" value=0 class=\"slider\" id=\"brightnessSlider\" oninput=\"brightnessValue.value = brightnessSlider.value\" onchange=\"ra.value = 2; this.form.submit()\">\n"
    "       </div>\n"
    "       <br>\n"
    "       <div class=\"sliderContainer\">\n"
    "         Volume:\n"
    "         <output id=\"volumeValue\">0</output>\n"
    "         <br><br>\n"
    "         <input name=\"vol\" type=\"range\" min=\"0\" max=\"100\" value=0 class=\"slider\" id=\"volumeSlider\" oninput=\"volumeValue.value = volumeSlider.value\" onchange=\"ra.value = 1; this.form.submit()\">\n"
    "       </div>\n"
    "       <br>\n"
    "       <div class=\"cssradio\">\n"
    "         Control<br>\n"
    "         <input onclick=\"if (document.getElementById('r1').checked) { document.getElementById('r1').checked = true; document.getElementById('r1').value = 'on'; document.forms[0].submit(); } else { document.getElementById('r1').checked = true; document.getElementById('r1').value = 'off'; document.forms[0].submit(); }\" type=\"checkbox\" id=\"r1\" name=\"r1\" value=\"0\"><label for=\"r1\">Desk lamp</label>&nbsp;\n"
    "         <input onclick=\"if (document.getElementById('r2').checked) { document.getElementById('r2').checked = true; document.getElementById('r2').value = 'on'; document.forms[0].submit(); } else { document.getElementById('r2').checked = true; document.getElementById('r2').value = 'off'; document.forms[0].submit(); }\" type=\"checkbox\" id=\"r2\" name=\"r2\" value=\"0\"><label for=\"r2\">Relay</label>\n"
    "       </div>\n"
    //"       <br>\n"
    //"       <div class=\"cssradio\">\n"
    //"         Audio Output<br>\n"
    //"         <input onclick=\"this.form.submit()\" type=\"radio\" id=\"a1\" name=\"ainput\" value=\"a1\"><label for=\"a1\">Monitor</label>\n"
    //"         <input onclick=\"this.form.submit()\" type=\"radio\" id=\"a2\" name=\"ainput\" value=\"a2\"><label for=\"a2\">Bypass</label>\n"
    //"       </div>\n"
    "     </form>\n"
    "   </div>\n"
    "  </body>\n"
    "</html>\n"
;

const char top[] PROGMEM =
    "HTTP/1.0 200 OK\r\n"
    "Content-Type: text/html\r\n"
    "Pragma: no-cache\r\n"
    "\r\n"
    "<html>\n"
    "  <head>\n"
    "   <script>\n"
    "     window.onload = function() {\n"
    ;

const char topA[] PROGMEM =
    "       document.getElementById('fm').action = \"$S\";\n"
    "       document.getElementById('brightnessValue').innerHTML = $D;\n"
    "       document.getElementById('brightnessSlider').value = $D;\n"
    ;

const char topB[] PROGMEM =
    //"       document.getElementById('stats').href = \"javascript:alert('$L + $D')\";\n"
    "       document.getElementById('volumeValue').innerHTML = $D;\n"
    "       document.getElementById('volumeSlider').value = $D;\n"
    ;

const char topC[] PROGMEM =
    //"       document.getElementById('hdmi').checked = $D;\n"
    "       document.getElementById('r1').checked = $D;\n"
    "       document.getElementById('r2').checked = $D;\n"
    ;

/*
const char topD[] PROGMEM =
    "       document.getElementById('a1').checked = $D;\n"
    "       document.getElementById('a2').checked = $D;\n"
    ;
*/
    
const char topZ[] PROGMEM =
    "     }\n"
    "   </script>\n"
    ;

void ether_get(bool external, bool authed = false)
{
  char _brightness = ddc_get(DDC_BRIGHTNESS);
  if (_brightness == -1) _brightness = 0;
  char _volume = ddc_get(DDC_VOLUME);
  if (_volume == -1) _volume = 0;
  ether.httpServerReplyAck();

  if (external && !authed)
  {
    buffer = ether.tcpOffset(); 
    buffer.emit_p(PSTR(
      //"HTTP/1.0 401 Unauthorized\r\n"
      //"WWW-Authenticate: Basic realm='none'\r\n"
      //"\r\n"
      //"<h1>Access is denied.</h1>"
      "HTTP/1.0 200 OK\r\n"
      "Content-Type: text/html\r\n"
      "Pragma: no-cache\r\n"
      "\r\n"
      "<html><head><script>window.onload=function(){"
      "var code=prompt(\"\",\"\");"
      "if(code!=null)location.replace(\"http://$S/\"+code);}"
      "</script></head><body></body></html>"), hostname
    );
    ether.httpServerReply_with_flags(
      buffer.position(), 
      TCP_FLAGS_ACK_V | TCP_FLAGS_FIN_V
    );
  }
  else
  {
    memcpy_P(
      ether.tcpOffset(), 
      top, 
      sizeof top
    );
    ether.httpServerReply_with_flags(
      sizeof top - 1,
      TCP_FLAGS_ACK_V
    );
    
    buffer = ether.tcpOffset();
    buffer.emit_p(
      topA,
      hostaddr,
      _brightness,
      _brightness
    );
    ether.httpServerReply_with_flags(
      buffer.position(), 
      TCP_FLAGS_ACK_V
    );
    buffer = ether.tcpOffset();
    buffer.emit_p(
      topB,
      //epoch,
      //millis() / 1000,
      _volume,
      _volume
    );
    ether.httpServerReply_with_flags(
      buffer.position(), 
      TCP_FLAGS_ACK_V
    );
    buffer = ether.tcpOffset();
    buffer.emit_p(
      topC,
      //pin_hdmi,
      pin_desklamp,
      pin_relay
    );
    ether.httpServerReply_with_flags(
      buffer.position(), 
      TCP_FLAGS_ACK_V
    );
    /*buffer = ether.tcpOffset();
    buffer.emit_p(
      topD,
      pin_ainput == 0,
      pin_ainput == 1
    );
    ether.httpServerReply_with_flags(
      buffer.position(), 
      TCP_FLAGS_ACK_V
    );*/
    
    memcpy_P(
      ether.tcpOffset(), 
      topZ, 
      sizeof topZ
    );
    ether.httpServerReply_with_flags(
      sizeof topZ - 1,
      TCP_FLAGS_ACK_V
    );
  
    for (size_t i = 0; i < sizeof page; i = i + ETHER_MINCHUNKLEN)
    {
      byte is_last = i + ETHER_MINCHUNKLEN > sizeof page;
      /*
      print_time();
      Serial.print("EtherCard: Sending chunk: ");
      if (!is_last)
      {
        Serial.print(i);
        Serial.print(" - ");
        Serial.println(i + ETHER_MINCHUNKLEN);
      }
      else
      {
        Serial.print(i);
        Serial.print(" - ");
        Serial.println(i + ETHER_MINCHUNKLEN - (i + ETHER_MINCHUNKLEN - sizeof page));
      }
      */
      memcpy_P(
        ether.tcpOffset(), 
        page + i, 
        is_last ?
          ETHER_MINCHUNKLEN - (i + ETHER_MINCHUNKLEN - sizeof page) :
          ETHER_MINCHUNKLEN
      );
      ether.httpServerReply_with_flags(
        is_last ?
          ETHER_MINCHUNKLEN - (i + ETHER_MINCHUNKLEN - sizeof page) :
          ETHER_MINCHUNKLEN
        ,
        TCP_FLAGS_ACK_V | (is_last ?
          TCP_FLAGS_FIN_V :
          0
        )
      );
    }
  }
}

const char redirect[] PROGMEM =
    "HTTP/1.0 302 Found\r\n"
    "Location: http://$S/$S\r\n"
    "\r\n"
    ;
  
void ether_post(char* packet)
{
  ////print_time();
  ////Serial.print("EtherCard: POST data: \"");
  ////Serial.print(packet);
  ////Serial.println("\"");

  int realAction = 0;
  byte volume = 0;
  byte brightness = 0;
  char source[POST_VAL_SIZE] = { 0 };
  char pc[POST_VAL_SIZE] = { 0 };
  char tp[POST_VAL_SIZE] = { 0 };
  byte audio = 0;
  char r1[POST_VAL_SIZE] = { 0 };
  char r2[POST_VAL_SIZE] = { 0 };
  char hdmi[POST_VAL_SIZE] = { 0 };
  char roomLight[POST_VAL_SIZE] = { 0 };
  char smLight[POST_VAL_SIZE] = { 0 };
  char ainput[POST_VAL_SIZE] = { 0 };
  
  while (1)
  {
    char* curr_end = strchr(packet, '&');
    char* curr_eq = strchr(packet, '=');
    if (curr_eq == 0)
    {
      break;
    }
    if (curr_end) curr_end[0] = 0;
    curr_eq[0] = 0;
    char param[20];
    strcpy(param, packet);
    if (!strcmp(param, "ra"))
    {
      realAction = atoi(curr_eq + 1);
    }
    else if (!strcmp(param, "vol"))
    {
      volume = atoi(curr_eq + 1);
    }
    else if (!strcmp(param, "br"))
    {
      brightness = atoi(curr_eq + 1);
    }
    else if (!strcmp(param, "src"))
    {
      strcpy(source, curr_eq + 1);
    }
    else if (!strcmp(param, "pc"))
    {
      strcpy(pc, curr_eq + 1);
    }
    else if (!strcmp(param, "tp"))
    {
      strcpy(tp, curr_eq + 1);
    }
    else if (!strcmp(param, "nok"))
    {
      audio = atoi(curr_eq + 1);
    }
    else if (!strcmp(param, "r1"))
    {
      strcpy(r1, curr_eq + 1);
    }
    else if (!strcmp(param, "r2"))
    {
      strcpy(r2, curr_eq + 1);
    }
    else if (!strcmp(param, "hdmi"))
    {
      strcpy(hdmi, curr_eq + 1);
    }
    else if (!strcmp(param, "roomLight"))
    {
      strcpy(roomLight, curr_eq + 1);
    }
    else if (!strcmp(param, "smLight"))
    {
      strcpy(smLight, curr_eq + 1);
    }
    else if (!strcmp(param, "ainput"))
    {
      strcpy(ainput, curr_eq + 1);
    }
    packet = curr_end + 1;
  }

  if (realAction == 1)
  {
    ddc_set(DDC_VOLUME, volume);
  }
  else if (realAction == 2)
  {
    ddc_set(DDC_BRIGHTNESS, brightness);
  }
  else if (source[0] != 0)
  {
    if (!strcmp(source, "pc"))
    {
      ddc_set(DDC_INPUT, DDC_INPUT_DP);
      delay(100);
    }
    else if (!strcmp(source, "ch"))
    {
      /*port1 &= ~(1 << CTL_PIN_DELOCK1);
      shift_ports();
      irsend.send(DELOCK_CH4, 32);
      port1 |= (1 << CTL_PIN_DELOCK1);
      shift_ports();*/
      ddc_set(DDC_INPUT, DDC_INPUT_HDMI);
      delay(100);
    }
    else if (!strcmp(source, "tp"))
    {
      /*port1 &= ~(1 << CTL_PIN_DELOCK1);
      shift_ports();
      irsend.send(DELOCK_CH5, 32);
      port1 |= (1 << CTL_PIN_DELOCK1);
      shift_ports();
      ddc_set(DDC_INPUT, DDC_INPUT_HDMI);
      delay(100);*/
      ddc_set(DDC_INPUT, DDC_INPUT_DVI);
      delay(100);
    }
    else if (!strcmp(source, "ps"))
    {
      ddc_set(DDC_INPUT, DDC_INPUT_DVI);
      delay(100);
    }
  }
  else if (pc[0] != 0)
  {
    if (!strcmp(pc, "onoff"))
    {
      port0 |= (1 << CTL_PIN_PC0);
      shift_ports();
      delay(1000);
      port0 &= ~(1 << CTL_PIN_PC0);
      shift_ports();
      
      /*digitalWrite(CTL_PIN_PC, HIGH);
      delay(1000);
      digitalWrite(CTL_PIN_PC, LOW);*/
    }
    else if (!strcmp(pc, "force"))
    {
      port0 |= (1 << CTL_PIN_PC0);
      shift_ports();
      delay(7000);
      port0 &= ~(1 << CTL_PIN_PC0);
      shift_ports();
      
      /*digitalWrite(CTL_PIN_PC, HIGH);
      delay(7000);
      digitalWrite(CTL_PIN_PC, LOW);*/
    }
  }
  else if (tp[0] != 0)
  {
    if (!strcmp(tp, "onoff"))
    {
      port0 |= (1 << CTL_PIN_TP0);
      shift_ports();
      delay(1000);
      port0 &= ~(1 << CTL_PIN_TP0);
      shift_ports();
      
      /*digitalWrite(CTL_PIN_TP, HIGH);
      delay(1000);
      digitalWrite(CTL_PIN_TP, LOW);*/
    }
    else if (!strcmp(tp, "force"))
    {
      port0 |= (1 << CTL_PIN_TP0);
      shift_ports();
      delay(7000);
      port0 &= ~(1 << CTL_PIN_TP0);
      shift_ports();
      
      /*digitalWrite(CTL_PIN_TP, HIGH);
      delay(7000);
      digitalWrite(CTL_PIN_TP, LOW);*/
    }
  }
  else if (audio)
  {
    port0 |= (1 << CTL_PIN_AUDIO0);
    shift_ports();
    delay(2000);
    port0 &= ~(1 << CTL_PIN_AUDIO0);
    shift_ports();
      
    /*digitalWrite(CTL_PIN_AUDIO, HIGH);
    delay(2000);
    digitalWrite(CTL_PIN_AUDIO, LOW);*/
  }
  else if (r1[0] != 0 && r1[0] != '0')
  {
    if (!strcmp(r1, "on"))
    {
      port0 |= (1 << CTL_PIN_DESKLAMP0);
      shift_ports();
      
      /*digitalWrite(CTL_PIN_DESKLAMP, HIGH);*/
      pin_desklamp = 1;
    }
    else
    {
      port0 &= ~(1 << CTL_PIN_DESKLAMP0);
      shift_ports();
      
      /*digitalWrite(CTL_PIN_DESKLAMP, LOW);*/
      pin_desklamp = 0;
    }
    EEPROM.write(EEPROM_DESKLAMP, pin_desklamp);
  }
  else if (r2[0] != 0 && r2[0] != '0')
  {
    if (!strcmp(r2, "on"))
    {
      port0 |= (1 << CTL_PIN_RELAY0);
      shift_ports();
      
      /*digitalWrite(CTL_PIN_RELAY, HIGH);*/
      pin_relay = 1;
    }
    else
    {
      port0 &= ~(1 << CTL_PIN_RELAY0);
      shift_ports();
      
      /*digitalWrite(CTL_PIN_RELAY, LOW);*/
      pin_relay = 0;
    }
  }
  else if (hdmi[0] != 0 && hdmi[0] != '0')
  {
    if (!strcmp(hdmi, "on"))
    {
      //port0 |= (1 << CTL_PIN_HDMI0);
      port1 &= ~(1 << CTL_PIN_DELOCK1);
      shift_ports();
      irsend.send(DELOCK_POWER, 32);
      port1 |= (1 << CTL_PIN_DELOCK1);
      shift_ports();
      
      /*digitalWrite(CTL_PIN_HDMI, HIGH);*/
      //pin_hdmi = 1;
    }
    else
    {
      //port0 &= ~(1 << CTL_PIN_HDMI0);
      port1 &= ~(1 << CTL_PIN_DELOCK1);
      shift_ports();
      irsend.send(DELOCK_POWER, 32);
      port1 |= (1 << CTL_PIN_DELOCK1);
      shift_ports();
      
      /*digitalWrite(CTL_PIN_HDMI, LOW);*/
      pin_hdmi = 0;
    }
  }
  else if (roomLight[0] != 0)
  {
    if (!strcmp(roomLight, "on"))
    {
      port0 &= ~(1 << CTL_PIN_IR_LIGHTBULB0);
      shift_ports();
      //digitalWrite(CTL_PIN_IR_LIGHTBULB, LOW);
      ////Serial.println("before");
      irsend.send(OSRAM_LIGHT_ON, 32);
      ////Serial.println("after");
      port0 |= (1 << CTL_PIN_IR_LIGHTBULB0);
      shift_ports();
      //digitalWrite(CTL_PIN_IR_LIGHTBULB, HIGH);
    }
    else
    {
      port0 &= ~(1 << CTL_PIN_IR_LIGHTBULB0);
      shift_ports();
      //digitalWrite(CTL_PIN_IR_LIGHTBULB, LOW);
      irsend.send(OSRAM_LIGHT_OFF, 32);
      port0 |= (1 << CTL_PIN_IR_LIGHTBULB0);
      shift_ports();
      //digitalWrite(CTL_PIN_IR_LIGHTBULB, HIGH);
    }
  }
  else if (smLight[0] != 0)
  {
    if (!strcmp(smLight, "on"))
    {
      port1 &= ~(1 << CTL_PIN_IR_NIGHTBULB1);
      shift_ports();
      //digitalWrite(CTL_PIN_IR_NIGHTBULB, LOW);
      irsend.send(OSRAM_LIGHT_ON, 32);
      port1 |= (1 << CTL_PIN_IR_NIGHTBULB1);
      shift_ports();
      //digitalWrite(CTL_PIN_IR_NIGHTBULB, HIGH);
    }
    else
    {
      port1 &= ~(1 << CTL_PIN_IR_NIGHTBULB1);
      shift_ports();
      //digitalWrite(CTL_PIN_IR_NIGHTBULB, LOW);
      irsend.send(OSRAM_LIGHT_OFF, 32);
      port1 |= (1 << CTL_PIN_IR_NIGHTBULB1);
      shift_ports();
      //digitalWrite(CTL_PIN_IR_NIGHTBULB, HIGH);
    }
  }
  if (ainput[0] != 0)
  {
    if (!strcmp(ainput, "a1"))
    {
      pin_ainput = 0;
      port1 &= ~(1 << CTL_PIN_AUDIO_INPUT1);
      shift_ports();
      //digitalWrite(CTL_PIN_AUDIO_INPUT, LOW);
    }
    else if (!strcmp(ainput, "a2"))
    {
      pin_ainput = 1;
      port1 |= (1 << CTL_PIN_AUDIO_INPUT1);
      shift_ports();
      //digitalWrite(CTL_PIN_AUDIO_INPUT, HIGH);
    } 
  }
}

void loop() 
{
  wdt_reset();

/*
  if (!((millis() / 1000) % 20))
  {
    while(1){};
  }
*/

  if (!((millis() / 1000) % RESPRING_DELAY) || !ether.isLinkUp())
  {
    Serial.println(millis() / 1000);
    digitalWrite(reset_pin, LOW);
    delay(10);
    reset();
  }
  
  word len = ether.packetReceive();
  word pos = ether.packetLoop(len);
  if (pos) 
  {
    // this is necessary when millis will eventually overflow
    // unfortunately, it does not work at the moment, apparently,
    // as it hangs when querying the DNS... ideally, one would
    // reset the board, one time in a month is ok I think
    /*if (prev_millis > millis())
    {
      if (ether.dnsLookup(NTP_REMOTEHOST))
      {
        uint8_t ntpIp[IP_LEN];
        ether.copyIp(ntpIp, ether.hisip);
        ether.printIp("NTP: ", ntpIp);
        ether.udpServerResumeListenOnPort(NTP_LOCALPORT);
        sendNTPpacket(ntpIp);
        prev_millis = millis();
      }
    }*/
    const char* packet = (const char*)ether.tcpOffset();
    char* hostloc = strstr(packet, "Host: ");
    if (hostloc)
    {
      hostloc += 6;
      char* hostlocfin = strchr(hostloc, '\r');
      if (hostlocfin)
      {
        hostlocfin[0] = 0;
        memset(hostname, 0, HOSTNAME_LEN);
        memcpy(hostname, hostloc, HOSTNAME_LEN - 1);
        hostlocfin[0] = '\r';
      }
    }
    char* addr = strstr(packet, " /");
    if (addr)
    {
      addr += 2;
      char* fin = strchr(addr, ' ');
      if (fin)
      {
        fin[0] = 0;
        memset(hostaddr, 0, HOSTADDR_LEN);
        memcpy(hostaddr, addr, HOSTADDR_LEN - 1);
        fin[0] = ' ';
      }
    }
    bool is_external = false;
    bool authed = false;
    uint8_t* ip_src = ether.buffer + 0x1A;
    if (ether.compareAddresses(ip_src, ether.gwip))
    {
      is_external = true;
      char* addr = strstr(packet, " /");
      if (addr)
      {
        addr += 2;
        char* fin = strchr(addr, ' ');
        if (fin)
        {
          fin[0] = 0;
          memset(hostaddr, 0, HOSTADDR_LEN);
          memcpy(hostaddr, addr, HOSTADDR_LEN - 1);
          fin[0] = ' ';
          
          unsigned long t = epoch + (millis() / 1000) - millis_offset / 1000;
          char* code = totp.getCode(t);
          /*
          Serial.print(t);
          Serial.print(" - ");
          Serial.print(attempts);
          Serial.print(" - ");
          Serial.print(hostaddr);
          Serial.print(" - ");
          Serial.println(code);
          */
          char o = hostaddr[NUM_DIGITS];
          hostaddr[NUM_DIGITS] = 0;
          if (!strcmp(hostaddr, code))
          {
            unsigned int ccode = atoi(code);
            if (ccode != last_code)
            {
              authed = true;
              attempts = 0;
              last_code = ccode;
            }
            else if (attempts < MAX_ATTEMPTS)
            {
              authed = true;
              attempts++;
            }
            else
            {
              attempts++;
            }
          }
          hostaddr[NUM_DIGITS] = o;
        }
      }
    }
    
    if (!strchr(hostaddr, '?')) 
    {
      ether_get(is_external, authed);
    } 
    else 
    {
      if (!is_external || (is_external && authed))
      {
        ether_post(strchr(hostaddr, '?') + 1);
        
        ether.httpServerReplyAck();
        buffer = ether.tcpOffset();

        hostaddr[NUM_DIGITS] = 0;
        buffer.emit_p(
          redirect,
          hostname,
          is_external ? hostaddr : ""
        );
        
        ether.httpServerReply_with_flags(
          buffer.position(), 
          TCP_FLAGS_ACK_V | TCP_FLAGS_FIN_V
        );
      }
      else
      {
        ether_get(is_external, authed);
      }
    }
  }
  delay(10);
}
