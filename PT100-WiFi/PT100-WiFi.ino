// 2025-01-21
// Arduino is connected to WiFi and implements an UDP server
// It responds to received messages about Pt100 acquisition
// RTC
// protocol with message ID

#include <WiFiS3.h>
#include <EEPROM.h>
#include "arduino_secrets.h" 
#include "LedMatrix.h"
#include "AFlib.h"
#include <Adafruit_MAX31865.h>

//----------------------------------
//#define OHM_DEBUG 
#define ZERO_K   -273.15
float temperature = ZERO_K;   //0 kelvin
float temp_raw = ZERO_K;      //0 kelvin
float maxerr = 0;
float ohm = 0.0;

// adj 
float m = 1.0;
float q = 0.0;
bool enable_adj = true;
#define X1 2.7
#define Y1 4.5
#define X2 28.7
#define Y2 31.0

#define RREF      431.0
#define RNOMINAL  100.0
Adafruit_MAX31865 thermo = Adafruit_MAX31865(10,11,12,13);
//----------------------------------
#ifdef OHM_DEBUG
#define TAB_ITEMS 21 
typedef struct {
    float celsius;
    float ohm;
} PT100_TABLE;
PT100_TABLE ptab[TAB_ITEMS] = {
    { -40.00, 84.27 },
    { -30.00, 88.22 },
    { -20.00, 92.16 },
    { -10.00, 96.09 },
    { 0.00, 100.00 },
    { 10.00, 103.90 },
    { 20.00, 107.79 },
    { 25.00, 109.74 },
    { 30.00, 111.67 },
    { 40.00, 115.54 },
    { 50.00, 119.40 },
    { 60.00, 123.24 },
    { 70.00, 127.07 },
    { 80.00, 130.89 },
    { 90.00, 134.70 },
    { 100.00, 138.50 },
    { 110.00, 142.29 },
    { 120.00, 146.06 },
    { 130.00, 149.82 },
    { 140.00, 153.58 },
    { 150.00, 157.31 },
};
float ohms2celsius(float ohm);
#endif
//----------------------------------
// real time clock using NTP protolol
#include "RTC.h"
#include <NTPClient.h>

#define TXRX_DEBUG 0

uint32_t LED_WELCOME[] = {
  0xe80ac0e8,
  0x8400001,
  0x77355177
};

uint32_t LED_OK[] = {
  0x7a44a,
  0x84b04b04,
  0xa87a4000
};
uint32_t LED_E1[] = {
  0x78841,
  0x84087884,
  0x8408788
};
uint32_t LED_A[] = {
  0x20050088,
  0x880f808,
  0x80880880
};

LED_MATRIX screen;

int cnt_row = 0;
int cnt_bit = 0;

// WiFi
int status = WL_IDLE_STATUS;
char ssid[] = SECRET_SSID;        // your network SSID (name)
char pass[] = SECRET_PASS;        // your network password (use for WPA, or use as key for WEP)
int keyIndex = 0;                 // your network key index number (needed only for WEP)
unsigned int localPort = 2390;    // local port to listen on

// rx buffer
#define LENBUF 255
char rx_full_buffer[LENBUF + 1];
char rx_full_bufcpy[LENBUF + 1];

char packetBuffer[LENBUF + 1];     // buffer for Rx packet
char ReplyBuffer[LENBUF + 1];      // message Tx buffer
char TxBuffer[LENBUF + 1];         // buffer for Tx
WiFiUDP Udp;                       // WiFi object 

// general
unsigned long time_latch;
unsigned long time_latch_di;
unsigned long time_now;

// counter
int rx_counter = 0;
int tx_counter = 0;

// retentive data stored into e2prom
int retentive_data = 0;

// RTC data
int yy = 0;
int mm = 0;
int dd = 0;
int hh = 0;
int mn = 0;
int ss = 0;
int wd = 0;
char weekDay[8][20];

NTPClient timeClient(Udp);

// ------------------------------------------------
// time statistics
// ------------------------------------------------

#define MAX_TIME_RXTX      0
#define MAX_TIME_AN        1
#define MAX_TIME_PROX      2
#define MAX_TIME_LOOP      3
#define MAX_TIME_INTERLOOP 4

#define MAX_TIMES 5
long lt[MAX_TIMES];
long mt[MAX_TIMES];
long at[MAX_TIMES];
long latch[MAX_TIMES];
long samples[MAX_TIMES];

void st_clear() 
{
  for(int i = 0; i < MAX_TIMES; i++) {
    lt[i] = 0x7fffffff;
    mt[i] = 0;
    at[i] = 0;
    latch[i] = 0;
    samples[i] = 0;
  }
}
void st_start(int index) 
{
  if(index < 0 || index >= MAX_TIMES)
    return;
  latch[index] = micros();
  samples[index]++;
}
void st_stop(int index) 
{
  if(index < 0 || index >= MAX_TIMES)
    return;
    if(samples[index] == 0)
      return;

  long tt = micros() - latch[index];
  if (mt[index] < tt) mt[index] = tt;
  if (lt[index] > tt) lt[index] = tt;
  tt += at[index] * (samples[index] - 1);
  at[index] = tt / samples[index];
}
bool RTC_read() {
  yy = 0;
  mm = 0;
  dd = 0;
  hh = 0;
  mn = 0;
  ss = 0;
  wd = 0;

  if (!RTC.isRunning()) 
    return false;

  RTCTime now;
  RTC.getTime(now);
  
  yy = now.getYear();
  mm = (int) now.getMonth() + 1;
  dd = now.getDayOfMonth();
  hh = now.getHour();
  mn = now.getMinutes();
  ss = now.getSeconds();
  wd = (int) now.getDayOfWeek();
  return true;
}
// =================================================
//             initialization
// =================================================
void setup() {
  
  st_clear();
  WeekDay_init();

  //Initialize serial
  Serial.begin(115200);
  screen.init(true);
  
  thermo.begin(MAX31865_3WIRE);
  if(enable_adj == true)
  {
    m = (Y2-Y1)/(X2-X1);
    q = (X2*Y1-X1*Y2)/(X2-X1);
  }

  screen.showImage(LED_WELCOME);
  while (!Serial) {
    ; // wait for serial port to connect. Needed for native USB port only
  }
  delay(3000);

  Serial.println("--- Start initialization ---");
  rx_full_bufcpy[0] = 0;  // empty rx buffer copy
  clear_rx_full_buffer();

  // check for the WiFi module:
  if (WiFi.status() == WL_NO_MODULE) {
    Serial.println("Communication with WiFi module failed!");
    // don't continue
    screen.showImage(LED_E1);
    while (true);
  }
  
  String fv = WiFi.firmwareVersion();
  Serial.print("WiFi firmware version: ");
  Serial.println(fv);
  Serial.print("WiFi firmware latest version: ");
  Serial.println(WIFI_FIRMWARE_LATEST_VERSION);
  if (fv < WIFI_FIRMWARE_LATEST_VERSION) {
    Serial.println("Please upgrade the firmware");
  }
  IPAddress ip("192.168.1.123");
  IPAddress ipdns_gw("192.168.1.1");
  IPAddress ipmask("255.255.255.0");
  WiFi.config(ip, ipdns_gw, ipdns_gw, ipmask);

  // attempt to connect to WiFi network:
  while (status != WL_CONNECTED) {
    Serial.print("Attempting to connect to SSID: ");
    Serial.println(ssid);
    // Connect to WPA/WPA2 network. Change this line if using open or WEP network:
    status = WiFi.begin(ssid, pass);
    // wait 10 seconds for connection:
    delay(10000);
  }
  Serial.println("Connected to WiFi");
  // if you get a connection, report back via serial:
  Udp.begin(localPort);
  delay(1000);  
  printWifiStatus();
  
  // Initialize the RTC
  RTC.begin();
  timeClient.begin();
  timeClient.update();
  auto timeZoneOffsetHours = 1;
  auto unixTime = timeClient.getEpochTime() + (timeZoneOffsetHours * 3600);
  Serial.print("Unix time = ");
  Serial.println(unixTime);
  RTCTime timeToSet = RTCTime(unixTime);
  RTC.setTime(timeToSet);
  bool rtc_ok = RTC_read();

  sprintf(ReplyBuffer, "%04d-%02d-%02d %02d:%02d:%02d %s", yy, mm, dd, hh, mn, ss, WeekDay_ptr(wd));
  Serial.print("RTC: " );
  Serial.println(ReplyBuffer);

  if (!rtc_ok) {
    screen.showImage(LED_A);
    Serial.println("RTC error");
  } else {
    screen.showImage(LED_OK);
  }   
  Serial.println("--- Start main loop ---");
  delay(1000);
  time_latch = millis();
  time_latch_di = 0;
}
// ------------------------------------------------
//  debug WiFi status
// ------------------------------------------------
void printWifiStatus() {

  // print the SSID of the network you're attached to:
  Serial.print("SSID: ");
  Serial.println(WiFi.SSID());
  // print your board's IP address:
  IPAddress ip = WiFi.localIP();
  Serial.print("IP Address: ");
  Serial.println(ip);
  // print the received signal strength:
  long rssi = WiFi.RSSI();
  Serial.print("signal strength (RSSI):");
  Serial.print(rssi);
  Serial.println(" dBm");
}
// ------------------------------------------------
//  get week day
// ------------------------------------------------
void WeekDay_init()
{
  strcpy(&weekDay[0][0], "???");
  strcpy(&weekDay[1][0], "Lunedi");
  strcpy(&weekDay[2][0], "Martedi");
  strcpy(&weekDay[3][0], "Mercoledi");
  strcpy(&weekDay[4][0], "Giovedi");
  strcpy(&weekDay[5][0], "Venerdi");
  strcpy(&weekDay[6][0], "Sabato");
  strcpy(&weekDay[7][0], "Domenica");
}
char * WeekDay_ptr(int n) 
{
  if(n > 0 && n < 8) 
  {
    return &weekDay[n][0];
  }
  return &weekDay[0][0];
}
// ============================
//      RX FUNCTIONS
// ============================
// clear rx buffer
void clear_rx_full_buffer() 
{
  rx_full_buffer[0] = 0;
}    

// compare rx buffer with previous
bool cmp_rx_full_buffer(char *ptr) 
{
  int n = strlen(ptr);
  int m = strlen(rx_full_bufcpy);
  if(n != m || n == 0) 
  {
    Serial.println("RX message size changed");
    return false;
  }
  for (int i = 0; i < n; i++) 
  {
    if(rx_full_bufcpy[i] != ptr[i])
      return false;
  }  
  Serial.println("RX message NOT changed");
  return true;
}
// use packets to build rx buffer without STX and ETX
char * chk_rx_full_buffer(int len) 
{
  bool done = false; 
  int i, j, count;
  int i_end = -1;
  int i_start1 = -1;
  int i_start2 = -1;
  
  if(len <= 0)
     return NULL;

  strcat(rx_full_buffer, packetBuffer);

  // search MESSAGE END
  count = strlen(rx_full_buffer);
  for(i = count - 1; i >= 0; i--)
  {
    if(i_end < 0) 
    { 
      if(rx_full_buffer[i] == '\n')  i_end = i;
    } else if(i_start2 < 0) 
    {
      if(rx_full_buffer[i] == '>') i_start2 = i;
    } else if(i_start1 < 0) 
    {
      if(rx_full_buffer[i] == '<')  i_start1 = i;
    } 
    if(rx_full_buffer[i] < ' ') rx_full_buffer[i] = ' ';   
  }
  if(i_end >= 0 && (i_start2 < 0 || i_start1 < 0)) 
  {
    int j = 0;
    for(i = i_end + 1; i < count; i++) {
      rx_full_buffer[j++] = rx_full_buffer[i] ;
    }
    Serial.println("---intital message lost");
    rx_full_buffer[j] = 0;
    return NULL;
  }
  if(i_end < 0 || i_start2 < 0 || i_start1 < 0) 
  {
    Serial.println("---message not completed");
    return NULL;
  }
  rx_full_buffer[i_end] = 0;
  if(cmp_rx_full_buffer(&rx_full_buffer[i_start1]) == true) {
    Serial.println("---double message");
    rx_full_buffer[0] = 0;
    return NULL;
  }
  strcpy(rx_full_bufcpy, &rx_full_buffer[i_start1]);
  return &rx_full_buffer[i_start2 + 1];
}
  
// -----------------------------------------------------------
//        Response transmission 
// -----------------------------------------------------------
void Udp_write(IPAddress ip, int port) 
{
static int mID = 0;
  mID++;
  sprintf(TxBuffer, "<%04X>%s\r\n", mID, ReplyBuffer);       
  Udp.beginPacket(ip, port);
  Udp.write(TxBuffer);
  Udp.endPacket();
  if(mID > 0x7fff) mID = 0;
}
// =================================================
//                main loop
// =================================================
void loop() {
  // if there's data available, read a packet
  int rx_number;

  st_stop(MAX_TIME_INTERLOOP);
  st_start(MAX_TIME_LOOP);

  int packetSize = Udp.parsePacket();
  if (packetSize) {
    st_start(MAX_TIME_RXTX);
    IPAddress remoteIp = Udp.remoteIP();
    int port = Udp.remotePort();
    int len = Udp.read(packetBuffer, LENBUF);
    packetBuffer[len] = 0;

    if(TXRX_DEBUG) {
      Serial.print("Rx packet from ");
      Serial.print(remoteIp);
      Serial.print(":");
      Serial.println(port);
      Serial.print(packetBuffer);
    }
    char *  ptr = chk_rx_full_buffer(len);
    if(ptr != NULL) {
      // process message 
      rx_counter++;
      Serial.print("rxcounter=");
      Serial.println(rx_counter);
      if(ptr[0] == 'T') {
        tx_counter++;
        sscanf(&ptr[1], "%d", &rx_number);
        Serial.print(" txcounter=");
        Serial.print(tx_counter);
        Serial.print(" err=");
        Serial.println(rx_number - tx_counter);
        sprintf(ReplyBuffer, "%ld/%ld/%ld rxtx;%ld/%ld/%ld prox;%ld/%ld/%ld an;%ld/%ld/%ld loop;%ld/%ld/%ld i-loop;%ld rx cnt;%ld tx cnt; %d err", 
          lt[MAX_TIME_RXTX],      at[MAX_TIME_RXTX],      mt[MAX_TIME_RXTX], 
          lt[MAX_TIME_PROX],      at[MAX_TIME_PROX],      mt[MAX_TIME_PROX], 
          lt[MAX_TIME_AN],        at[MAX_TIME_AN],        mt[MAX_TIME_AN], 
          lt[MAX_TIME_LOOP],      at[MAX_TIME_LOOP],      mt[MAX_TIME_LOOP], 
          lt[MAX_TIME_INTERLOOP], at[MAX_TIME_INTERLOOP], mt[MAX_TIME_INTERLOOP], 
          rx_counter, tx_counter, rx_number-tx_counter);
      } else if(ptr[0] == 't') {
        tx_counter++;
        // reply with 5 items
        sprintf(ReplyBuffer, "%s;r%d;%2.2f Ω;%2.2f ºC (raw);%2.2f ºC", ptr, rx_counter, ohm, temp_raw, temperature);
      } else if(ptr[0] == 'X') {
        // reset counters
        tx_counter = 0;
        rx_counter = 0;
        st_clear();
        sprintf(ReplyBuffer, "X_DONE");
      } else if(ptr[0] == 'Y') {  
        tx_counter++;
        RTC_read();
        sprintf(ReplyBuffer, "%04d-%02d-%02d;%02d:%02d:%02d;%s", 
          yy, mm, dd, hh, mn, ss, WeekDay_ptr(wd));
      } else {
        sprintf(ReplyBuffer, "CMD_ERR");
      } 
      Udp_write(remoteIp, port);

      if(TXRX_DEBUG) {
        Serial.println("Rx packet (cleaned):");
        Serial.println(ptr);
        Serial.println("Tx packet:");
        Serial.print(ReplyBuffer);
      }
      clear_rx_full_buffer();
    }
    st_stop(MAX_TIME_RXTX);
  }
  // get sensors and perform a relay rule
  time_now = millis();

  st_start(MAX_TIME_AN); 
  temp_raw = thermo.temperature(RNOMINAL, RREF);
  temperature = m * temp_raw + q; 

  uint16_t rtd = thermo.readRTD();
  float ratio = rtd;
  ratio /= 32768;
  ohm = RREF * ratio;

#ifdef OHM_DEBUG
  float celsius = ohms2celsius(ohm); 
  Serial.print("Ohm/rawT/adjT ; "); 
  Serial.print(ohm, 2);
  Serial.print(" ; ");
  Serial.print(temp_raw, 2);
  Serial.print(" ; ");
  Serial.println(temperature, 2);
#endif
  st_stop(MAX_TIME_AN); 
  
  float inc = 0.5;
  if (temperature < 0) inc = -0.5;  
  int t = int(temperature * 10.0 + inc);
  screen.showDSNumber3(t, 1);

  st_stop(MAX_TIME_LOOP);
  st_start(MAX_TIME_INTERLOOP);
}
#ifdef OHM_DEBUG
float ohms2celsius(float ohm)
{
  int idx = -1;
  for(int i = 0; i < TAB_ITEMS; i++)
  {
      if(ohm >= ptab[i].ohm) 
      {
          idx = i;
      }
  }  
  if(idx < 0 || idx >= TAB_ITEMS - 1) return ZERO_K;
  float x1 = ptab[idx].ohm;
  float y1 = ptab[idx].celsius;
  float x2 = ptab[idx+1].ohm;
  float y2 = ptab[idx+1].celsius;
  float m = (y2-y1)/(x2-x1);
  float q = (x2*y1-x1*y2)/(x2-x1);
  return m*ohm+q;
}
#endif

