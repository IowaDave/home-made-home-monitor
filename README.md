# home-made-home-monitor
Build a home monitor system with an Arduino

![A screenshot showing graphs of temperature and humidity data for a house](images/frosty-morning.jpg)

Connect some indoor and outdoor sensors to an Arduino, take measurements at regular intervals, send the data to an external server for logging and visualize it online.

A combination of inexpensive components plus a non-commercial account with Thingspeak examines how commercial home-monitoring systems might work. It could be of educational interest to anyone interested in looking behind the scenes into the so-called Internet of Things.

![photo of an Arduino Mega2560 controller and a breadboard containing a number of small components](images/thermogizmo.jpg)

## Components
* [An Arduino Mega2560 or similar development board](#development-board)
* [DHT22 Humidity and Temperature Sensor](#dht22)
* A Thermistor, resistor, and a pair of long wires
* ESP8266 controller having Expressif AT-type firmware installed
* SD card adapter
* DS3231 Real Time Clock module
* TM1637 Display module
* Optional voltage regulator
* Solderless breadboard and hookup wire

Each one of the components is discussed in separate sections of this article. Links to the sections are provided above for convenience.

## Development Board
This project needed a Mega2560-style development board for two reasons.

The first reason is memory capacity. Arduino IDE compiles the example progem to almost 37,000 bytes. This size is too large for an Uno-style board, which has only a 32K-byte flash (program) memory. The Mega2560 gives a roomy 256K of flash.

Moreover, the program and the device libraries that support it combine to require more than 2,500 bytes of random-access memory. An Uno has, at most, 2,048 bytes. The Mega2560 provides a comfortable 32,768 bytes.

The second reason is that it calls for two Serial input-output connections in the hardware. One connection is for Serial communication with an attached computer. The second goes to the ESP8266 for internet access.

I considered using the Arduino Software Serial library and an Uno-style board for the ESP8266 but decided to play it safe by sending all Serial I/O through hardware.

## Arduino Device Code Libraries
Most of the devices in this project are what I call &ldquo;semi-smart&rdquo; devices. It means they do part of the work internally, gathering, organizing and storing the information they can provide.

For these, an Arduino program will use custom-tailored code that it &ldquo;includes&rdquo; from a <em>library</em>. The following selection of code lines from the example program lists the device libraries it includes.

~~~ c
/* The following libraries come pre-installed with Arduino IDE */
#include <Wire.h>             // for I2C communication with the DS3231 RTC
#include <SPI.h>              // for SPI communication with SD card device
#include <SD.h>               // to read and write files on SD card device

/* These additional libraries need to be installed using the Library Manager */
#include <DHT.h>              // Adafruit library
#include <DHT_U.h>            // Adafruit dependency
#include <DS3231.h>           // DS3231 Real Time Clock (by Andrew Wickert)
#include <WiFiEsp.h>          // talk to the 8266 (by bportaluri)
#include <ThingSpeak.h>       // talk to ThingSpeak (by MathWorks)
#include <TM1637Display.h>    // LED display module (by Avishay Orpaz)
~~~

Some device libraries are provided by companies that manufacture or sell the devices. The Adafruit libaries supporting the DHT22 are example of seller-provided code.

Volunteers contribute libraries for most of the devices used in this project. The Arduino IDE includes a Library Manager that you can use to discover and install device libraries. 

The Manager shows the names of the people who contributed each library. I have identified such libraries in this project by including the contributors' names in parentheses.

<h2 id="dht22">DHT22 Humidity and Temperature Sensor</h2>
This one measures temperature and humidity inside the house. I had some on and and they seem to work, so I use one here. I discuss contemporary controversy about these devices below, after showing the code.

As we enter the second quarter of the 21st century we can choose better products, perhaps. Adafruit recommends using a newer type of sensor instead of the DHT22. You can read their commentary [at this link](https://learn.adafruit.com/modern-replacements-for-dht11-dht22-sensors/overview).

Here is the code that fetches the indoor temperature and humidity from the DHT22. The variables named "humidity" and "temperature" were declared earlier in the program. The following procedure assigns values to them from the sensor.

~~~ c
    humidity = dht.readHumidity();                  // percent relative humidity
    indoorTemperature = dht.readTemperature(true);  // true selects Fahrenheit
    /* Did either reading return NaN, meaning Not a Number?
     * If so, make up to ten more attempts before giving up.
     */
    if (isnan(humidity) || isnan(indoorTemperature)) {
      int DHTattempt = 0;
      while ( (DHTattempt < 10) &&
              (isnan(humidity) || isnan(indoorTemperature))
            ) 
        /* repeat the tests until either
         * both readings are valid numbers
         * or the count of attempts reaches 10 */
      {
        humidity = dht.readHumidity();                  // percent relative humidity
        indoorTemperature = dht.readTemperature(true);  // true selects Fahrenheit
        DHTattempt += 1;  
      }
    }
    /* if still failing, exit this attempt to log data */
    if (isnan(humidity) || isnan(indoorTemperature)) {
      Serial.println(F("Failed to read from DHT sensor!"));
      return;
    }

    /* Reaching this point means we have obtained
     * valid readings from the DHT22 sensor */
~~~

I pause development of the article here so that I may upload the file to GitHub and see how it looks there.
