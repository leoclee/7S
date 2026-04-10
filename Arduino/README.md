# Introduction
This is the Arduino code for a clock which uses the [LOLIN C3 Mini](https://www.wemos.cc/en/latest/c3/c3_mini.html). This is based on my previous iteration of this project [7-segment-clock](https://github.com/leoclee/7-segment-clock), which ran on the WEMOS/LOLIN D1 Mini, an ESP8266 dev board with the same form factor.

# Installation
1. Install [Arduino IDE](https://www.arduino.cc/en/software#ide)
2. Use the [Boards Manager](https://support.arduino.cc/hc/en-us/articles/360016119519-Add-boards-to-Arduino-IDE) to install:
   - [esp32](https://github.com/espressif/arduino-esp32) (by Espressif Systems) **version 2.0.17** (‼️IMPORTANT‼️)
3. Use the [Library Manager](https://support.arduino.cc/hc/en-us/articles/5145457742236-Add-libraries-to-Arduino-IDE) to install:
   - [WiFiManager](https://github.com/tzapu/WiFiManager) (by tzapu)
   - [Arduinojson](https://arduinojson.org) (by Benoit Blanchon)
   - [FastLED](https://github.com/FastLED/FastLED) (by Daniel Garcia)
   - [hp_BH1750](https://github.com/Starmbi/hp_BH1750) (by Stefan Armborst)
   - [TzDbLookup](https://github.com/anonymousaga/TzDbLookup) (by anonymousaga)
   - [Async TCP](https://github.com/ESP32Async/AsyncTCP) (by ESP32Async)
   - [ESP Async WebServer](https://github.com/ESP32Async/ESPAsyncWebServer) (by ESP32Async)
4. Install [arduino-littlefs-upload](https://github.com/earlephilhower/arduino-littlefs-upload?tab=readme-ov-file#installation)
5. Open clock.ino using Arduino IDE
6. Connect the C3 Mini to the computer using a USB-C cable
7. Select the LOLIN C3 Mini as the board and the USB port ([steps](https://support.arduino.cc/hc/en-us/articles/4406856349970-Select-board-and-port-in-Arduino-IDE))
8. Compile and upload the sketch
   - Sketch > Upload
9. Upload the data files using arduino-littlefs-upload ([steps](https://github.com/earlephilhower/arduino-littlefs-upload?tab=readme-ov-file#usage-uploading-a-filesystem-to-the-device))
   - Note that you will not be able to upload files while the Serial Monitor is active

# Startup States
While starting up, the built-in WS2812B RGB LED on the C3 Mini will light solid in various colors to indicate the state  
1. 🔴 Load preferences, set up LEDs, initialize sensors
2. 🔵 Captive portal via access point awaiting WiFi setup (look for open WiFi SSID 7SClock-XXXXXXXX)
3. 🟢 IP-based time zone detection, NTP sync