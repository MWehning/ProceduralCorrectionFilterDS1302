 #include <Arduino.h>
 #include <TFT_eSPI.h> // Graphics and font library for ST7735 driver chip
 #include <SPI.h>
 #include <WiFi.h>
 #include "time.h"
 #include <Ds1302.h>

#define TFT_GREY        0x5AEB  // New colour
#define BAUD            115200  // Serialspeed

#define RST             27      // RTC Pins
#define CLC             26
#define DAT             25

#define CYCLES       5          // Cycles until wifi is severed
#define INTERVAL     12         // interval between error comparison(hours)

const char* ssid       = "Vodafone-C318";
const char* password   = "RFpr6cT6NHbtrME8";
const char* ntpServer = "pool.ntp.org";

const long  gmtOffset_sec = 7200;      // Offset for ntp request
const int   daylightOffset_sec = 0;

TFT_eSPI tft = TFT_eSPI();             // Invoke library, pins defined in User_Setup.h
Ds1302 rtc(RST, CLC, DAT);             // set up new instant for rtc
struct tm timeinfo;

bool wifiState = false;                // global value representing if wifi connection is available

//current ntp time
int     at = 0;
uint8_t hh = 0;
uint8_t mm = 0;
uint8_t ss = 0;

//current rtc time
int     at2 = 0;
uint8_t hh2 = 0;
uint8_t mm2 = 0;
uint8_t ss2 = 0;


int last = 0;                         // keep track of differences between rtc and ntp
int diff = 0;
int lastDiff = 1;

float averageDiff = -1;               // average diff(really important once wifi is severed)


int intervalAdj = INTERVAL*3600;      // transform interval into seconds

// global variables to make 1 sec adjustments work
int sign = 0;                         // determines which way rtc is adjusted
int adjSteps = intervalAdj;           // time between those adjusts(fraction of intervalAdj)
int totalAdjustment = 0;              //total amount of seconds adjusted for right now

bool getNTPTime(){
  if(!getLocalTime(&timeinfo)){       
    Serial.println("Failed to obtain time");
    return false;
  }
  // convert to time in seconds and then back
  at = timeinfo.tm_hour*3600+timeinfo.tm_min*60+timeinfo.tm_sec;  
  hh = at / 3600;
  mm = at % 3600 / 60;
  ss = at % 60;
  return true;
}

void getRTCTime(){
  Ds1302::DateTime now2;
  rtc.getDateTime(&now2);
  // convert to time in seconds and then back while adding adjustment seconds
  at2 = now2.hour*3600+now2.minute*60+now2.second + totalAdjustment; 
  hh2 = at2/3600;
  mm2 = at2 % 3600 / 60;
  ss2 = at2 % 60;
}

bool setupDisplay(){
  tft.init();                               
  tft.setRotation(3);
  delay(10);
  tft.fillScreen(TFT_BLACK);
  tft.setCursor(0, 0, 4);
  tft.setTextColor(TFT_YELLOW,TFT_BLACK);
  tft.printf("Connecting...");
  tft.setCursor(0,0,4);
  return true;
}

bool setupWifi(){
  Serial.printf("Connecting to %s ", ssid); 
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {       // wait until connection is established
      delay(500);
      Serial.print(".");
  }
  Serial.println("");
  wifiState = true;                            
  return(true);
}

bool setRTC(uint8_t hours,uint8_t minutes,uint8_t seconds){
    Ds1302::DateTime dt = {
      // year, month and day can be ignored since we are focusing 
      // on seconds, minutes and hours exclusively
        .year = 1,
        .month = Ds1302::MONTH_JAN,
        .day = 1,
        .hour = hours,
        .minute = minutes,
        .second = seconds,
        .dow = Ds1302::DOW_MON
    };
    rtc.setDateTime(&dt);
    last = at;
    return true;

}

void disconnectWifi(){
  WiFi.disconnect(true);                
  WiFi.mode(WIFI_OFF);
  Serial.println("Wifi disconnected");
  wifiState = false;
}

void drawTime(){
  tft.setCursor(10, 0, 7);
  tft.setTextColor(TFT_YELLOW,TFT_BLACK);
  tft.printf("%02d:%02d:%02d",hh,mm,ss);
  tft.setCursor(10, 50, 7);
  tft.setTextColor(TFT_RED,TFT_BLACK);
  tft.printf("%02d:%02d:%02d",hh2,mm2,ss2);
  // calculate difference between both clocks every second
  diff = at - at2;                            
  tft.setCursor(10, 100, 4);
  tft.setTextColor(TFT_BLUE,TFT_BLACK);
  tft.printf("%02d       ",totalAdjustment);
  tft.setCursor(80, 100, 4);
  tft.setTextColor(TFT_GREEN,TFT_BLACK);
  tft.printf("%f       ",averageDiff);
}

void setup(void) {
  Serial.begin(BAUD);   
  // setup various aspects of the project making sure they each work
  for(int x = 0;x<10;x++)
    Serial.println("");
  delay(1000);  
  if(!setupDisplay()){
    Serial.println("Screen Setup Failure");
    while(1){}
  }
  Serial.println("Screen Setup Success");
  delay(1000); 
  if(!setupWifi()){
    
    Serial.println("Wifi Failure");
    while(1){}
  }
  Serial.println("Wifi Connected");
  
  //get ntp time and set it as initial value of external rtc
  configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);
  delay(1000); 

  if(!getNTPTime()){
    Serial.println("NTP Failure");
    while(1){}
  }
  Serial.println("NTP Available");
  rtc.init();

  if(!setRTC(hh,mm,ss)){
    Serial.println("RTC Failure");
    while(1){}
  }
  Serial.println("RTC Set");

  // all checks complete, ready for takeoff
  tft.fillScreen(TFT_BLACK);
  Serial.println("Setup Complete");
  Serial.printf("%02d:%02d:%02d   Starting Time \n",hh,mm,ss);
}

void loop() {
  // wait for CYCLES, then sever connection
  for(int x = 0;x < CYCLES;x++){
    // wait for INTERVAL hours between difference checks
    for(int x = 1;x<intervalAdj+1;x++){
      //display current time on display every second
      getNTPTime();
      getRTCTime();
      drawTime();
      // check if adjustment is possible this second
      if((x+adjSteps/2) % adjSteps == 0){
        totalAdjustment = totalAdjustment + sign;
        Serial.printf("adjusted for %02d\n",sign);
      }
      delay(1000);
    }

    //average difference based on last average and new difference
    averageDiff = (averageDiff + diff)/2;

    //look which adjustment steps make sense and which direction to adjust to
    if(averageDiff<0){
      sign = -1;

      adjSteps = intervalAdj/ (-1*averageDiff);
    }
    else if(averageDiff==0){
      sign = 0;
      adjSteps = intervalAdj;
    }
    else{
      sign = 1;
      adjSteps = intervalAdj/ averageDiff;
    }

    //if wifi is disconnected continue without it
    if (wifiState == true){
      setRTC(hh,mm,ss);
      Serial.println("adjusted using wifi");
    }
    else if(wifiState == false){
      setRTC(hh2,mm2,ss2+totalAdjustment);
      Serial.println("adjusted without wifi involvement");
    }
    totalAdjustment = 0;
  }
  disconnectWifi();
  Serial.println("Connection severed");
}