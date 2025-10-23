/*
 * home_monitor
 * Send temperature data to ThingSpeak at 10-minute intervals
 * as timed by DH3231 Real Time Clock:
 *    + indoor temperature (DHT22)
 *    + indoor humidity (DHT22)
 *    + outdoor temperature (a plain thermistor)
 *
 * Edited with Arduino IDE 2.3.3
 *    
 * HARDWARE: Arduino Mega2560 + ESP-01S
 * 
 */

 /*
  * Copyright () 2025 by David G.Sparks
  * _wdtOff code copyright by Microchip, Inc.
  * All Rights Reserved
  * 
  * Caveat lector: this code reflects a triumph of function
  * over form, meaning that it stands the way it happened
  * to have come together at the moment it began to work 
  * as I intended. It is not as well-organized as I might like
  * nor does it contain all of the comments I wish to include.
  * Code can stop running if it encounters certain runtime errors. 
  * It is recommended to have a Serial Monitor connected when running, 
  * to view the Serial Output as an aid to understanding the code.
  * To be clear: the code does not require a serial monitor.
  * It is designed to operate even when Serial output has nowhere to go. 
  * The code is provided here for illustration purposes, only, 
  * No claim is made regarding any other use for the code.
  * 
  * 
  * This code is free software; you can redistribute it and/or
  * modify it under the terms of the GNU Lesser General Public
  * License as published by the Free Software Foundation; either
  * version 2.1 of the License, or (at your option) any later version.
  *
  * This code is distributed in the hope that it will be informative,
  * but WITHOUT ANY WARRANTY; without even the implied warranty of
  * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
  * Lesser General Public License for more details.
  *
  * You should have received a copy of the GNU Lesser General
  * Public License along with this library; if not, write to the
  * Free Software Foundation, Inc., 59 Temple Place, Suite 330,
  * Boston, MA  02111-1307  USA
  */

/* The following libraries are included with Arduino IDE */
#include <Wire.h>             // I2C for DS3231 (included with Arduino IDE)
#include <SPI.h>              // SPI for SD card device (included with Arduino ODE)
#include <SD.h>               // control SD card device (included with Arduino IDE)

/* These additional libraries need to be installed using the Library Manager */
#include <DHT.h>              // Adafruit library
#include <DHT_U.h>            // Adafruit dependency
#include <DS3231.h>           // DS3231 Real Time Clock (by Andrew Wickert)
#include <WiFiEsp.h>          // talk to the 8266 (by bportaluri)
#include <ThingSpeak.h>       // talk to ThingSpeak (by MathWorks)
#include <TM1637Display.h>    // LED display module (by Avishay Orpaz)

// create this file in the same folder that contains this Arduino program
#include "secrets.h"          // supply your WiFi and Thingspeak access codes

// definitions
#define DHTPIN 12             // DHT data on digital pin 12
#define DHTTYPE DHT22
#define CLOCK_INTERRUPT_PIN 2 // DS3231 SQW connects to digital 2
#define SD_CHIP_SELECT 53     // selects SD card device
#define ESP_BAUDRATE  9600    // not 115200, because slower is surer
#define TM1637_DIO 39         // TM1637 Data I/O
#define TM1637_CLK 41         // TM1637 Clock signal

/* software objects */

// DHT sensor object
DHT dht(DHTPIN, DHTTYPE);

// DS3231 real time clock object
DS3231 RTClock; // instantiate real time clock object

// DateTime object
DateTime clockData;

// SD file object
File dataFile;

// ESP8266 WiFi object
WiFiEspClient espClient;

// TM1637 object
TM1637Display display(TM1637_CLK, TM1637_DIO);

/* variables */

// prototype function for outside air temperature
float getOAT ();                  // has to be float for thingspeak library
// these global variables not used if duplicated in that function?
double therm, thermAvg;
float outsideAirTemperature;    // must be type float to work with thingspeak library
unsigned char thermCount = 0;

/* These values come from the secrets.h file, included above */
// WiFi variables
char ssid[] = SECRET_SSID;
char pass[] = SECRET_PASS;
// ThingSpeak variables
unsigned long myChannelNumber = SECRET_CH_ID;
const char * myWriteAPIKey = SECRET_WRITE_APIKEY;

// other global variables
bool doneOnce = true;           // change to true after done once. why is this here?
float humidity;                 // used with thingspeak library
float indoorTemperature;        // used with thingspeak library
bool sdCardInitialized = false;
bool DS18B20_Found = false;

// clock-related declarations
volatile bool alarmEventFlag = false;

// alarm interrupt handler changes flag to true
// Note: this is the actual function definition, not a prototype
void rtcISR() {alarmEventFlag = true;}

// alarm-related function prototypes
DateTime addSecondsToTime( int secondsToAdd, DateTime theTime);
void setTheAlarm(DateTime alarmTime);
void set_Ten_Minute_Alarm(DateTime referenceTime);
void printDateAndTime(DateTime referenceTime);


void setup() {
  // setup code runs only once, at power-on or reset:
  Serial.begin(9600); // Serial Monitor
  Serial1.begin(9600); // HW Serial1 for ESP-01
  dht.begin(); // start the DHT22 sensor
  Wire.begin(); // start I2C

  // start up the data card
  Serial.print("Initializing SD card...");

  if (!SD.begin(SD_CHIP_SELECT)) {
    // error message to Serial monitor if SD card cannot be initialized
    Serial.println("SD CARD initialization failed!");
  } else {
    // report successful SD card initialization
    sdCardInitialized = true;
    Serial.println("SD card initialized OK");
  }

  // write this session's data table header to the file
  if (sdCardInitialized) {
    // read the clock
    clockData = RTClib::now();
    // write the header
    dataFile = SD.open("mydata.txt", FILE_WRITE);
    dataFile.println("Home Temperatures and Humidity");
    dataFile.print("Data logging initiated at ");
    // the current date
    dataFile.print(clockData.month()); dataFile.print("/");
    dataFile.print(clockData.day()); dataFile.print("/");
    dataFile.print(clockData.year());
    dataFile.print("\t"); // tab

    // the current time
    dataFile.print(clockData.hour()); dataFile.print(":");
    if (clockData.minute() < 10) dataFile.print("0");
    dataFile.print(clockData.minute()); dataFile.print(":");
    if (clockData.second() < 10) dataFile.print("0");
    dataFile.println(clockData.second());

    // the field headers for data in tab-delimited columns
    // Date <tab> Time <tab> Indoor_T <tab> Indoor_H <tab> OAT
    dataFile.println("Date\tTime\tIndoor_T\tIndoor_H\tOAT");

    // close the file on the SD card
    dataFile.close();
  }
  
  /* configure Arduino to act on a signal from the DS3231 RTC */
  attachInterrupt(
      // respond to signal on pin 2
      digitalPinToInterrupt(CLOCK_INTERRUPT_PIN),
      // call function 'rtcISR' upon receiving a FALLING signal
      rtcISR, FALLING
  );

  /* output progress reports to the Serial Monitor */
  Serial.println("Fetching clock data.");
  // initialize the alarm on the clock
  // read the clock
  clockData = RTClib::now();
  Serial.println("Printing clock data.");
  printDateAndTime(clockData);
  Serial.println();
  Serial.println("Did you see any clock data, above?");
  
  /* tell DS3231 to send signal after waiting 10 minutes */
  set_Ten_Minute_Alarm(clockData);

  // get the ESP-01 and ThingSpeak going
  Serial.print("Searching for ESP8266..."); 
  // initialize ESP module
  WiFi.init(&Serial1);

  // check for the presence of the ESP8266 device
  if (WiFi.status() == WL_NO_SHIELD) {
    Serial.println("WiFi shield not present");
    // HALT EXECUTION IF THE ABOVE TEST FAILS?
    while (true);
  }
  // If we get this far, it means 8266 is available
  Serial.println("found it!");

  Serial.println("Initializing Thingspeak");
  ThingSpeak.begin(espClient);  // Initialize ThingSpeak
  Serial.println("ThingSpeak initialized!"); 

   //TM1637
  display.setBrightness(0x01);  /* level 1 in range of 0 - 15 */
  display.showNumberDec(8888);  /* indicate the TM1637 is working */

} // end of setup()

void loop() {
  // main code runs repeatedly:
  if (alarmEventFlag == true) {
    // read the clock
    clockData = RTClib::now();
    // advance the alarm
    set_Ten_Minute_Alarm(clockData);
    // turn off this code's alarm flag
    alarmEventFlag = false;
    
    // print the date and time to the Serial Monitor
    printDateAndTime(clockData);
    
    // add spaces
    Serial.print("  ");
    
    // obtain the temperatures and humidity...

    outsideAirTemperature = getOAT();               // returns Fahrenheit
    humidity = dht.readHumidity();                  // percent relative humidity
    indoorTemperature = dht.readTemperature(true);  // true selects Fahrenheit
    // Check if any reads failed and exit early (to try again).
    if (isnan(humidity) || isnan(indoorTemperature)) {
      Serial.println(F("Failed to read from DHT sensor!"));
      return;
    }
    // output the data as a space-delimited string to the serial monitor
    Serial.print("Indoor temperature: ");
    Serial.print(indoorTemperature);  
    Serial.print("  Humidity: ");
    Serial.print(humidity);
    Serial.print("  OAT: ");
    Serial.println(outsideAirTemperature);
  
    // output the same data as tab-delimited columns to the SD card 

    if (sdCardInitialized) {
      // open a 'datafile' object for writing to the file
      dataFile = SD.open("mydata.txt", FILE_WRITE);

      // write the current date
      dataFile.print(clockData.month()); dataFile.print("/");
      dataFile.print(clockData.day()); dataFile.print("/");
      dataFile.print(clockData.year());
      dataFile.print("\t"); // tab

      // write the current time
      dataFile.print(clockData.hour()); dataFile.print(":");
      if (clockData.minute() < 10) dataFile.print("0");
      dataFile.print(clockData.minute()); dataFile.print(":");
      if (clockData.second() < 10) dataFile.print("0");
      dataFile.print(clockData.second());
      dataFile.print("\t"); // tab
    
      // write the data
      dataFile.print(indoorTemperature);  
      dataFile.print("\t"); // tab
      dataFile.print(humidity);
      dataFile.print("\t"); // tab
      dataFile.print(outsideAirTemperature);

      // the line end
      dataFile.println();

      // close the file
      dataFile.close();
    }  // SD card

    // Send the data via WiFi to ThingSpeak

    // Connect or reconnect to WiFi
    if(WiFi.status() != WL_CONNECTED){
      Serial.print("Attempting to connect to SSID: ");
      Serial.println(SECRET_SSID);
      while(WiFi.status() != WL_CONNECTED){
        WiFi.begin(ssid, pass);  // Connect to WPA/WPA2 network. Change this line if using open or WEP network
        Serial.print(".");
        delay(5000);     
      } 
      Serial.println("\nConnected.");
    }

    // set the fields with the values
    ThingSpeak.setField(1, indoorTemperature);
    ThingSpeak.setField(2, humidity);
    ThingSpeak.setField(3, outsideAirTemperature);
  
    // write to the ThingSpeak channel
    int x = ThingSpeak.writeFields(myChannelNumber, myWriteAPIKey);
    // check result of upload; 200 indicates success
    if(x == 200){
      Serial.println("Channel update successful.");
    }
    else{
      // any value different from 200 indicates trouble
      // report this result to the Serial Monitor
      Serial.println("Problem updating channel. HTTP error code " + String(x));
    }

  }  // end processing alarmEventFlag

  /* At this point the main loop has completed its tasks.
   * Program execution returns to the top of the loop, where
   * the test for the AlarmEventFlag will skip over all of the loop code
   * repeatedly until the next time the DS3231 sends a signal.
   */

}  // main loop

/*******************************************************************/

/* The code that follows here defines functions that were
 * previously declared as prototypes at the top of this program
 */

DateTime addSecondsToTime(int secondsToAdd, DateTime theTime) {
  // the "unixtime()" function returns the DS3231 time
  // as number of seconds in an unisgned long integer.
  // NOTE TO FUSSY PEOPLE: this number might not equal  
  // what you would expect for the conventional "Unix Time".
  // It does not matter.
  // For this purpose, we briefly need only a number of seconds.
  uint32_t theTimeInSeconds = theTime.unixtime(); 
  
  // It turns out that a new DataTime variable can be defined
  // by supplying a time as a number of seconds.
  // (Technically, the DateTime object constructor is overloaded,
  //  and this is one of the declarations for it.)
  DateTime newTime(theTimeInSeconds + secondsToAdd);

  // send the updated DateTime value back to the caller
  return newTime;
}

void setTheAlarm(DateTime alarmTime) {

  /*  Here is a copy of the declaration
   *  of the DS3231 library function
   *  that sets Alarm 1:
   *  
   *  void setA1Time(
   *    byte A1Day, 
   *    byte A1Hour, 
   *    byte A1Minute, 
   *    byte A1Second, 
   *    byte AlarmBits, 
   *    bool A1Dy, 
   *    bool A1h12, 
   *    bool A1PM
   *  ); 
  */

    // set the alarm to the new time
    RTClock.setA1Time(
      alarmTime.day(),
      alarmTime.hour(),
      alarmTime.minute(),
      alarmTime.second(),
      0x00001100, // this mask means alarm when minutes and seconds match
      false, false, false
    );

    // activate the alarm

    /* Tricks I learned about how the alarm on the DS3231 
     * signals an interrupt on the Arduino.
     * 
     * The alarm pin on the DS3231 is labeled, "SQW".
     * The DS3231 sends signals by changing the voltage
     * on its SQW pin. The voltage can be HIGH or LOW.
     * 
     * We want the voltage on SQW start as HIGH.
     * DS3231 signals an alarm by changing the signal to LOW.
     * 
     * Arduino's hardware can detect the change in the voltage
     * through digital pins having this special ability.
     * Pin 2 is one of those special pins on the Mega2560.
     * The hardware then literally interrupts the CPU,
     * causing it to switch over and run our special code.
     * 
     * A change from HIGH to LOW is called "FALLING".
     * The "attachInterrupt()" code in the setup() block
     * of this sketch tells Arduino to interrupt the CPU
     * when it detects a FALLING signal from the DS3231.
     * 
     * After that happens, the SQW pin will remain LOW.
     * The DS3231 cannot send any more "FALLING" signals
     * as long as the SQW pin remains LOW.
     * 
     * What this means to us as code writers is that
     * it is our job to tell the DS3231
     * to restore the HIGH voltage on its SQW pin.
     * We do this by "clearing" certain bits
     * in two control registers on the DS3231.
     * 
     * The DS3231 library provides code statements 
     * for this purpose. The trick, which I struggled
     * for a while to understand, is that the 
     * statement named "checkIfAlarm()" 
     * has a non-obvious side effect.
     * It clears one of those critical bits.
     * It is the only statement in the DS3231 library
     * that accomplishes this important step.
     * Which means we have to use it even if we do not
     * really need to check the status of the alarm.
     */
    RTClock.turnOffAlarm(1); // modifies register 0Eh in the DS3231
    RTClock.checkIfAlarm(1); // modifies register 0Fh in the DS3231

    // Now, we can turn on the alarm!
    RTClock.turnOnAlarm(1);
  
}

void set_Ten_Minute_Alarm(DateTime referenceTime) {
  // calculate number of seconds to the next greater minute
  int secondsUntilNextWholeTenMinutes = 600 - referenceTime.second();
  //  allow extra minute if within 3 seconds
  if (secondsUntilNextWholeTenMinutes < 3) {
    secondsUntilNextWholeTenMinutes += 600;  
  }
  
  // set alarm for next whole ten minute mark
  setTheAlarm(
    addSecondsToTime (
      secondsUntilNextWholeTenMinutes,
      referenceTime
    )
  );  
}

void printDateAndTime(DateTime referenceTime) {
  // print the current date
  Serial.print(referenceTime.month()); Serial.print("/");
  Serial.print(referenceTime.day()); Serial.print("/");
  Serial.print(referenceTime.year());
  Serial.print("  ");

  // print the current time
  Serial.print(referenceTime.hour()); Serial.print(":");
  if (referenceTime.minute() < 10) Serial.print("0");
  Serial.print(referenceTime.minute()); Serial.print(":");
  if (referenceTime.second() < 10) Serial.print("0");
  Serial.print(referenceTime.second());
   
}

float getOAT ()
{
  const double calibrateTherm = 1.08;
  double therm = 0;
  double thermAvg = 0;
  char thermCount = 0;

  // smooth the measurement by accumulating an average of 15 readings 
  while (thermCount < 15)
  {
    // the fixed resistor R2 in the circuit measures 99.3 kOhm
    // estimate thermistor R1 resistance from divided voltage 
    therm = ((1023.0 / analogRead(A7))-1)*99300 * calibrateTherm;
    // display the calculated thermistor resistance in Ohms
    Serial.print(therm);
    Serial.print(" => ");
    // convert thermistor resistance to temperature
    // see github.com/iowadave/thermistors
    // degrees Kelvin
    therm = 1 / ((1.0/263.15) - (log(57670/therm) / 3936 ));
    // degrees Celsius
    therm = therm - 273.15;
    // degrees Fahrenheit
    therm = (therm * 1.8) + 32;
    // display the calculated temperature in degerees Fahrenheit
    Serial.println(therm);

    // calculate cumulative moving average

    thermAvg = (therm + (thermAvg * thermCount));
    thermCount = thermCount + 1;
    thermAvg = thermAvg / thermCount;
    // repeat at 1-second intervals
    delay(1000);
    // until thermCount = 15
  }

  // display the averaged OAT as integer on the TM1637
  int t = (int) round(thermAvg);
  display.showNumberDec(t, false);

  // return the averaged temperature,
  // typecasting it to float, 
  // to match the definition of this function
  // and for thingspeak compatability
  return (float) thermAvg;
}

// end of listing
