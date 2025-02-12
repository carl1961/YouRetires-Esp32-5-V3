//============================================================================
// TITLE:         YouRetires.ino
// DATE:          2/8/2025
// AUTHOR:        All Things Bobot
//                https://www.youtube.com/@AllThingsBobot
//                https://allthingsbobot.com
//                bobot@allthingsbobot.com
// VERSION:       1.0
//
// SUITABLE FOR:  Elecrow CrowPanel 5.0" ESP32 HMI Display (v2)
//
// DESCRIPTION:   Displays retirement date countdown information
//
// UPDATED:       02.08.2025  Bobot   Initial release
//                02.12.2025  Bobot   Add seconds to time difference calc
//                                    to fix variance in raw work days;
//                                    Highlight current date on first launch
//                                    of retirement date adjustment calendar
//============================================================================

#include <Arduino.h>
#include <WiFi.h>
#include <time.h>
#include <TimeLib.h>
#include <esp_sntp.h>
#include <Preferences.h>

#include "secrets.h"


// LVGL and UI
#include <lvgl.h>
#include "ui.h"

// Config the display panel and touch panel in gfx_conf.h
#include "gfx_conf.h"
static lv_disp_draw_buf_t draw_buf;
static lv_color_t disp_draw_buf1[screenWidth * screenHeight / 8];
static lv_color_t disp_draw_buf2[screenWidth * screenHeight / 8];
static lv_disp_drv_t disp_drv;


// WiFi Credentials / IP Setup 
const char* ssid = SECRET_WIFISSID;
const char* password = SECRET_WIFIPASS;
IPAddress ip(192, 168, 20, 22);
IPAddress gateway(192, 168, 20, 1);
IPAddress subnet(255, 255, 255, 0);
IPAddress dns(192, 168, 20, 1); //primaryDNS

// Time variables
const char *ntpServer1 = "pool.ntp.org";
const char *ntpServer2 = "time.nist.gov";
const char *time_zone = "EST5EDT,M3.2.0,M11.1.0";   // TimeZone rule for America/New_York including daylight adjustment rules 

// Global Project variables
struct tm gtimeinfo;
Preferences gpreferences; 
bool gblnFirstRun = true;
bool gblnFirstRun2 = true;
unsigned int guintRetireMonth = 0;
unsigned int guintRetireDay = 0;
unsigned int guintRetireYear = 0;
unsigned int guintDaysRemaining = 0;
unsigned int guintUpdateHour = 2;   // hour of day to update internal clock via NTP
unsigned int guintUpdateMin = 2;   // minute of day to update internal clock via NTP
const unsigned long gulngUpdateInterval = 1 * 60000ul; // (1 minute in milliseconds) - how often to update display fields 


/* Display flushing */
void my_disp_flush(lv_disp_drv_t *disp, const lv_area_t *area, lv_color_t *color_p)
{
  uint32_t w = ( area->x2 - area->x1 + 1 );
  uint32_t h = ( area->y2 - area->y1 + 1 );

  tft.pushImageDMA(area->x1, area->y1, w, h,(lgfx::rgb565_t*)&color_p->full);

  lv_disp_flush_ready( disp );
}


void my_touchpad_read(lv_indev_drv_t *indev_driver, lv_indev_data_t *data)
{
  uint16_t touchX, touchY;
  bool touched = tft.getTouch( &touchX, &touchY);
  if( !touched )
  {
    data->state = LV_INDEV_STATE_REL;
  }
  else
  {
    data->state = LV_INDEV_STATE_PR;

    //Set the coordinates
    data->point.x = touchX;
    data->point.y = touchY;

    Serial.print( "Data x " );
    Serial.println( touchX );

    Serial.print( "Data y " );
    Serial.println( touchY );
  }
}


void StartWifi()
{
  // Connect to WiFi network
  WiFi.mode(WIFI_STA);
  WiFi.config(ip, dns, gateway, subnet); 
  WiFi.begin(ssid, password);

  Serial.println();
  Serial.println("Connecting Wifi");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    {Serial.print(".");}
  }
  
  Serial.println();
  Serial.println("WiFi connected.");
  Serial.print("IP address: ");
  Serial.println(WiFi.localIP());
  Serial.print("Wifi RSSI = ");
  Serial.println(WiFi.RSSI());
}


void GetRetireDate()
{
  // Create a “storage space” in the flash memory in read/write mode (false)
  // Note: space name is limited to 15 character
  gpreferences.begin("you-retires", false);

  // Get the Retire value(s), if the key does not exist, return a default value of 0
  // Note: key name is limited to 15 chars.
  // Store values read from EEPROM into global vars
  guintRetireMonth = gpreferences.getUInt("retiremonth", 0);
  guintRetireDay = gpreferences.getUInt("retireday", 0);
  guintRetireYear = gpreferences.getUInt("retireyear", 0);

  // Close the Preferences
  gpreferences.end();

  DisplayRetirementDate();
}


void SetRetireDate()
{
  // Create a “storage space” in the flash memory in read/write mode (false)
  // Note: space name is limited to 15 character
  gpreferences.begin("you-retires", false);

  guintRetireMonth = calendardate.month;
  guintRetireDay = calendardate.day;
  guintRetireYear = calendardate.year;

  Serial.println("Storing date to EEPROM");

  gpreferences.clear();
  // Store the counter to the Preferences
  gpreferences.putUInt("retiremonth", guintRetireMonth);
  gpreferences.putUInt("retireday", guintRetireDay);
  gpreferences.putUInt("retireyear", guintRetireYear);

  // Close the Preferences
  gpreferences.end();

  DisplayRetirementDate();
}


void DisplayRetirementDate()
{
  // Display retirement date on display
  char chrRetireDate[17];
  sprintf(chrRetireDate,"%d/%d/%d",guintRetireMonth,guintRetireDay,guintRetireYear);
  Serial.println(chrRetireDate);
  lv_label_set_text(ui_RetireDate, chrRetireDate);
}


void DisplayCurrentTime()
{
  if(!getLocalTime(&gtimeinfo)){
    Serial.println("No time available (yet)");
    lv_label_set_text(ui_CurrentTime, "--:-- --/--");
    return;
  }
  Serial.println("Displaying Current Time");
  Serial.println(&gtimeinfo, "%A, %B %d %Y %H:%M:%S zone %Z %z ");

  // Update display field for readings time and date 
  char chrCurrentTime[13];
  strftime(chrCurrentTime, 13, "%H:%M %m/%d", &gtimeinfo);
  Serial.println(chrCurrentTime);
  lv_label_set_text(ui_CurrentTime, chrCurrentTime);
}


void DisplayRemainingRawDays()
{
  guintDaysRemaining = CalculateDaysBetweenDates(gtimeinfo.tm_year+1900, gtimeinfo.tm_mon+1, gtimeinfo.tm_mday, guintRetireYear, guintRetireMonth, guintRetireDay);
 
  Serial.print("Number of RAW days is: ");
  Serial.println(guintDaysRemaining);
  char chrRawDays[8];
  sprintf(chrRawDays, "%d", guintDaysRemaining);
  lv_label_set_text(ui_Days, chrRawDays);
}


// Function to calculate the number of days between two dates, without weekends and defined holidays
unsigned int CalculateDaysBetweenDates(int pStartYear, int pStartMonth, int pStartDay, int pEndYear, int pEndMonth, int pEndDay) 
{
  time_t StartDateTime;
  time_t EndDateTime;

  double timediff = CalculateTimeDiff(pStartYear, pStartMonth, pStartDay, 0, 0, 0,
                                      pEndYear, pEndMonth, pEndDay, 0, 0, 0,
                                      StartDateTime, EndDateTime);
  unsigned int totaldays = round(timediff / SECS_PER_DAY);

  Serial.print("1-Days to Go--->");
  Serial.println(totaldays);

  // Loop through each day and count weekend days 
  unsigned int weekendDays = 0;
  for (time_t t = StartDateTime; t <= EndDateTime; t += SECS_PER_DAY) { 
    // Check if it’s a weekend day (Saturday or Sunday) 
    if (weekday(t) == 7 || weekday(t) == 1) { 
      weekendDays++;
    }
  } 
  totaldays -= weekendDays;

  char chrTemp[60];
  sprintf(chrTemp,"2-Remove %d weekend days yields %d days",weekendDays,totaldays);
  Serial.println(chrTemp);

  // Check if it’s a holiday 
  unsigned int holidaysCount = 0;
  int holidays[][12] = { 
    {1, 1},   // New's Years Day
    {1, 20},  // Martin Luther King Day
    {2, 15},  // President's Day 
    {5, 31},  // Memorial Day 
    {6, 19},  // Juneteenth
    {7, 4},   // July 4 
    {9, 1},   // Labor Day 
    {10, 13}, // Columbus Day
    {11, 14}, // Veteran's Day 
    {11, 27}, // Thanksgiving 
    {11, 28}, // Thanksgiving Friday 
    {12, 25}  // Christmas 
  };
  
  int numHolidays = sizeof(holidays) / sizeof(holidays[0]);
  for (time_t t = StartDateTime; t <= EndDateTime; t += SECS_PER_DAY) {   
    for (int i = 0; i < numHolidays; i++) { 
      tmElements_t tm; 
      breakTime(t, tm);
      if (tm.Month == holidays[i][0] && tm.Day == holidays[i][1]) { 
        holidaysCount++; 
        break; 
      } 
    } 
  } 

  totaldays -= holidaysCount;
  memset(chrTemp, 0, sizeof(chrTemp));
  sprintf(chrTemp,"3-Remove %d holiday days yields %d days",holidaysCount,totaldays);
  Serial.println(chrTemp);

  return totaldays;
}


// Helper function to calculate and return time difference between start date and end date
double CalculateTimeDiff(int pStartYear, int pStartMonth, int pStartDay, int pStartHour, int pStartMinute, int pStartSecond,
                        int pEndYear, int pEndMonth, int pEndDay, int pEndHour, int pEndMinute, int pEndSecond,
                        time_t &pStartDateTime, time_t &pEndDateTime)
{
  tmElements_t StartDate;
  tmElements_t EndDate;

  StartDate.Year = CalendarYrToTm(pStartYear);
  StartDate.Month = pStartMonth;
  StartDate.Day = pStartDay;
  StartDate.Hour = pStartHour;
  StartDate.Minute = pStartMinute;
  StartDate.Second = pStartSecond;
  Serial.print("StartDate.Year->");
  Serial.println(StartDate.Year);
  Serial.print("StartDate.Month->");
  Serial.println(StartDate.Month);
  Serial.print("StartDate.Day->");
  Serial.println(StartDate.Day);
    
  EndDate.Year = CalendarYrToTm(pEndYear);
  EndDate.Month = pEndMonth;
  EndDate.Day = pEndDay;
  EndDate.Hour = pEndHour;
  EndDate.Minute = pEndMinute;
  EndDate.Second = pEndSecond;  
  Serial.print("EndDate.Year->");
  Serial.println(EndDate.Year);
  Serial.print("EndDate.Month->");
  Serial.println(EndDate.Month);
  Serial.print("EndDate.Day->");
  Serial.println(EndDate.Day);

  time_t StartDateTime = makeTime(StartDate);
  time_t EndDateTime = makeTime(EndDate);

  Serial.println(StartDateTime);
  Serial.println(EndDateTime);

  double timediff = difftime(EndDateTime,StartDateTime);
  Serial.print("DIFF>>");
  Serial.println(timediff);

  // Return values to caller
  pStartDateTime = StartDateTime;
  pEndDateTime = EndDateTime;

  return timediff;
}


// Display remaining actual years, months and days until retirement date
void DisplayRemainingYMD()
{
  int remyears, remmonths, remdays;  
  CalculateYMD(gtimeinfo.tm_year+1900, gtimeinfo.tm_mon+1, gtimeinfo.tm_mday, guintRetireYear, guintRetireMonth, guintRetireDay, remyears, remmonths, remdays);
  
    Serial.print("Number of years is: ");
    Serial.println(remyears);

    Serial.print("Number of months is: ");
    Serial.println(remmonths);

    Serial.print("Number of days is: ");
    Serial.println(remdays);

    char chrYears1[8];
    sprintf(chrYears1, "%d", remyears);

    char chrMonths1[8];
    sprintf(chrMonths1, "%d", remmonths);

    char chrDays1[8];
    sprintf(chrDays1, "%d", remdays);

    lv_label_set_text(ui_Years1, chrYears1);
    lv_label_set_text(ui_Months1, chrMonths1);
    lv_label_set_text(ui_Days1, chrDays1);
}


// Function to calculate the difference between two dates and times in years, months, and days
void CalculateYMD(int startYear, int startMonth, int startDay, int endYear, int endMonth, int endDay, int &years, int &months, int &days) {
  // Start date
  int currentYear = startYear;
  int currentMonth = startMonth;
  int currentDay = startDay;
  
  years = endYear - startYear;
  months = endMonth - startMonth;
  days = endDay - startDay;

  // Adjust months and years if the end date is before the start date in the current year
  if (endMonth < startMonth || (endMonth == startMonth && endDay < startDay)) {
    years--;
  }
  currentYear += years;

  // Calculate months
  if (months < 0) { 
    months += 12;
  }
  if (endDay < startDay || (endDay == startDay)) {
    months--;
  }
  currentMonth = startMonth + months;
  if (currentMonth > 12) {
    currentMonth -= 12;
  }

  // Calculate days
  if (days < 0) {
    days += monthDays(currentMonth, currentYear); 
  }

}


// Define the number of days in each month
int monthDays(int month, int year) {
  int daysInMonth[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
  if (month == 2 && isLeapYear(year)) {
    return 29;
  }
  return daysInMonth[month - 1];
}


// Is this a leap year?
bool isLeapYear(int year) {
  if (year % 4 == 0) {
    if (year % 100 == 0) {
      if (year % 400 == 0) {
        return true;
      }
      return false;
    }
    return true;
  }
  return false;
}


// Function to report error message to serial monitor and display on status field
void ErrorStatus(char *pchrErrorDesignator)
{
  Serial.println(pchrErrorDesignator);
  SetAlertColors(ui_Status1);
  lv_label_set_text(ui_Status1, pchrErrorDesignator);
  delay(1000);
}


void SetAlertColors(lv_obj_t *puiControlName)
{
  lv_obj_set_style_text_color(puiControlName,lv_color_hex(0xFFFF00),LV_PART_MAIN);
  lv_obj_set_style_bg_color(puiControlName,lv_color_hex(0xFF0000), LV_PART_MAIN);
}


void SetNormalColors(lv_obj_t *puiControlName)
{
  lv_obj_set_style_text_color(puiControlName,lv_color_hex(0x25302A),LV_PART_MAIN);
  lv_obj_set_style_bg_color(puiControlName,lv_color_hex(0x0A8421), LV_PART_MAIN);
}


// Callback function (gets called when time adjusts via NTP)
void timeavailable(struct timeval *t) 
{
  Serial.println("Got time adjustment from NTP!");
  DisplayCurrentTime();
  
  // Update Status field on display
  SetNormalColors(ui_Status1);
  char chrSuccessMsgNTP[40];
  strftime(chrSuccessMsgNTP, 40, "Successful NTP time updated %H:%M %m/%d", &gtimeinfo);
  lv_label_set_text(ui_Status1, chrSuccessMsgNTP);
}


// Start NTP client, initialize it with the time zone and set the real time clock of the ESP32
void updateNTP() 
{
  ErrorStatus("Waiting on NTP server...");

  //refresh display to show above message
  lv_timer_handler();
  delay(5);  

  // Set the timezone and NTP server information
  // this is the most convenient method of setting the timezone information and the address of the server
  // and will connect to the server and set the internal RTC
  configTzTime(time_zone, ntpServer1, ntpServer2);
}


// This sets Arduino Stack Size - comment out this line to use default 8K stack size
SET_LOOP_TASK_STACK_SIZE(12*1024); // 12K

void setup()
{
  Serial.begin(9600);
  delay(1000);

  // GPIO init
  #if defined (CrowPanel_50) || defined (CrowPanel_70)
    pinMode(38, OUTPUT);
    digitalWrite(38, LOW);
    pinMode(17, OUTPUT);
    digitalWrite(17, LOW);
    pinMode(18, OUTPUT);
    digitalWrite(18, LOW);
    pinMode(42, OUTPUT);
    digitalWrite(42, LOW);
	
  #elif defined (CrowPanel_43)
    pinMode(20, OUTPUT);
    digitalWrite(20, LOW);
    pinMode(19, OUTPUT);
    digitalWrite(19, LOW);
    pinMode(35, OUTPUT);
    digitalWrite(35, LOW);
    pinMode(38, OUTPUT);
    digitalWrite(38, LOW);
    pinMode(0, OUTPUT);//TOUCH-CS
  #endif

  // Display Prepare
  tft.begin();
  tft.fillScreen(TFT_BLACK);
  tft.setTextSize(2);
  delay(200);

  lv_init();

  delay(100);

  lv_disp_draw_buf_init(&draw_buf, disp_draw_buf1, disp_draw_buf2, screenWidth * screenHeight/8);
  
  /* Initialize the display */
  lv_disp_drv_init(&disp_drv);
  
  /* Change the following line to your display resolution */
  disp_drv.hor_res = screenWidth;
  disp_drv.ver_res = screenHeight;
  disp_drv.flush_cb = my_disp_flush;
  disp_drv.full_refresh = 1;
  disp_drv.draw_buf = &draw_buf;
  lv_disp_drv_register(&disp_drv);

  /* Initialize the (dummy) input device driver */
  static lv_indev_drv_t indev_drv;
  lv_indev_drv_init(&indev_drv);
  indev_drv.type = LV_INDEV_TYPE_POINTER;
  indev_drv.read_cb = my_touchpad_read;
  lv_indev_drv_register(&indev_drv);

  tft.fillScreen(TFT_BLACK);

  // Squareline Studio UI initialization
  ui_init();

  // Set notification call-back function that gets called when NTP updates the internal clock
  // NTP server address could be acquired via DHCP
  // NOTE: This call should be made BEFORE esp32 acquires IP address via DHCP,
  // otherwise SNTP option 42 would be rejected by default.
  sntp_set_time_sync_notification_cb(timeavailable);

  // Start WiFi
  StartWifi();

  //sntp_set_sync_interval(60*60*1000); //set time period after which an sntp server request for time update is made. 
  // NOTE: This method could be used if the default sync interval of 3 hrs needs to be changed.
  
}


void loop()
{
  //required for display refresh
  lv_timer_handler();
  delay(5);

  //Display stack and heap size on serial monitor for diagnostics
  if (gblnFirstRun)
  {
    Serial.printf("Stack:%d,Heap:%lu\n", uxTaskGetStackHighWaterMark(NULL), (unsigned long)ESP.getFreeHeap());
    gblnFirstRun = false;

    // Update internal realtime clock from NTP call
    updateNTP();
    delay(1000);

    // Get retire date from EEPROM (required to load retirement date after power loss)
    GetRetireDate();
  }

  // User changed retirement date via display calendar
  // Validate that proposed date is later than current date
  // If valid, store new retirement date to EEPROM and update display fields
  if (calendardate_trigger)
  {
    time_t StartDateTime;   // must be supplied as call by ref variable to function call but not used
    time_t EndDateTime;     // must be supplied as call by ref variable to function call but not used
    double timediff = CalculateTimeDiff(gtimeinfo.tm_year+1900, gtimeinfo.tm_mon+1, gtimeinfo.tm_mday, gtimeinfo.tm_hour, gtimeinfo.tm_min, 0,
                                        calendardate.year, calendardate.month, calendardate.day, 0, 0, 0,
                                        StartDateTime, EndDateTime);

    if (timediff > 0)
      {
        char chrCalDate[30];
        sprintf(chrCalDate,"Clicked date: %02d/%02d/%d", calendardate.month, calendardate.day, calendardate.year);
        Serial.println(chrCalDate);
        SetRetireDate();
        DisplayCurrentTime();
        DisplayRemainingRawDays();
        DisplayRemainingYMD();
      }
    calendardate_trigger = false;
  }

    // On first run, get current time and update display fields
  if(!getLocalTime(&gtimeinfo)){   // it may take some time to sync time :)
    Serial.println("No time available (yet)");
    delay(1000);
    return;
  }
  else
  {
    if (gblnFirstRun2)
      {    
        DisplayCurrentTime();
        DisplayRemainingRawDays();
        DisplayRemainingYMD();
        lv_calendar_set_today_date(ui_RetirementCalendar, gtimeinfo.tm_year+1900, gtimeinfo.tm_mon+1, gtimeinfo.tm_mday);
        lv_calendar_set_showed_date(ui_RetirementCalendar, gtimeinfo.tm_year+1900, gtimeinfo.tm_mon+1);
        gblnFirstRun2 = false;
      }
  }

  // Update internal clock via NTP call at defined time every day  (also internals of configTzTime has default sync interval of 3 hrs)
  // But this assures more immediate update for DST changes
  if (gtimeinfo.tm_hour == guintUpdateHour && gtimeinfo.tm_min == guintUpdateMin && gtimeinfo.tm_sec == 0) 
  { 
    updateNTP(); 
    delay(1000); // Wait to avoid multiple updates in the same second 
  }

  // Update display fields every defined interval
  static unsigned long lastUpdateTime = millis(); 
  if (millis() - lastUpdateTime > gulngUpdateInterval)
  {
    Serial.println("****** Minute timer hit");
    lastUpdateTime = millis();
    DisplayCurrentTime();
    DisplayRemainingRawDays();
    DisplayRemainingYMD();
  }
}
