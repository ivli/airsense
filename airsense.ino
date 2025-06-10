/*
*  simple air quality sensor using 
*  ENS160+AHT21 carbon dioxide CO2 eco2 TVOC air quality and temperature and humidity sensor,
*  WeMos D1 R2 WiFi uno based ESP8266, and optionally
*  128X64 I2C SSD1306 12864 LCD Screen Board
*  both ENS160+AHT21 and SSD1306 have connected via I2C    
*/
#include <Arduino.h>
#include <Wire.h>

#include <SparkFun_Qwiic_Humidity_AHT20.h>
#include <SparkFun_ENS160.h> 

#include <WiFiClient.h>
#include <ESP8266WebServer.h>
#include <ESP8266mDNS.h>
#include <StreamString.h>

#define ENABLE_DISPLAY

#ifdef ENABLE_DISPLAY
#include <U8x8lib.h>
#endif

#include <FS.h>

#include <ESP8266WiFi.h>
#include <WiFiUdp.h>

#ifndef STASSID
#define STASSID "Keenetic-6012"
#define STAPSK  "VSUUvJZn"
#endif

const char* ssid = STASSID;  
const char* pass = STAPSK;   


unsigned int localPort = 2390;  // local port to listen for UDP packets
IPAddress timeServerIP;  // time.nist.gov NTP server address
const char* ntpServerName = "time.nist.gov";
const int NTP_PACKET_SIZE = 48;
byte packetBuffer[NTP_PACKET_SIZE];  // buffer to hold incoming and outgoing packets
WiFiUDP udp;


bool setup_wifi()
{
  Serial.print("Connecting to ");
  Serial.println(ssid);
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, pass);

  while (WiFi.status() != WL_CONNECTED) {
    static unsigned n = 0;
    delay(500);
    Serial.print(".");

    if (++n > 10)
      return false;
  }

  Serial.println("");

  Serial.println("WiFi connected");
  Serial.println("IP address: ");
  Serial.println(WiFi.localIP());

  Serial.println("Starting UDP");
  udp.begin(localPort);
  Serial.print("Local port: ");
  Serial.println(udp.localPort());
  return true;
}


// send an NTP request to the time server at the given address
void sendNTPpacket(IPAddress& address) {
  Serial.println("sending NTP packet...");
  // set all bytes in the buffer to 0
  memset(packetBuffer, 0, NTP_PACKET_SIZE);
  // Initialize values needed to form NTP request
  // (see URL above for details on the packets)
  packetBuffer[0] = 0b11100011;  // LI, Version, Mode
  packetBuffer[1] = 0;           // Stratum, or type of clock
  packetBuffer[2] = 6;           // Polling Interval
  packetBuffer[3] = 0xEC;        // Peer Clock Precision
  // 8 bytes of zero for Root Delay & Root Dispersion
  packetBuffer[12] = 49;
  packetBuffer[13] = 0x4E;
  packetBuffer[14] = 49;
  packetBuffer[15] = 52;

  // all NTP fields have been given values, now
  // you can send a packet requesting a timestamp:
  udp.beginPacket(address, 123);  // NTP requests are to port 123
  udp.write(packetBuffer, NTP_PACKET_SIZE);
  udp.endPacket();
}

void getNTPTime()
{
// get a random server from the pool
  WiFi.hostByName(ntpServerName, timeServerIP);

  sendNTPpacket(timeServerIP);  // send an NTP packet to a time server
  // wait to see if a reply is available
  delay(1000);

  int cb = udp.parsePacket();
  if (!cb) {
    Serial.println("no packet yet");
  } else {
    Serial.print("packet received, length=");
    Serial.println(cb);
    // We've received a packet, read the data from it
    udp.read(packetBuffer, NTP_PACKET_SIZE);  // read the packet into the buffer

    // the timestamp starts at byte 40 of the received packet and is four bytes,
    //  or two words, long. First, esxtract the two words:

    unsigned long highWord = word(packetBuffer[40], packetBuffer[41]);
    unsigned long lowWord = word(packetBuffer[42], packetBuffer[43]);
    // combine the four bytes (two words) into a long integer
    // this is NTP time (seconds since Jan 1 1900):
    unsigned long secsSince1900 = highWord << 16 | lowWord;

    Serial.print("Seconds since Jan 1 1900 = ");
    Serial.println(secsSince1900);

    // now convert NTP time into everyday time:
    Serial.print("Unix time = ");
    // Unix time starts on Jan 1 1970. In seconds, that's 2208988800:
    const unsigned long seventyYears = 2208988800UL;
    // subtract seventy years:
    unsigned long epoch = secsSince1900 - seventyYears;
    // print Unix time:
    Serial.println(epoch);


    // print the hour, minute and second:
    Serial.print("The UTC time is ");       // UTC is the time at Greenwich Meridian (GMT)
    Serial.print((epoch % 86400L) / 3600);  // print the hour (86400 equals secs per day)
    Serial.print(':');
    if (((epoch % 3600) / 60) < 10) {
      // In the first 10 minutes of each hour, we'll want a leading '0'
      Serial.print('0');
    }
    Serial.print((epoch % 3600) / 60);  // print the minute (3600 equals secs per minute)
    Serial.print(':');
    if ((epoch % 60) < 10) {
      // In the first 10 seconds of each minute, we'll want a leading '0'
      Serial.print('0');
    }
    Serial.println(epoch % 60);  // print the second
  }
}


AHT20 humiditySensor;
SparkFun_ENS160 airSensor;
#ifdef ENABLE_DISPLAY
U8X8_SSD1306_128X64_NONAME_SW_I2C u8x8(SCL, SDA, U8X8_PIN_NONE);
#endif

const char* const GASSTATSTR = "Gas Sensor Status Flag (0 - Standard, 1 - Warm up, 2 - Initial Start Up): ";
static bool bWiFi = false;

ESP8266WebServer server(80);

struct SData
{
  bool valid;
  uint8_t aoi;
  uint16_t tvoc;
  uint16_t eco2;
  float temperature;
  float humidity;
} sd;

void handleRoot() {
  int sec = millis() / 1000;
  int min = sec / 60;
  int hr = min / 60;

  StreamString temp;
  temp.reserve(500);  // Preallocate a large chunk to avoid memory fragmentation
  temp.printf("\
<html>\
  <head>\
    <meta http-equiv='refresh' content='5'/>\
    <title>Air quality</title>\
    <style>\
      body { background-color: #cccccc; font-family: Arial, Helvetica, Sans-Serif; Color: #000088; }\
    </style>\
  </head>\
  <body>\
    <h1>Temperature, Humidity and air quality</h1>\
    <p>Uptime: %02d:%02d:%02d</p>\
    <p>Temperature: %.2f C </p>\
    <p>Humidity: %.2f % </p>\
    <p>tvoc: %d ppi (Total Volatile Organic Compounds)</p>\
    <p>eco2: %d ppm (Equivalent CO2: <800 - Good, >1500 - Bad)</p>\
    <p>AQI:  %1d (UBA Air Quality Index: 1- Excellent, 5 - Unhealthy)</p>\
  </body>\
</html>",
              hr, min % 60, sec % 60,  sd.temperature, sd.humidity, sd.tvoc , sd.eco2, sd.aoi);

  server.send(200, "text/html", temp.c_str());
}

void handleNotFound() {

  String message = "File Not Found\n\n";
  message += "URI: ";
  message += server.uri();
  message += "\nMethod: ";
  message += (server.method() == HTTP_GET) ? "GET" : "POST";
  message += "\nArguments: ";
  message += server.args();
  message += "\n";

  for (uint8_t i = 0; i < server.args(); i++) { message += " " + server.argName(i) + ": " + server.arg(i) + "\n"; }

  server.send(404, "text/plain", message);

}

void setup(void)
{
  Serial.begin(115200);
  Wire.begin();
  
  if (!(bWiFi = setup_wifi())){
    Serial.println("Failed to initialise WiFi");
  } else {
    Serial.print("Connected to ");
    Serial.println(ssid);
    Serial.print("IP address: ");
    Serial.println(WiFi.localIP());

    if (MDNS.begin("airsense")) 
      Serial.println("MDNS responder started"); 

    server.on("/", handleRoot);

    server.on("/inline", []() {
    server.send(200, "text/plain", "this works as well");
    });

  server.onNotFound(handleNotFound);
  server.begin();
  Serial.println("HTTP server started");
  }
  

#ifdef ENABLE_DISPLAY
  u8x8.begin();
  u8x8.setFont(u8x8_font_amstrad_cpc_extended_f);
  u8x8.clear();

  u8x8.inverse();
  u8x8.setFont(u8x8_font_chroma48medium8_r);  
  u8x8.noInverse();
#endif

  if (!humiditySensor.begin())
    Serial.println("AHT20 not detected. Please check wiring. Freezing.");
  else
    Serial.println("AHT20 acknowledged.");

  if(!airSensor.begin())
    Serial.println("Could not communicate with the ENS160, check wiring.");

  if(airSensor.setOperatingMode(SFE_ENS160_RESET))
    Serial.println("Ready.");

  delay(100);

  airSensor.setOperatingMode(SFE_ENS160_STANDARD); 
}

void loop()
{  
  //if (bWiFi)
  //  getNTPTime();

  if (humiditySensor.available())
  {
    sd.temperature = humiditySensor.getTemperature();
    sd.humidity = humiditySensor.getHumidity();

 #if DUMP_SERIAL
    Serial.print("Temperature: ");
    Serial.print(sd.temperature, 2);
    Serial.print(" C\t");
    Serial.print("Humidity: ");
    Serial.print(sd.humidity, 2);
    Serial.print("% RH");
    Serial.println();
#endif

#ifdef ENABLE_DISPLAY
    u8x8.setCursor(0, 2);
    u8x8.print("T: ");
    u8x8.print(sd.temperature, 2);
    u8x8.print(" C\t");
    u8x8.setCursor(0, 3);
    u8x8.print("RH: ");
    u8x8.print(sd.humidity, 2);
    u8x8.print(" %");
#endif
  }

  sd.aoi = airSensor.getAQI();
  sd.tvoc = airSensor.getTVOC();
  sd.eco2 = airSensor.getECO2();

#if DUMP_SERIAL
  Serial.print("Air Quality Index (1-5) : ");
  Serial.println(sd.aoi);
  Serial.print("Total Volatile Organic Compounds: ");
  Serial.print(sd.tvoc);
  Serial.println("ppb");
  Serial.print("CO2 concentration: ");
  Serial.print(sd.eco2);
  Serial.println("ppm");
#endif 

/*
    Serial.println("T(C):");
    Serial.println(myENS.getTempCelsius());

    Serial.println("RH(%):");
    Serial.println(myENS.getRH());
*/
#ifdef ENABLE_DISPLAY
  u8x8.setCursor(0, 4);
  u8x8.print("Status: ");
  u8x8.print(airSensor.getFlags());
  u8x8.setCursor(0, 5);
  u8x8.print("AOI: ");
  u8x8.print(sd.aoi);
  u8x8.setCursor(0, 6);
  u8x8.print("TVOC: ");
  u8x8.print(sd.tvoc);
  u8x8.print("ppb");
  u8x8.setCursor(0, 7);
  u8x8.print("CO2: ");
  u8x8.print(sd.eco2);
  u8x8.print("ppm");
#endif

  if (bWiFi){
    server.handleClient();
    MDNS.update();
  }
  ///delay(1000);
}
