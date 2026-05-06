#include <WiFiManager.h>
#include <HTTPClient.h>
#include <WifiClientSecure.h>
#include <ArduinoJson.h>
#include <Preferences.h>
#include <FastLED.h>
#include <hp_BH1750.h>
#include "TzDbLookup.h"
#include <ESPmDNS.h>
#include <LittleFS.h>
#include <AsyncTCP.h>  // needed for ESPAsyncWebServer
#include <ESPAsyncWebServer.h>

#define NUM_LEDS_PER_SEGMENT 1
#define NUM_LEDS_HOUR (14 * NUM_LEDS_PER_SEGMENT) + 1
#define NUM_LEDS_MINUTE (28 * NUM_LEDS_PER_SEGMENT) + 3
#define PIN_HOUR 1
#define PIN_MINUTE 0
#define PIN_BUILTIN 7

// LED
CRGB ledsBuiltin[1];
CRGB ledsHour[NUM_LEDS_HOUR];
CRGB ledsMinute[NUM_LEDS_MINUTE];
CHSV ledsHourHsv[NUM_LEDS_HOUR];      // parallel array for logic
CHSV ledsMinuteHsv[NUM_LEDS_MINUTE];  // parallel array for logic
CHSV fromColor = CHSV(0, 0, 0);
CHSV toColor = CHSV(0, 0, 0);
CHSV currentColor = CHSV(0, 0, 0);
CHSV savedColor;
unsigned long lastColorChangeTime = 0;
unsigned long colorSaveInterval = 15000;  // milliseconds to wait for a color change to trigger a save

bool fading = false;           // true when transitioning between requested colors; false otherwise
uint8_t lerp = 0;              // used to keep track of fade progress
bool blinkEnabled = false;     // true to indicate half-second colon blinking
bool rainbowEnabled = false;   // true to slowly change hues instead of the static selected color
uint8_t rainbowHueOffset = 0;  // used to keep track of hue offset when rainbow color mode enabled
bool twelveHour = false;       // true: 12-hour format; false: 24-hour/military format; 24-hour time format is more popular worldwide
String timeZoneState = "";     // IANA time zone ID to maintain state ("": auto, even though a real IANA time zone ID might be in use)

// This is a Google Trust Services cert, the root Certificate Authority that
// signed the server certificate for https://ipwho.is This certificate is
// valid until June 21, 2036
const char* rootCACertificate = R"string_literal(
-----BEGIN CERTIFICATE-----
MIICCTCCAY6gAwIBAgINAgPlwGjvYxqccpBQUjAKBggqhkjOPQQDAzBHMQswCQYD
VQQGEwJVUzEiMCAGA1UEChMZR29vZ2xlIFRydXN0IFNlcnZpY2VzIExMQzEUMBIG
A1UEAxMLR1RTIFJvb3QgUjQwHhcNMTYwNjIyMDAwMDAwWhcNMzYwNjIyMDAwMDAw
WjBHMQswCQYDVQQGEwJVUzEiMCAGA1UEChMZR29vZ2xlIFRydXN0IFNlcnZpY2Vz
IExMQzEUMBIGA1UEAxMLR1RTIFJvb3QgUjQwdjAQBgcqhkjOPQIBBgUrgQQAIgNi
AATzdHOnaItgrkO4NcWBMHtLSZ37wWHO5t5GvWvVYRg1rkDdc/eJkTBa6zzuhXyi
QHY7qca4R9gq55KRanPpsXI5nymfopjTX15YhmUPoYRlBtHci8nHc8iMai/lxKvR
HYqjQjBAMA4GA1UdDwEB/wQEAwIBhjAPBgNVHRMBAf8EBTADAQH/MB0GA1UdDgQW
BBSATNbrdP9JNqPV2Py1PsVq8JQdjDAKBggqhkjOPQQDAwNpADBmAjEA6ED/g94D
9J+uHXqnLrmvT/aDHQ4thQEd0dlq7A/Cr8deVl5c1RxYIigL9zC2L7F8AjEA8GE8
p/SgguMh1YQdc4acLa/KNJvxn7kjNuK8YAOdgLOaVsjh4rsUecrNIdSUtUlD
-----END CERTIFICATE-----
)string_literal";
const char* prefsNamespace = "clock-prefs";

static AsyncWebServer server(80);
static AsyncEventSource events("/events");
Preferences prefs;
hp_BH1750 BH1750;

// piezo buzzer
const int TONE_OUTPUT_PIN = 4;
const int TONE_PWM_CHANNEL = 0;

/**
  * @brief Generic handler for server requests tied to bool preferences.
  * @param key Name associated with the preference, which will be used in calls to {@link saveBoolPreference} as necessary.
  * @param boolRef Reference to the boolean associated with the preference.
  * @param postEnableCallback Additional logic to run after setting preference to true.
  * @param postDisableCallback Additional logic to run after setting preference to false.
  */
void handleBoolPreference(AsyncWebServerRequest* request, const char* key, bool& boolRef, std::function<void()> postEnableCallback = nullptr, std::function<void()> postDisableCallback = nullptr) {
  String enabled = request->getParam("enabled")->value();
  enabled.toLowerCase();
  if (enabled == "true") {
    if (!boolRef) {
      Serial.print(key);
      Serial.println(" enabled");
      boolRef = true;
      saveBoolPreference(key, boolRef);

      // call post-enable callback, if provided
      if (postEnableCallback) {
        postEnableCallback();
      }

      sendState();
    } else {
      Serial.print(key);
      Serial.println(" already enabled");
    }
    request->send(204);
  } else if (enabled == "false") {
    if (boolRef) {
      Serial.print(key);
      Serial.println(" disabled");
      boolRef = false;
      saveBoolPreference(key, boolRef);

      // call post-disable callback, if provided
      if (postDisableCallback) {
        postDisableCallback();
      }

      sendState();
    } else {
      Serial.print(key);
      Serial.println(" already disabled");
    }
    request->send(204);
  } else {
    request->send(400);
  }
}

void handleSysInfo(AsyncWebServerRequest* request) {
  String result;

  result += "{";
  result += "\"Chip Model\":\"" + String(ESP.getChipModel()) + "\",";
  result += "\"Chip Cores\":\"" + String(ESP.getChipCores()) + "\",";
  result += "\"Chip Revision\":\"" + String(ESP.getChipRevision()) + "\",";
  result += "\"flashSize\":\"" + String(ESP.getFlashChipSize()) + "\",";
  result += "\"freeHeap\":\"" + String(ESP.getFreeHeap()) + "\",";
  result += "\"uptime\":";
  result += millis();
  result += ",";
  result += "\"time\":" + String(time(nullptr)) + "";
  result += "}";

  AsyncWebServerResponse* response = request->beginResponse(200, "application/json; charset=utf-8", result);
  response->addHeader("Cache-Control", "no-cache");
  request->send(response);
}  // handleSysInfo()

/**
 * @brief Gets IANA timezone ID based on IP
 * Uses an IP geolocation service to find the IANA timezone ID (e.g., America/Chicago).
 * @return the IANA timezone ID corresponding the IP's location
 */
String getIanaTimeZoneId() {
  Serial.println("getting IANA time zone ID...");
  String result = "Etc/UTC";  // default UTC timezone ID

  // get timezone ID from https://ipwho.is
  WiFiClientSecure* client = new WiFiClientSecure;
  if (client) {
    client->setCACert(rootCACertificate);

    {
      // Add a scoping block for HTTPClient https to make sure it is destroyed before WiFiClientSecure *client is
      HTTPClient https;

      Serial.print("[HTTPS] begin...\n");
      if (https.begin(*client, "https://ipwho.is")) {  // HTTPS
        Serial.print("[HTTPS] GET...\n");
        // start connection and send HTTP header
        int httpCode = https.GET();

        // httpCode will be negative on error
        if (httpCode > 0) {
          // HTTP header has been send and Server response header has been handled
          Serial.printf("[HTTPS] GET... code: %d\n", httpCode);

          // file found at server
          if (httpCode == HTTP_CODE_OK || httpCode == HTTP_CODE_MOVED_PERMANENTLY) {
            String payload = https.getString();
            Serial.println(payload);

            JsonDocument filter;
            filter["timezone"]["id"] = true;  // Keep the "id" field
            JsonDocument doc;
            DeserializationError error = deserializeJson(doc, payload, DeserializationOption::Filter(filter));
            if (error) {
              Serial.print("deserializeJson() failed: ");
              Serial.println(error.c_str());
            }
            result = doc["timezone"]["id"].as<String>();
          }
        } else {
          Serial.printf("[HTTPS] GET... failed, error: %s\n", https.errorToString(httpCode).c_str());
        }

        https.end();
      } else {
        Serial.printf("[HTTPS] Unable to connect\n");
      }
    }

    delete client;
  } else {
    Serial.println("Unable to create client");
  }
  Serial.print("IANA time zone ID: ");
  Serial.println(result);
  return result;
}

void wifiManagerConfigModeCallback(WiFiManager* wiFiManager) {
  ledsBuiltin[0] = CRGB::Blue;
  FastLED.show();
}

void setup() {
  Serial.begin(115200);

  LittleFS.begin();

  // initialize piezo buzzer output
  ledcAttachPin(TONE_OUTPUT_PIN, TONE_PWM_CHANNEL);

  bool avail = BH1750.begin(BH1750_TO_GROUND);
  if (!avail) {
    Serial.println("No BH1750 sensor found!");
  }
  BH1750.calibrateTiming();
  BH1750.start();

  // initialize and reset LEDs
  // delay(1); // leds sometimes not all resetting properly without this for some reason
  FastLED.addLeds<WS2812, PIN_HOUR, GRB>(ledsHour, NUM_LEDS_HOUR);
  FastLED.addLeds<WS2812, PIN_MINUTE, GRB>(ledsMinute, NUM_LEDS_MINUTE);
  FastLED.addLeds<WS2812, PIN_BUILTIN, GRB>(ledsBuiltin, 1);
  // FastLED.setBrightness(25); // set global brightness which overrides V
  fill_solid(ledsHour, NUM_LEDS_HOUR, CRGB::Black);
  fill_solid(ledsMinute, NUM_LEDS_MINUTE, CRGB::Black);
  ledsBuiltin[0] = CRGB::Red;
  FastLED.show();

  // Open Preferences
  prefs.begin(prefsNamespace, false);  // false = read/write
  uint8_t colorPref[3] = { 0, 0, 0 };
  size_t bytesRead = prefs.getBytes("color", colorPref, 3);
  if (bytesRead == 3) {
    toColor.hue = colorPref[0];
    toColor.saturation = colorPref[1];
    toColor.value = colorPref[2];
  } else {
    Serial.println("no color preference or error reading preference");
    // default light blue color
    toColor.hue = colorPref[128];
    toColor.saturation = colorPref[255];
    toColor.value = colorPref[128];
  }
  savedColor = toColor;
  blinkEnabled = prefs.getBool("blink", false);
  Serial.print("blink=");
  Serial.println(blinkEnabled ? "true" : "false");
  rainbowEnabled = prefs.getBool("rainbow", false);
  Serial.print("rainbow=");
  Serial.println(rainbowEnabled ? "true" : "false");
  twelveHour = prefs.getBool("twelveHour", false);
  Serial.print("twelveHour=");
  Serial.println(twelveHour ? "true" : "false");

  // match the fade in color's hue and saturation -- this is needed to avoid fading across colors if starting from 0,0,0
  fromColor.hue = toColor.hue;
  fromColor.saturation = toColor.saturation;
  fromColor.value = 0;

  //WiFiManager, Local intialization. Once its business is done, there is no need to keep it around
  WiFiManager wm;

  // reset settings - wipe stored credentials for testing
  // these are stored by the esp library
  // wm.resetSettings();

  // Automatically connect using saved credentials,
  // if connection fails, it starts an access point with the specified name ( "AutoConnectAP"),
  // if empty will auto generate SSID, if password is blank it will be anonymous AP (wm.autoConnect())
  // then goes into a blocking loop awaiting configuration and will return success result

  wm.setAPCallback(wifiManagerConfigModeCallback);

  char chipIdStr[10];
  uint64_t chipid = ESP.getEfuseMac();                                       // Get the 64-bit Chip ID from the ESP32 hardware
  snprintf(chipIdStr, sizeof(chipIdStr), "%08X", (uint32_t)(chipid >> 32));  // convert the lower 4 bytes to hex
  String customSSID = "7SClock-" + String(chipIdStr);                        // Results in "7SClock-1234ABCD
  // bool res = wm.autoConnect(customSSID.c_str()); // TODO restore when able to connect
  bool res = false;  // TODO remove when able to connect

  if (!res) {
    Serial.println("Failed to connect");
    // ESP.restart();

    // turn off built in LED
    ledsBuiltin[0] = CRGB::Black;
    FastLED.show();

    return;
  } else {
    //if you get here you have connected to the WiFi
    Serial.println("connected...yeey :)");

    ledsBuiltin[0] = CRGB::Green;
    FastLED.show();
  }

  // Initialize mDNS
  if (!MDNS.begin("7sclock")) {
    Serial.println("Error setting up MDNS");
  } else {
    Serial.println("mDNS responder started: 7sclock.local");
    // Advertise HTTP service on port 80
    MDNS.addService("http", "tcp", 80);
  }

  String timeZoneId;
  if (prefs.isKey("overrideTZ")) {
    timeZoneId = prefs.getString("overrideTZ");
    Serial.print("overrideTZ=");
    Serial.println(timeZoneId);
    timeZoneState = timeZoneId;
  } else {
    Serial.println("overrideTZ not set");
    timeZoneId = getIanaTimeZoneId();
    timeZoneState = "";
  }
  const char* posixTz = TzDbLookup::getPosix(timeZoneId.c_str());
  if (!posixTz) {
    posixTz = "UTC0";
  }

  prefs.end();

  Serial.print("Synchronize NTP using tz ");
  Serial.println(posixTz);
  // after successful sync, time(nullptr) will be the number of seconds since January 1, 1970, 00:00:00
  // once this is set up, syncing occurs every hour by default (see https://forum.arduino.cc/t/esp32-time-library-sync-interval/902588)
  // daylight savings time is automatically handled according to the POSIX time zone specification
  configTzTime(posixTz, "pool.ntp.org", "time.aws.com", "time.google.com");

  // wait for first successful sync before showing anything
  while (time(nullptr) < 1700000000) {  // 1700000000 == November 14, 2023
    delay(100);
    Serial.print(".");
  }
  Serial.println(" OK");
  Serial.println(time(nullptr));
  printTime();

  // curl "http://{{IPADDRESS}}/api/sysinfo"
  server.on("/api/sysinfo", HTTP_GET, handleSysInfo);
  // curl -X PUT "http://{{IPADDRESS}}/api/color?h=128&s=255&v=128"
  server.on("/api/color", HTTP_PUT, [](AsyncWebServerRequest* request) {
    String h = request->getParam("h")->value();  // 0-255
    String s = request->getParam("s")->value();  // 0-255
    String v = request->getParam("v")->value();  // 0-255
    Serial.print("PUT ");
    Serial.print(request->url());
    Serial.print("?h=");
    Serial.print(h);
    Serial.print("&s=");
    Serial.print(s);
    Serial.print("&v=");
    Serial.println(v);
    // TODO validation
    setColor(h.toInt(), s.toInt(), v.toInt());
    request->send(204);
  });
  // curl -X PUT "http://{{IPADDRESS}}/api/rainbow?enabled=true"
  server.on("/api/rainbow", HTTP_PUT, [](AsyncWebServerRequest* request) {
    handleBoolPreference(
      request, "rainbow", rainbowEnabled, []() {
        // post-enable callback logic: this is needed because when we first start rainbow mode, we want the hue offset to be 0 so that it starts transitioning from the static color
        rainbowHueOffset = 0;
      },
      []() {
        // post-disable callback logic: this is needed because there is nothing that reverts the currentColor back when coming out of rainbow mode
        if (!fading) {
          currentColor = toColor;
        }
      });
  });
  // curl -X PUT "http://{{IPADDRESS}}/api/blink?enabled=true"
  server.on("/api/blink", HTTP_PUT, [](AsyncWebServerRequest* request) {
    handleBoolPreference(request, "blink", blinkEnabled);
  });
  // curl -X PUT "http://{{IPADDRESS}}/api/twelveHour?enabled=true"
  server.on("/api/twelveHour", HTTP_PUT, [](AsyncWebServerRequest* request) {
    handleBoolPreference(request, "twelveHour", twelveHour);
  });
  // curl -X PUT "http://{{IPADDRESS}}/api/overrideTimeZone?value=America/Chicago"
  server.on("/api/overrideTimeZone", HTTP_PUT, [](AsyncWebServerRequest* request) {
    String overrideTZ = request->getParam("value")->value();

    const char* posixTz = TzDbLookup::getPosix(overrideTZ.c_str());
    if (!posixTz) {
      Serial.println("invalid/unknown time zone ID");
      request->send(400);
      return;
    }

    timeZoneState = overrideTZ;
    sendState();

    // update time zone info
    Serial.print("Synchronize NTP using tz ");
    Serial.println(posixTz);
    setenv("TZ", posixTz, 1);  // 1 is the overwrite flag, ensuring previous settings are replaced
    tzset();                   // apply the change
    printTime();

    // save preference
    Serial.print("saving String overrideTZ=");
    Serial.println(overrideTZ);
    prefs.begin(prefsNamespace, false);  // open in read/write mode
    prefs.putString("overrideTZ", overrideTZ);
    prefs.end();

    request->send(204);
  });
  // curl -X DELETE "http://{{IPADDRESS}}/api/overrideTimeZone"
  server.on("/api/overrideTimeZone", HTTP_DELETE, [](AsyncWebServerRequest* request) {
    // remove preference
    prefs.begin(prefsNamespace, false);  // open in read/write mode
    if (prefs.isKey("overrideTZ")) {
      Serial.print("removing String overrideTZ");
      prefs.remove("overrideTZ");
      prefs.end();
    } else {
      Serial.println("overrideTZ not found--nothing to do");
      prefs.end();
      request->send(204);
      return;
    }

    timeZoneState = "";
    sendState();

    // attempt to get time zone ID from IP
    String timeZoneId = getIanaTimeZoneId();
    const char* posixTz = TzDbLookup::getPosix(timeZoneId.c_str());
    if (!posixTz) {
      posixTz = "UTC0";
    }

    // update time zone info
    Serial.print("Synchronize NTP using tz ");
    Serial.println(posixTz);
    setenv("TZ", posixTz, 1);  // 1 is the overwrite flag, ensuring previous settings are replaced
    tzset();                   // apply the change
    printTime();

    request->send(204);
  });
  // static file handler
  server.serveStatic("/", LittleFS, "/").setDefaultFile("index.html");

  // SSE (Server-Sent Events)
  events.onConnect([](AsyncEventSourceClient* client) {
    Serial.println("SSE Client connected!");
    client->send(getState(), "state", millis(), 1000);
  });
  events.onDisconnect([](AsyncEventSourceClient* client) {
    Serial.println("SSE Client disconnected!");
  });
  server.addHandler(&events);

  server.begin();

  // turn off built in LED
  ledsBuiltin[0] = CRGB::Black;
  FastLED.show();
}

// returns local time information (hour, minute, second)
struct tm getTimeInfo() {
  struct tm timeInfo;
  if (!getLocalTime(&timeInfo, 0)) {  // by default this could block for up to 5 seconds--set to 0 to avoid waiting at all
    // fallback: get current time since epoch (will be 1970 if never synced)
    time_t now;
    time(&now);
    // Convert to local time structure
    localtime_r(&now, &timeInfo);
  }

  return timeInfo;
}

// prints formatted time
void printTime() {
  struct tm timeInfo = getTimeInfo();
  Serial.println(&timeInfo, "%H:%M:%S");
}

boolean first = true;
uint8_t octave = 0;
void loop() {
  EVERY_N_BSECONDS(1) {
    // can only handle octave 2 to 8
    // for octave 0 or 1, get this error: "ledc: requested frequency and duty resolution can not be achieved, try reducing freq_hz or duty_resolution."
    octave = max((octave + 1) % 9, 2);
    // Serial.print("octave:");
    // Serial.println(octave);
    // ledcWriteNote(TONE_PWM_CHANNEL, NOTE_C, octave);
    ledcWriteTone(TONE_PWM_CHANNEL, 0);
    ledcDetachPin(TONE_OUTPUT_PIN);  // use ledcDetach() for Core 3.x
  }

  if (first) {  // super dramatic fade in effect
    fading = true;
    first = false;
  }
  updateColor();
  updateRainbow();
  updateLeds();
  saveColorChange();

  // periodically print the time (useful for debugging)
  EVERY_N_BSECONDS(30) {
    printTime();
  }

  if (BH1750.hasValue()) {
    float lux = BH1750.getLux();
    // Serial.print("lux:");
    // Serial.println(lux);
    BH1750.start();
  }

  // Server-Sent Events (SSE) heartbeat
  EVERY_N_BSECONDS(15) {
    uint32_t now = millis();
    events.send(String("💓 ") + now, "heartbeat", now);
  }
}

// saves color to preferences, if necessary
void saveColorChange() {
  // throttle color change induced saving to avoid unnecessary write/erase cycles
  if (toColor != savedColor && ((millis() - lastColorChangeTime) > colorSaveInterval)) {
    savedColor = toColor;

    Serial.println("saving color change");
    prefs.begin(prefsNamespace, false);  // open in read/write mode
    uint8_t colorPref[3] = { savedColor.hue, savedColor.saturation, savedColor.value };
    prefs.putBytes("color", colorPref, 3);
    prefs.end();
  }
}

// sets the current color on the appropriate LEDs, based on the current time
void updateLeds() {
  // set everything to black
  fill_solid(ledsHourHsv, NUM_LEDS_HOUR, CHSV(0, 0, 0));
  fill_solid(ledsMinuteHsv, NUM_LEDS_MINUTE, CHSV(0, 0, 0));

  // set colons to currentColor (if not off due to blink)
  struct timeval tv;
  gettimeofday(&tv, NULL);  // get UTC time info with microsecond precision -- we can't use millis() here because that is based on uptime rather than the NTP synced time
  if ((tv.tv_usec / 1000) % 1000 < 500 || !blinkEnabled) {
    // blink: colons are lit for the first half of the second
    ledsHourHsv[0] = currentColor;
    ledsMinuteHsv[0] = currentColor;
    ledsMinuteHsv[(14 * NUM_LEDS_PER_SEGMENT) + 1] = currentColor;
    ledsMinuteHsv[(14 * NUM_LEDS_PER_SEGMENT) + 2] = currentColor;
  }

  // get current time
  struct tm timeInfo = getTimeInfo();

  // set hour digits to currentColor
  int hour = timeInfo.tm_hour;
  if (twelveHour) {
    if (hour > 12) {
      hour -= 12;
    } else if (hour == 0) {
      hour = 12;
    }
  }
  if (hour >= 10 || !twelveHour) {
    displayHourDigit(7 * NUM_LEDS_PER_SEGMENT + 1, hour / 10);  // first digit
  }
  displayHourDigit(1, hour % 10);  // second digit

  // set minute digits to currentColor
  int minute = timeInfo.tm_min;
  displayMinuteDigit(1, minute / 10);                             // first digit
  displayMinuteDigit(7 * NUM_LEDS_PER_SEGMENT + 1, minute % 10);  // second digit

  // set second digits to currentColor
  int second = timeInfo.tm_sec;
  displayMinuteDigit((14 * NUM_LEDS_PER_SEGMENT) + 3, second / 10);  // first digit
  displayMinuteDigit((21 * NUM_LEDS_PER_SEGMENT) + 3, second % 10);  // second digit

  // vertical rainbow: shift hue by row
  bool verticalRainbow = true;  // TODO make a preference
  if (verticalRainbow) {
    int hueShift = 0;

    // row -2
    hueShift = -102;  // 255/5*-2
    shiftHueSegment(ledsHourHsv, 1, 1, hueShift);
    shiftHueSegment(ledsHourHsv, 1, 8, hueShift);
    shiftHueSegment(ledsMinuteHsv, 1, 5, hueShift);
    shiftHueSegment(ledsMinuteHsv, 1, 12, hueShift);
    shiftHueSegment(ledsMinuteHsv, 3, 19, hueShift);
    shiftHueSegment(ledsMinuteHsv, 3, 26, hueShift);

    // row -1
    hueShift = -51;  // 255/5*-1
    shiftHueSegment(ledsHourHsv, 1, 0, hueShift);
    shiftHueSegment(ledsHourHsv, 1, 2, hueShift);
    shiftHueSegment(ledsHourHsv, 1, 7, hueShift);
    shiftHueSegment(ledsHourHsv, 1, 9, hueShift);
    shiftHueSingleLed(ledsMinuteHsv, 0, hueShift);  // top colon between HH:MM
    shiftHueSegment(ledsMinuteHsv, 1, 4, hueShift);
    shiftHueSegment(ledsMinuteHsv, 1, 6, hueShift);
    shiftHueSegment(ledsMinuteHsv, 1, 11, hueShift);
    shiftHueSegment(ledsMinuteHsv, 1, 13, hueShift);
    shiftHueSingleLed(ledsMinuteHsv, 14 * NUM_LEDS_PER_SEGMENT + 2, hueShift);  // top colon between MM:SS
    shiftHueSegment(ledsMinuteHsv, 3, 18, hueShift);
    shiftHueSegment(ledsMinuteHsv, 3, 20, hueShift);
    shiftHueSegment(ledsMinuteHsv, 3, 25, hueShift);
    shiftHueSegment(ledsMinuteHsv, 3, 27, hueShift);

    // row +1
    hueShift = 51;                                // 255/5*1
    shiftHueSingleLed(ledsHourHsv, 0, hueShift);  // bottom colon between HH:MM
    shiftHueSegment(ledsHourHsv, 1, 4, hueShift);
    shiftHueSegment(ledsHourHsv, 1, 6, hueShift);
    shiftHueSegment(ledsHourHsv, 1, 11, hueShift);
    shiftHueSegment(ledsHourHsv, 1, 13, hueShift);
    shiftHueSegment(ledsMinuteHsv, 1, 0, hueShift);
    shiftHueSegment(ledsMinuteHsv, 1, 2, hueShift);
    shiftHueSegment(ledsMinuteHsv, 1, 7, hueShift);
    shiftHueSegment(ledsMinuteHsv, 1, 9, hueShift);
    shiftHueSingleLed(ledsMinuteHsv, 14 * NUM_LEDS_PER_SEGMENT + 1, hueShift);  // bottom colon between MM:SS
    shiftHueSegment(ledsMinuteHsv, 3, 14, hueShift);
    shiftHueSegment(ledsMinuteHsv, 3, 16, hueShift);
    shiftHueSegment(ledsMinuteHsv, 3, 21, hueShift);
    shiftHueSegment(ledsMinuteHsv, 3, 23, hueShift);

    // row +2
    hueShift = 102;  // 255/5*2
    shiftHueSegment(ledsHourHsv, 1, 5, hueShift);
    shiftHueSegment(ledsHourHsv, 1, 12, hueShift);
    shiftHueSegment(ledsMinuteHsv, 1, 1, hueShift);
    shiftHueSegment(ledsMinuteHsv, 1, 8, hueShift);
    shiftHueSegment(ledsMinuteHsv, 3, 15, hueShift);
    shiftHueSegment(ledsMinuteHsv, 3, 22, hueShift);
  }

  // horizontal rainbow: shift hue by column
  bool horizontalRainbow = true;  // TODO make a preference
  if (horizontalRainbow) {
    int hueShift = 0;

    // column -6
    hueShift = -118;  // 255/13*-6;
    shiftHueSegment(ledsHourHsv, 1, 9, hueShift);
    shiftHueSegment(ledsHourHsv, 1, 13, hueShift);

    // column -5
    hueShift = -98;  // 255/13*-5
    shiftHueSegment(ledsHourHsv, 1, 8, hueShift);
    shiftHueSegment(ledsHourHsv, 1, 10, hueShift);
    shiftHueSegment(ledsHourHsv, 1, 12, hueShift);

    // column -4
    hueShift = -78;  // 255/13*-4
    shiftHueSegment(ledsHourHsv, 1, 7, hueShift);
    shiftHueSegment(ledsHourHsv, 1, 11, hueShift);

    // column -3
    hueShift = -59;  // 255/13*-3
    shiftHueSegment(ledsHourHsv, 1, 2, hueShift);
    shiftHueSegment(ledsHourHsv, 1, 6, hueShift);

    // column -2
    hueShift = -39;  // 255/13*-2
    shiftHueSegment(ledsHourHsv, 1, 1, hueShift);
    shiftHueSegment(ledsHourHsv, 1, 3, hueShift);
    shiftHueSegment(ledsHourHsv, 1, 5, hueShift);

    // column -1
    hueShift = -20;  // 255/13*-1
    shiftHueSegment(ledsHourHsv, 1, 0, hueShift);
    shiftHueSegment(ledsHourHsv, 1, 4, hueShift);

    // column +1
    hueShift = 20;  // 255/13*1
    shiftHueSegment(ledsMinuteHsv, 1, 0, hueShift);
    shiftHueSegment(ledsMinuteHsv, 1, 4, hueShift);

    // column +2
    hueShift = 39;  // 255/13*2
    shiftHueSegment(ledsMinuteHsv, 1, 1, hueShift);
    shiftHueSegment(ledsMinuteHsv, 1, 3, hueShift);
    shiftHueSegment(ledsMinuteHsv, 1, 5, hueShift);

    // column +3
    hueShift = 59;  // 255/13*3
    shiftHueSegment(ledsMinuteHsv, 1, 2, hueShift);
    shiftHueSegment(ledsMinuteHsv, 1, 6, hueShift);

    // column +4
    hueShift = 78;  // 255/13*4
    shiftHueSegment(ledsMinuteHsv, 1, 7, hueShift);
    shiftHueSegment(ledsMinuteHsv, 1, 11, hueShift);

    // column +5
    hueShift = 98;  // 255/13*5
    shiftHueSegment(ledsMinuteHsv, 1, 8, hueShift);
    shiftHueSegment(ledsMinuteHsv, 1, 10, hueShift);
    shiftHueSegment(ledsMinuteHsv, 1, 12, hueShift);

    // column +6
    hueShift = 118;  // 255/13*6
    shiftHueSegment(ledsMinuteHsv, 1, 9, hueShift);
    shiftHueSegment(ledsMinuteHsv, 1, 13, hueShift);

    // column +7
    hueShift = 137;                                                             // 255/13*7
    shiftHueSingleLed(ledsMinuteHsv, 14 * NUM_LEDS_PER_SEGMENT + 1, hueShift);  // bottom colon between MM:SS
    shiftHueSingleLed(ledsMinuteHsv, 14 * NUM_LEDS_PER_SEGMENT + 2, hueShift);  // bottom colon between MM:SS

    // column +8
    hueShift = 157;  // 255/13*8
    shiftHueSegment(ledsMinuteHsv, 3, 14, hueShift);
    shiftHueSegment(ledsMinuteHsv, 3, 18, hueShift);

    // column +9
    hueShift = 177;  // 255/13*9
    shiftHueSegment(ledsMinuteHsv, 3, 15, hueShift);
    shiftHueSegment(ledsMinuteHsv, 3, 17, hueShift);
    shiftHueSegment(ledsMinuteHsv, 3, 19, hueShift);

    // column +10
    hueShift = 196;  // 255/13*10
    shiftHueSegment(ledsMinuteHsv, 3, 16, hueShift);
    shiftHueSegment(ledsMinuteHsv, 3, 20, hueShift);

    // column +11
    hueShift = 216;  // 255/13*11
    shiftHueSegment(ledsMinuteHsv, 3, 21, hueShift);
    shiftHueSegment(ledsMinuteHsv, 3, 25, hueShift);

    // column +12
    hueShift = 235;  // 255/13*12
    shiftHueSegment(ledsMinuteHsv, 3, 22, hueShift);
    shiftHueSegment(ledsMinuteHsv, 3, 24, hueShift);
    shiftHueSegment(ledsMinuteHsv, 3, 26, hueShift);
  }

  // Sync the HSV and RGB arrays (converts HSV to RGB)
  for (int i = 0; i < NUM_LEDS_HOUR; i++) {
    ledsHour[i] = ledsHourHsv[i];
  }
  for (int i = 0; i < NUM_LEDS_MINUTE; i++) {
    ledsMinute[i] = ledsMinuteHsv[i];
  }

  FastLED.show();
}

void shiftHueSingleLed(struct CHSV* leds, int index, int hueShift) {
  leds[index].hue += hueShift;
}

void shiftHueSegment(struct CHSV* leds, int offset, int segmentIndex, int hueShift) {
  int startIndex = offset + segmentIndex * NUM_LEDS_PER_SEGMENT;
  for (int i = startIndex; i < startIndex + NUM_LEDS_PER_SEGMENT; i++) {  // for each LED in the segment
    shiftHueSingleLed(leds, i, hueShift);
  }
}

void fillSegment(struct CHSV* leds, int offset, int segmentIndex) {
  fill_solid(leds + offset + segmentIndex * NUM_LEDS_PER_SEGMENT, NUM_LEDS_PER_SEGMENT, currentColor);
}

/**
   sets the proper LEDs to the current color to display a digit on the hour (left) side of the controller, assuming that all LEDs for the digit are already set to black
*/
void displayHourDigit(int offset, int digit) {
  switch (digit) {
    case 0:
      fillSegment(ledsHourHsv, offset, 0);
      fillSegment(ledsHourHsv, offset, 1);
      fillSegment(ledsHourHsv, offset, 2);
      fillSegment(ledsHourHsv, offset, 4);
      fillSegment(ledsHourHsv, offset, 5);
      fillSegment(ledsHourHsv, offset, 6);
      break;
    case 1:
      fillSegment(ledsHourHsv, offset, 0);
      fillSegment(ledsHourHsv, offset, 4);
      break;
    case 2:
      fillSegment(ledsHourHsv, offset, 0);
      fillSegment(ledsHourHsv, offset, 1);
      fillSegment(ledsHourHsv, offset, 3);
      fillSegment(ledsHourHsv, offset, 5);
      fillSegment(ledsHourHsv, offset, 6);
      break;
    case 3:
      fillSegment(ledsHourHsv, offset, 0);
      fillSegment(ledsHourHsv, offset, 1);
      fillSegment(ledsHourHsv, offset, 3);
      fillSegment(ledsHourHsv, offset, 4);
      fillSegment(ledsHourHsv, offset, 5);
      break;
    case 4:
      fillSegment(ledsHourHsv, offset, 0);
      fillSegment(ledsHourHsv, offset, 2);
      fillSegment(ledsHourHsv, offset, 3);
      fillSegment(ledsHourHsv, offset, 4);
      break;
    case 5:
      fillSegment(ledsHourHsv, offset, 1);
      fillSegment(ledsHourHsv, offset, 2);
      fillSegment(ledsHourHsv, offset, 3);
      fillSegment(ledsHourHsv, offset, 4);
      fillSegment(ledsHourHsv, offset, 5);
      break;
    case 6:
      fillSegment(ledsHourHsv, offset, 1);
      fillSegment(ledsHourHsv, offset, 2);
      fillSegment(ledsHourHsv, offset, 3);
      fillSegment(ledsHourHsv, offset, 4);
      fillSegment(ledsHourHsv, offset, 5);
      fillSegment(ledsHourHsv, offset, 6);
      break;
    case 7:
      fillSegment(ledsHourHsv, offset, 0);
      fillSegment(ledsHourHsv, offset, 1);
      fillSegment(ledsHourHsv, offset, 4);
      break;
    case 8:
      fillSegment(ledsHourHsv, offset, 0);
      fillSegment(ledsHourHsv, offset, 1);
      fillSegment(ledsHourHsv, offset, 2);
      fillSegment(ledsHourHsv, offset, 3);
      fillSegment(ledsHourHsv, offset, 4);
      fillSegment(ledsHourHsv, offset, 5);
      fillSegment(ledsHourHsv, offset, 6);
      break;
    case 9:
      fillSegment(ledsHourHsv, offset, 0);
      fillSegment(ledsHourHsv, offset, 1);
      fillSegment(ledsHourHsv, offset, 2);
      fillSegment(ledsHourHsv, offset, 3);
      fillSegment(ledsHourHsv, offset, 4);
      fillSegment(ledsHourHsv, offset, 5);
      break;
  }
}

/**
   sets the proper LEDs to the current color to display a digit on the minute (right) side of the controller, assuming that all LEDs for the digit are already set to black
*/
void displayMinuteDigit(int offset, int digit) {
  switch (digit) {
    case 0:
      fillSegment(ledsMinuteHsv, offset, 0);
      fillSegment(ledsMinuteHsv, offset, 1);
      fillSegment(ledsMinuteHsv, offset, 2);
      fillSegment(ledsMinuteHsv, offset, 4);
      fillSegment(ledsMinuteHsv, offset, 5);
      fillSegment(ledsMinuteHsv, offset, 6);
      break;
    case 1:
      fillSegment(ledsMinuteHsv, offset, 2);
      fillSegment(ledsMinuteHsv, offset, 6);
      break;
    case 2:
      fillSegment(ledsMinuteHsv, offset, 0);
      fillSegment(ledsMinuteHsv, offset, 1);
      fillSegment(ledsMinuteHsv, offset, 3);
      fillSegment(ledsMinuteHsv, offset, 5);
      fillSegment(ledsMinuteHsv, offset, 6);
      break;
    case 3:
      fillSegment(ledsMinuteHsv, offset, 1);
      fillSegment(ledsMinuteHsv, offset, 2);
      fillSegment(ledsMinuteHsv, offset, 3);
      fillSegment(ledsMinuteHsv, offset, 5);
      fillSegment(ledsMinuteHsv, offset, 6);
      break;
    case 4:
      fillSegment(ledsMinuteHsv, offset, 2);
      fillSegment(ledsMinuteHsv, offset, 3);
      fillSegment(ledsMinuteHsv, offset, 4);
      fillSegment(ledsMinuteHsv, offset, 6);
      break;
    case 5:
      fillSegment(ledsMinuteHsv, offset, 1);
      fillSegment(ledsMinuteHsv, offset, 2);
      fillSegment(ledsMinuteHsv, offset, 3);
      fillSegment(ledsMinuteHsv, offset, 4);
      fillSegment(ledsMinuteHsv, offset, 5);
      break;
    case 6:
      fillSegment(ledsMinuteHsv, offset, 0);
      fillSegment(ledsMinuteHsv, offset, 1);
      fillSegment(ledsMinuteHsv, offset, 2);
      fillSegment(ledsMinuteHsv, offset, 3);
      fillSegment(ledsMinuteHsv, offset, 4);
      fillSegment(ledsMinuteHsv, offset, 5);
      break;
    case 7:
      fillSegment(ledsMinuteHsv, offset, 2);
      fillSegment(ledsMinuteHsv, offset, 5);
      fillSegment(ledsMinuteHsv, offset, 6);
      break;
    case 8:
      fillSegment(ledsMinuteHsv, offset, 0);
      fillSegment(ledsMinuteHsv, offset, 1);
      fillSegment(ledsMinuteHsv, offset, 2);
      fillSegment(ledsMinuteHsv, offset, 3);
      fillSegment(ledsMinuteHsv, offset, 4);
      fillSegment(ledsMinuteHsv, offset, 5);
      fillSegment(ledsMinuteHsv, offset, 6);
      break;
    case 9:
      fillSegment(ledsMinuteHsv, offset, 1);
      fillSegment(ledsMinuteHsv, offset, 2);
      fillSegment(ledsMinuteHsv, offset, 3);
      fillSegment(ledsMinuteHsv, offset, 4);
      fillSegment(ledsMinuteHsv, offset, 5);
      fillSegment(ledsMinuteHsv, offset, 6);
      break;
  }
}

// sets the target color
void setColor(uint8_t hue, uint8_t saturation, uint8_t value) {
  if (toColor.hue == hue && toColor.saturation == saturation && toColor.value == value) {
    Serial.println("setColor called with the same color--nothing to do");
    return;
  }

  lastColorChangeTime = millis();
  Serial.print("setting color to CHSV(");
  Serial.print(hue);
  Serial.print(", ");
  Serial.print(saturation);
  Serial.print(", ");
  Serial.print(value);
  Serial.println(")");
  fromColor = currentColor;
  toColor.hue = hue;
  toColor.saturation = saturation;
  toColor.value = value;
  lerp = 0;
  fading = true;

  sendState();
}

// sets the current color based on fade, rainbow, etc.
void updateColor() {
  if (fading) {
    if (lerp < 255) {
      if (rainbowEnabled) {
        currentColor = blend(fromColor, CHSV(toColor.hue + rainbowHueOffset, toColor.saturation, toColor.value), ++lerp);
      } else {
        currentColor = blend(fromColor, toColor, ++lerp);
      }
    } else {
      Serial.println("done fading");
      fading = false;
    }
  } else if (rainbowEnabled) {
    currentColor.hue = toColor.hue + rainbowHueOffset;
  }
}

void updateRainbow() {
  if (rainbowEnabled) {
    // this controls the speed of color change; at 100ms, it takes 25.5 seconds to cycle through all hue values
    EVERY_N_MILLIS(100) {
      rainbowHueOffset++;
    }
  }
}

void saveBoolPreference(const char* key, bool value) {
  Serial.print("saving bool ");
  Serial.print(key);
  Serial.print("=");
  Serial.println(value ? "true" : "false");

  prefs.begin(prefsNamespace, false);  // open in read/write mode
  prefs.putBool(key, value);
  prefs.end();
}

/**
 * @return A JSON String representation of the current state.
 */
String getState() {
  String stateJson;
  stateJson += "{\"color\":{\"h\":";
  stateJson += toColor.hue;
  stateJson += ",\"s\":";
  stateJson += toColor.saturation;
  stateJson += ",\"v\":";
  stateJson += toColor.value;
  stateJson += "},\"rainbow\":";
  stateJson += rainbowEnabled ? "true" : "false";
  stateJson += ",\"blink\":";
  stateJson += blinkEnabled ? "true" : "false";
  stateJson += ",\"twelveHour\":";
  stateJson += twelveHour ? "true" : "false";
  stateJson += ",\"timeZone\":";
  if (timeZoneState == "") {
    stateJson += "null";
  } else {
    stateJson += "\"";
    stateJson += timeZoneState;
    stateJson += "\"";
  }
  stateJson += "}";
  return stateJson;
}

/**
 * @brief Notifies connected clients of the current state after a change.
 */
void sendState() {
  events.send(getState(), "state", millis());
}