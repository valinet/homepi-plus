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

// i2c
#include "src/ArduinoCore-avr/libraries/Wire/src/Wire.h"
// IR
#include "src/IRLib2/IRLibProtocols/IRLibSendBase.h"
#include "src/IRLib2/IRLibProtocols/IRLib_P01_NEC.h"
// enc28j60
#include "src/EtherCard/src/EtherCard.h"
// MDNS
#include "src/EtherCard-MDNS/EC_MDNSResponder.h"

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
#define POST_SIZE 50

static byte mymac[] = { 0x74, 0x69, 0x69, 0x2D, 0x30, 0x31 };
const static uint8_t ip[] = {192,168,105,24};
const static uint8_t gateway[] = {192,168,105,1};
const static uint8_t dns[] = {8,8,8,8};
const static uint8_t mask[] = {255,255,255,0};

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

char post_data[POST_SIZE];
byte p_i = 0;
byte p_ok = 0;

////void print_time()
////{
  ////char buf[20];
  ////snprintf(buf, 20, "[%15d] ", millis());
  ////Serial.print(buf);
////}

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

void setup() 
{
  //port0 = 0b11111111;
  //port1 = 0b11111111;
  //shift_ports();
  
  Serial.begin(9600);
  while (!Serial);
  Serial.println("\n");
  ////print_time();
  ////Serial.println("Starting homepi version 2.0 (build 2020.10.31) on Arduino Nano.");
  
  digitalWrite(SDA, LOW);
  digitalWrite(SCL, LOW);
  ////print_time();
  ////Serial.println("Disable internal pull up resistors for i2c pins.");

  pinMode(latchPin, OUTPUT);
  port0 = 0;
  port1 = 0;
  port0 &= ~(1 << CTL_PIN_TP0);
  port0 &= ~(1 << CTL_PIN_PC0);
  port0 &= ~(1 << CTL_PIN_AUDIO0);
  port0 &= ~(1 << CTL_PIN_HDMI0);
  pin_hdmi = 0;
  port0 |= (1 << CTL_PIN_DESKLAMP0);
  pin_desklamp = 0;
  port0 |= (1 << CTL_PIN_RELAY0);
  pin_relay = 0;
  port0 |= (1 << CTL_PIN_IR_LIGHTBULB0);
  port1 |= (1 << CTL_PIN_IR_NIGHTBULB1);
  port1 &= ~(1 << CTL_PIN_AUDIO_INPUT1);
  pin_ainput = 0;
  port1 |= (1 << CTL_PIN_DELOCK1);
  shift_ports();
  pinMode(oePin, OUTPUT);
  digitalWrite(oePin, LOW);
  ////print_time();
  ////Serial.println("Configure pins.");

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
  
  ////print_time();
  ////Serial.println("Perform startup tasks.");

  // enable i2c
  Wire.begin();
  Wire.setWireTimeout();
  ////print_time();
  ////Serial.println("Wire: Enable i2c.");

  ////print_time();
  if (ether.begin(sizeof Ethernet::buffer, mymac) == 0)
  {
  ////  Serial.print(STR_FAILED);
  }
  ////Serial.println("EtherCard: Enable Ethernet stack.");

  ////print_time();
  //ether.staticSetup(ip, gateway, dns, mask);
  while (!ether.dhcpSetup())
  {
    //Serial.print(STR_FAILED);
    //ether.staticSetup(ip, gateway, dns, mask);
  }
  ////Serial.println("EtherCard: DHCP configuration.");

  ////print_time();
  ether.hisport = HTTP_PORT;
  ////Serial.print("EtherCard: Service port - ");
  ////Serial.println(HTTP_PORT);
  ////print_time();
  ether.printIp("EtherCard: IP - ", ether.myip);
  ////print_time();
  ////ether.printIp("EtherCard: Netmask - ", ether.netmask);
  ////print_time();
  ////ether.printIp("EtherCard: Gateway - ", ether.gwip);
  ////print_time();
  ////ether.printIp("EtherCard: DNS - ", ether.dnsip);

  if(!mdns.begin("homepi", ether)) {
    //Serial.println("MDNS error");
  } else {
    //Serial.println("Listening on homepi.local");
  }

  ////print_time();
  ENC28J60::enableBroadcast();
  ENC28J60::enableMulticast();
  ether.enableMulticast();
  ////Serial.println("EtherCard: Enable broadcast, multicast.");

  ////print_time();
  ////Serial.println("EtherCard: Listening...");
}

const char page[] PROGMEM =
    "   <meta name=\"HandheldFriendly\" content=\"true\" />\n"
    "   <meta name=\"viewport\" content=\"width=device-width, height=device-height, user-scalable=no\" />\n"
    "   <title>homepi+</title>\n"
    "   <style>\n"
    "     .cssradio input[type=\"radio\"]{display: none;}\n"
    "     .cssradio input[type=\"checkbox\"]{display: none;}\n"
    "     .cssradio label{padding: 10px; margin: 15px 10px 0 0;\n"
    "     background: rgba(255, 255, 255, 0.8); border-radius: 3px;\n"
    "     display: inline-block; color: black;\n"
    "     cursor: pointer; border: 1px solid #000; width: 46%}\n"
    "     .cssradio input:checked + label {\n"
    "     background: green; font-weight:bold; color: white; border-color: green;}\n"
    "     /* Enables dark mode support in Safari */\n"
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
    "       width: 100%; /* Width of the outside container */\n"
    "     }\n"
    "     \n"
    "     /* The slider itself */\n"
    "     .slider {\n"
    "       -webkit-appearance: none;\n"
    "       width: 91%;\n"
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
    "       background-color: rgba(0, 0, 0, 0.1);/*#f2f2f2;*/\n"
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
    "     /* Clear floats after the columns */\n"
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
    "   </style>\n"
    "  </head>\n"
    "  <body>\n"
    "   <div class=\"container widthel\">\n"
    "     <form action=\"/\" method=post>\n"
    "       <h2>\n"
    "         homepi+\n"
    "       </h2>\n"
    "       <input name=\"ra\" style=\"display: none\" value=\"0\">\n"
    "       <div class=\"cssradio\">\n"
    "         PC<br>\n"
    "         <input onclick=\"this.form.submit()\" type=\"radio\" id=\"pcOnOff\" name=\"pc\" value=\"onoff\"><label for=\"pcOnOff\">On / Off</label>\n"
    "         <input onclick=\"this.form.submit()\" type=\"radio\" id=\"pcForce\" name=\"pc\" value=\"force\"><label for=\"pcForce\">Force Off</label>\n"
    "       </div>\n"
    "       <br>\n"
    "       <div class=\"cssradio\">\n"
    "         ThinkPad<br>\n"
    "         <input onclick=\"this.form.submit()\" type=\"radio\" id=\"tpOnOff\" name=\"tp\" value=\"onoff\"><label for=\"tpOnOff\">On / Off</label>\n"
    "         <input onclick=\"this.form.submit()\" type=\"radio\" id=\"tpForce\" name=\"tp\" value=\"force\"><label for=\"tpForce\">Force Off</label>\n"
    "       </div>\n"
    "       <br>\n"
    "       <div class=\"cssradio\">\n"
    "         Lightbulb<br>\n"
    "         <input onclick=\"this.form.submit()\" type=\"radio\" id=\"roomOn\" name=\"roomLight\" value=\"on\"><label for=\"roomOn\">On</label>\n"
    "         <input onclick=\"this.form.submit()\" type=\"radio\" id=\"roomOff\" name=\"roomLight\" value=\"off\"><label for=\"roomOff\">Off</label>\n"
    "       </div>\n"
    "       <br>\n"
    "       <div class=\"cssradio\">\n"
    "         Night bulb<br>\n"
    "         <input onclick=\"this.form.submit()\" type=\"radio\" id=\"smOn\" name=\"smLight\" value=\"on\"><label for=\"smOn\">On</label>\n"
    "         <input onclick=\"this.form.submit()\" type=\"radio\" id=\"smOff\" name=\"smLight\" value=\"off\"><label for=\"smOff\">Off</label>\n"
    "       </div>\n"
    "       <br>\n"
    "       <div class=\"cssradio\">\n"
    "         Monitor<br>\n"
    //"         <input onclick=\"if (document.getElementById('hdmi').checked) { document.getElementById('hdmi').checked = true; document.getElementById('hdmi').value = 'on'; document.forms[0].submit(); } else { document.getElementById('hdmi').checked = true; document.getElementById('hdmi').value = 'off'; document.forms[0].submit(); }\" type=\"checkbox\" id=\"hdmi\" name=\"hdmi\" value=\"0\"><label for=\"hdmi\">On / Off</label>\n"
    "         <input onclick=\"this.form.submit()\" type=\"radio\" id=\"nok\" name=\"nok\" value=\"1\"><label for=\"nok\">Audio</label>\n"
    "         <input onclick=\"this.form.submit()\" type=\"radio\" id=\"HDMITP\" name=\"src\" value=\"tp\"><label for=\"HDMITP\">ThinkPad</label>\n"
    "         <input onclick=\"this.form.submit()\" type=\"radio\" id=\"HDMIPC\" name=\"src\" value=\"pc\"><label for=\"HDMIPC\">PC</label>\n"
    "         <input onclick=\"this.form.submit()\" type=\"radio\" id=\"HDMIRP\" name=\"src\" value=\"ch\"><label for=\"HDMIRP\">Chromecast</label>\n"
    //"         <input onclick=\"this.form.submit()\" type=\"radio\" id=\"HDMIPS\" name=\"src\" value=\"ps\"><label for=\"HDMIPS\">PlayStation 3</label>\n"
    "       </div>\n"
    "       <br>\n"
    "       <div class=\"sliderContainer\">\n"
    "         Volume:\n"
    "         <output id=\"volumeValue\">0</output>\n"
    "         <br><br>\n"
    "         <input name=\"vol\" type=\"range\" min=\"0\" max=\"100\" value=0 class=\"slider\" id=\"volumeSlider\" oninput=\"volumeValue.value = volumeSlider.value\" onchange=\"ra.value = 1; this.form.submit()\">\n"
    "       </div>\n"
    "       <br>\n"
    "       <div class=\"sliderContainer\">\n"
    "         Brightness:\n"
    "         <output id=\"brightnessValue\">0</output>\n"
    "         <br><br>\n"
    "         <input name=\"br\" type=\"range\" min=\"0\" max=\"100\" value=0 class=\"slider\" id=\"brightnessSlider\" oninput=\"brightnessValue.value = brightnessSlider.value\" onchange=\"ra.value = 2; this.form.submit()\">\n"
    "       </div>\n"
    "       <br>\n"
    "       <div class=\"cssradio\">\n"
    "         Control<br>\n"
    "         <input onclick=\"if (document.getElementById('r1').checked) { document.getElementById('r1').checked = true; document.getElementById('r1').value = 'on'; document.forms[0].submit(); } else { document.getElementById('r1').checked = true; document.getElementById('r1').value = 'off'; document.forms[0].submit(); }\" type=\"checkbox\" id=\"r1\" name=\"r1\" value=\"0\"><label for=\"r1\">Desk lamp</label>\n"
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
    "       document.getElementById('brightnessValue').innerHTML = $D;\n"
    "       document.getElementById('brightnessSlider').value = $D;\n"
    ;

const char topB[] PROGMEM = 
    "       document.getElementById('volumeValue').innerHTML = $D;\n"
    "       document.getElementById('volumeSlider').value = $D;\n"
    ;

const char topC[] PROGMEM =
    //"       document.getElementById('hdmi').checked = $D;\n"
    "       document.getElementById('r1').checked = $D;\n"
    "       document.getElementById('r2').checked = $D;\n"
    ;

const char topD[] PROGMEM =
    "       document.getElementById('a1').checked = $D;\n"
    "       document.getElementById('a2').checked = $D;\n"
    ;
     
const char topZ[] PROGMEM =
    "     }\n"
    "   </script>\n"
    ;

void ether_get()
{
  char _brightness = ddc_get(DDC_BRIGHTNESS);
  if (_brightness == -1) _brightness = 0;
  char _volume = ddc_get(DDC_VOLUME);
  if (_volume == -1) _volume = 0;
  ether.httpServerReplyAck();

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

const char redirect[] PROGMEM =
    "HTTP/1.0 302 Found\r\n"
    "Location: http://$D.$D.$D.$D/\r\n"
    "\r\n"
    ;

const char post_ok[] PROGMEM =
    "HTTP/1.0 200 OK\r\n"
    "\r\n"
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
      port0 &= ~(1 << CTL_PIN_DESKLAMP0);
      shift_ports();
      
      /*digitalWrite(CTL_PIN_DESKLAMP, HIGH);*/
      pin_desklamp = 1;
    }
    else
    {
      port0 |= (1 << CTL_PIN_DESKLAMP0);
      shift_ports();
      
      /*digitalWrite(CTL_PIN_DESKLAMP, LOW);*/
      pin_desklamp = 0;
    }
  }
  else if (r2[0] != 0 && r2[0] != '0')
  {
    if (!strcmp(r2, "on"))
    {
      port0 &= ~(1 << CTL_PIN_RELAY0);
      shift_ports();
      
      /*digitalWrite(CTL_PIN_RELAY, HIGH);*/
      pin_relay = 1;
    }
    else
    {
      port0 |= (1 << CTL_PIN_RELAY0);
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
  word len = ether.packetReceive();
  word pos = ether.packetLoop(len);
 
  if (pos) 
  {
    const char* packet = (const char*)ether.tcpOffset();
    if (packet[0] == 'G' && packet[1] == 'E' && packet[2] == 'T') {
      ////print_time();
      ////Serial.println("EtherCard: Servicing GET.");
      ether_get();
      p_ok = 0;
      p_i = 0;
    } else {
      ether.setBufferPtr(pos);
      byte b, b1 = 0, b2 = 0, b3 = 0;
      while(ether.packetPayloadSize>0)
      {
        b3 = b2;
        b2 = b1;
        b1 = b;
        b=ether.readByte();
        if (p_ok)
        {
          post_data[p_i] = b;
          p_i++;
        }
        if (b == '\n' && b1 == '\r' && b2 == '\n' && b1 == '\r')
        {
          p_ok = 1;
        }
      }
      post_data[p_i] = 0;
      if (p_i != 0)
      {
        ether_post(post_data);
  
        delay(100);
  
        buffer = ether.tcpOffset();
        buffer.emit_p(
          redirect,
          ether.myip[0],
          ether.myip[1],
          ether.myip[2],
          ether.myip[3]
        );
        ether.httpServerReply_with_flags(
          buffer.position(), 
          TCP_FLAGS_ACK_V | TCP_FLAGS_FIN_V
        );

        p_ok = 0;
        p_i = 0;
      }
    }
  }
}
