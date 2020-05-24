/************************************************************************************************************************
   LED-IOT-APP v1.2
   Communicates with a rest api server to get LED control parameters in order to control attached LEDs.
   Also updates its state (e.g., "heartbeat") to rest API.
 ********************************************************************************************************************/

#include "Arduino_DebugUtils.h"
#include <FastLED.h>
#include "credentials.h" // wifi network credentials and authorization token stored in separate file
#include "sha1.h"

// if using ESP32
#include <WiFiMulti.h>
#include <WiFi.h>
#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <ArduinoOTA.h>
WiFiMulti wifiMulti;
HTTPClient http;
#include <WiFiClientSecure.h>

// if using OLED display
#include "SSD1306Wire.h"         // legacy include: `#include "SSD1306.h"` //OLED screen
SSD1306Wire display(0x3c, 5, 4); //wifi bluetooth battery oled 18650 board dispplay

#include <SimpleHOTP.h>

// if using ESP8266....
//#include <ESP8266WiFi.h>
//#include <ESP8266WiFiMulti.h>
//#include <ESP8266HTTPClient.h>
//ESP8266WiFiMulti wifiMulti;
//#include <ArduinoHttpClient.h>

#define NETWORK_VANNEST
#define HTTPS

// #ifdef NETWORK_CXE
// String IPaddress = "192.168.2.54";
// #endif

// #ifdef NETWORK_VANNEST
// on vannet
String IPaddress = "api.ashleynewton.net";
// String IPaddress = "10.0.0.59";
//String apiURL = "http://10.0.0.59:5000/devices/";
//String controllerURL = "10.0.0.59/site/index.html";
// #endif

bool SERVER_SECURE = true; // global variable to indicate whether HTTPS or HTTP is used
String apiURL = IPaddress + "/devices/";
String controllerURL = IPaddress + "site/index.html";

String DEVICE_ID;

//RMT is an ESP hardware feature that offloads stuff like PWM and led strip protocol, it's rad
//#define FASTLED_RMT_CORE_DRIVER true
#define FASTLED_RMT_MAX_CHANNELS 1
FASTLED_USING_NAMESPACE

//#define LED_TYPE    WS2812B
#define LED_TYPE WS2811

//#ifdef DEV_LIGHTCONTROL_TRIANGLE
#define DATA_PIN 33
String hostname = "Trianglez";
#define COLOR_ORDER GRB //pixels
#define COLOR_CORRECT TypicalLEDStrip
#define NUM_LEDS 200
#define MILLI_AMPS 1000
#define BRIGHTNESS 100
#define FRAMES_PER_SECOND 120
//#endif

CRGB leds[NUM_LEDS];

uint8_t speed = 10;

uint8_t gHue = 0; // rotating "base color" used by many of the patterns

boolean connectioWasAlive = true;
String lastState = "false";
String onState = "false";
int brightVal = BRIGHTNESS;

#define NUM_SECONDS_TO_WAIT 5

/****************************************************************************************
   setupAOTA: Set up wireless updating if using ESP32
 ***************************************************************************************/

void setupAOTA()
{

  ArduinoOTA.setHostname(hostname.c_str());

  ArduinoOTA
      .onStart([]() {
        String type;
        if (ArduinoOTA.getCommand() == U_FLASH)
          type = "sketch";
        else // U_SPIFFS
          type = "filesystem";

        // NOTE: if updating SPIFFS this would be the place to unmount SPIFFS using SPIFFS.end()
        Serial.println("Start updating " + type);
      })
      .onEnd([]() {
        Serial.println("\nEnd");
      })
      .onProgress([](unsigned int progress, unsigned int total) {
        Serial.printf("Progress: %u%%\r", (progress / (total / 100)));
      })
      .onError([](ota_error_t error) {
        Serial.printf("Error[%u]: ", error);
        if (error == OTA_AUTH_ERROR)
          Serial.println("Auth Failed");
        else if (error == OTA_BEGIN_ERROR)
          Serial.println("Begin Failed");
        else if (error == OTA_CONNECT_ERROR)
          Serial.println("Connect Failed");
        else if (error == OTA_RECEIVE_ERROR)
          Serial.println("Receive Failed");
        else if (error == OTA_END_ERROR)
          Serial.println("End Failed");
      });

  ArduinoOTA.begin();
}

/* ****************************************************************************************
    setDeviceID: Generates a unique device ID based on performing SHA1 hashing of device's
                 MAC address.Returns this ID as a string variable.
 *****************************************************************************************/
String setDeviceID()
{
  byte mac[6] = {};

  //      String test = "test";
  //      int ml = test.length();
  WiFi.macAddress(mac);
  Serial.print("MAC ADDRESS: ");
  for (int i = 0; i < 5; i++)
  {
    Serial.print(mac[i], HEX);
    Serial.print(":");
  }
  Serial.println();
  int ml = sizeof(mac);
  char macArray[ml + 1];
  //  test.getBytes(macArray, ml + 1);
  //  mac.toCharArray(macArray, 6);

  uint32_t hash[5] = {}; // This will contain the 160-bit Hash
  SimpleSHA1::generateSHA(mac, (ml * 8), hash);
  //    Serial.println(hash);
  char hashChar[sizeof(hash) * 8 + 1];
  String device_hash = "device_";

  for (int i = 0; i < 5; i++)
  {
    //        Serial.print(hash[i], HEX);

    sprintf(hashChar, "%X", hash[i]);
    device_hash += String(hashChar);
  }

  Serial.print("device_hash = ");
  Serial.println(device_hash);

  //      utoa((unsigned int)hash, hashChar, HEX);
  //      Serial.println();
  //      Serial.println(hashChar2);
  //      Serial.print("hashChar = ");
  //      Serial.println(hashChar);
  //      String testDev = String(hashChar);
  //      Serial.print("testDev = ");
  //      Serial.println(testDev);
  //      DEVICE_ID = "device_" + hashString;
  //      Serial.print("DEVICE_ID = ");
  //      Serial.println(DEVICE_ID);
  return device_hash;
}

// Not sure if WiFiClientSecure checks the validity date of the certificate.
// Setting clock just to be sure...
void setClock()
{
  configTime(0, 0, "pool.ntp.org", "time.nist.gov");

  Serial.print(F("Waiting for NTP time sync: "));
  time_t nowSecs = time(nullptr);

  while (nowSecs < 8 * 3600 * 2)
  {
    delay(500);
    Serial.print(F("."));
    yield();
    nowSecs = time(nullptr);
  }

  Serial.println();
  struct tm timeinfo;
  gmtime_r(&nowSecs, &timeinfo);
  Serial.print(F("Current time: "));
  Serial.print(asctime(&timeinfo));
}

/***************************************************************************************
  setup
***************************************************************************************/

void setup()
{
  Serial.begin(115200);
  Debug.timestampOn();
  Debug.setDebugLevel(DBG_DEBUG);
  Debug.print(DBG_DEBUG, "Debugger set to DEBUG");

#ifdef __AVR_ATmega32U4__ // Arduino AVR Leonardo

  while (!Serial)
  {
    ; // wait for serial port to connect. Needed for Leonardo only
  }

#else

  delay(500); // Wait a time

#endif

  //all this is for OLED status screen
  display.init();
  display.clear();
  display.drawString(0, 0, "probably booting");
  //display.flipScreenVertically();
  display.display();

  // set up LEDS
  FastLED.setDither(BINARY_DITHER);
  FastLED.setMaxPowerInVoltsAndMilliamps(5, MILLI_AMPS);
  FastLED.setBrightness(BRIGHTNESS);
  FastLED.addLeds<LED_TYPE, DATA_PIN, COLOR_ORDER>(leds, NUM_LEDS).setCorrection(COLOR_CORRECT);

  // setup WIFI
  WiFi.mode(WIFI_STA);
  wifiMulti.addAP(ssid1, password1);
  wifiMulti.addAP(ssid2, password2);

  // allow reuse (if server supports it)
  // http.setReuse(true);

  // wait for WiFi connection
  Serial.print("Waiting for WiFi to connect...");
  // while ((WiFiMulti.run() != WL_CONNECTED)) {
  //   Serial.print(".");
  // }

  if (wifiMulti.run() == WL_CONNECTED)
  {
    Serial.println("");
    //    Serial.println("WiFi connected");
    Serial.printf("WiFi connected to %s\n", WiFi.SSID().c_str());
    //    Serial.println(WiFi.localIP());
    Serial.print("IP address: ");
    Serial.println(WiFi.localIP());

    setClock();

    // turn leds to green if connected
    fill_solid(leds, NUM_LEDS, CRGB::Green);
    FastLED.show();
    delay(100);
    fill_solid(leds, NUM_LEDS, CRGB::Black);
    FastLED.show();

    // inititalize deviceID
    DEVICE_ID = setDeviceID();

    // check if server is secure
    checkSSL();
  }

  setupAOTA(); // set up arduino over-the-air updating

  updateDisplay(); // update OLED screen on device

  //    printlnA(F("*** Setup end"));
}

/****************************************************************************************
    monitorWiFi
****************************************************************************************/
void monitorWiFi()
{
  Debug.print(DBG_INFO, "Monitoring wifi...");

  if (wifiMulti.run() != WL_CONNECTED)
  {
    if (connectioWasAlive == true)
    {
      connectioWasAlive = false;
      Serial.print("Looking for WiFi ");
      // turn first 20 leds to red if not connected
      fill_solid(leds, 20, CRGB::Red);
      FastLED.show();
    }
    Serial.print(".");
    delay(100);
  }
  else if (connectioWasAlive == false)
  {
    connectioWasAlive = true;
    //    delay(500);
    Serial.printf(" connected to %s\n", WiFi.SSID().c_str());
    Serial.println(WiFi.localIP());
    updateDisplay();
  }
}

/***************************************************************************
   checkSSL: checks whether HTTPS or HTTP should be used with server and
            updates global variable bool SERVER_SECURE.
***************************************************************************/
void checkSSL()
{
  Debug.print(DBG_VERBOSE, "in checkSSL");

  WiFiClientSecure *client = new WiFiClientSecure;
  if (client)
  {
    // client->setCACert(rootCACertificate); // comment out this to skip verifying certificate -- less secure
    Debug.print(DBG_VERBOSE, "checkSSL: client created");

    {

      if (http.begin(*client, "https://" + apiURL + DEVICE_ID))
      {
        http.addHeader("Authorization", "Bearer " + auth_token);

        Debug.print(DBG_VERBOSE, "checkSSL: [HTTPS] GET...\n");

        int httpCode = http.GET();

        if (httpCode > 0)
        {
          String response = http.getString();                 //Get the response to the request
          Debug.print(DBG_VERBOSE, "checkSSL: %i", httpCode); //Print return code
          // Debug.print(DBG_INFO, "%s", response);         //Print request answer
          SERVER_SECURE = true;
          Debug.print(DBG_INFO, "checkSSL: ***SERVER_SECURE set to TRUE");
        }
        else
        {
          Debug.print(DBG_VERBOSE, "checkSSL: Insecure connection, updating bool");
          Serial.println(http.errorToString(httpCode).c_str());
          SERVER_SECURE = false;
          Debug.print(DBG_INFO, "checkSSL: ***SERVER_SECURE set to FALSE");
        }

        http.end(); //Free resources
      }
      else
      {
        Debug.print(DBG_INFO, "checkSSL: ***SERVER_SECURE set to FALSE");
      }
    }
    delete client;
  }
}

/*******************************************************
  Main Loop
*******************************************************/

void loop()
{

  monitorWiFi();

  ArduinoOTA.handle();

  // use HTTP or HTTPS depending on SERVER_SECURE state
  switch (SERVER_SECURE)
  {
  case true:
    Debug.print(DBG_INFO, "main: server is secure - using to https");
    secure_connection("https://");
    break;

  case false:
    Debug.print(DBG_INFO, "main: server is insecure - using to http");
    insecure_connection("http://");
    break;
  }

  //  Serial.println("Setting brightness");
  //  delay(500);
  FastLED.setBrightness(brightVal);

  updateLEDS();
  FastLED.show();
  // insert a delay to keep the framerate modest
  FastLED.delay(1000 / FRAMES_PER_SECOND);

  // checkSSL();
}

/***************************************************************************
   insecure_connection: sets heartbeat and gets device info using HTTP
***************************************************************************/
void insecure_connection(String protocol)
{
  HTTPClient http;

  Debug.print(DBG_INFO, "insecure connection!");

  ////// SET HEARTBEAT ////////////////
  Debug.print(DBG_INFO, "insecure: setting heartbeat");
  String HB_URL = protocol + apiURL + "hb/" + DEVICE_ID;
  Debug.print(DBG_DEBUG, "insecure: HB_URL is %s", HB_URL);

  if (http.begin(HB_URL))
  {
    http.addHeader("Content-Type", "application/json"); //Specify content-type header
    http.addHeader("Authorization", "Bearer " + auth_token);
    Debug.print(DBG_INFO, "insecure: [HTTP] about to POST");
    int httpResponseCode = http.POST("{}"); //Send the actual POST request

    if (httpResponseCode > 0)
    {
      String response = http.getString();                      //Get the response to the request
      Debug.print(DBG_INFO, "insecure: %i", httpResponseCode); //Print return code
      Debug.print(DBG_INFO, "insecure: %s", response);         //Print request answer
    }
    else
    {
      Debug.print(DBG_ERROR, "insecure: [HTTP] Error on sending POST: ");
      Debug.print(DBG_ERROR, "%s", http.errorToString(httpResponseCode).c_str());
      SERVER_SECURE = true;
    }

    http.end(); //Free resources
  }
  else
  {
    Debug.print(DBG_ERROR, "insecure: [HTTP] Unable to connect\n");
    SERVER_SECURE = true;
  }

  /////////////////////// get device parameters ///////////////////

  String get_URL = protocol + apiURL + DEVICE_ID;
  Debug.print(DBG_VERBOSE, "insecure: opening connection to %s", get_URL);

  if (http.begin(get_URL))
  {
    http.addHeader("Authorization", "Bearer " + auth_token);

    // Serial.print("[HTTPS] GET...\n");

    int httpCode = http.GET();

    // Check returning httpCode -- will be negative on error
    if (httpCode > 0)
    {
      Debug.print(DBG_VERBOSE, "insecure: [HTTP] GET... code: %d\n", httpCode);

      // file found at server
      if (httpCode == HTTP_CODE_OK)
      {
        Debug.print(DBG_INFO, "insecure: Found file at server: ");
        String payload = http.getString();
        Debug.print(DBG_INFO, "insecure: %s", payload.c_str());

        // parse payload

        const size_t capacity = JSON_ARRAY_SIZE(2) + JSON_OBJECT_SIZE(3) + 100;
        DynamicJsonDocument doc(capacity);

        //                    const char* json = "[\"device_266A08DBF47456428F703EEDF1E208B7117785DF\",{\"brightness\":\"123\",\"name\":\"triangle\",\"onState\":\"true\"}]";

        deserializeJson(doc, payload);

        const char *root_0 = doc[0]; // "device_266A08DBF47456428F703EEDF1E208B7117785DF"

        JsonObject root_1 = doc[1];
        const char *root_1_brightness = root_1["brightness"]; // "123"
        const char *root_1_name = root_1["name"];             // "triangle"
        const char *root_1_onState = root_1["onState"];       // "true"
        // Debug.print(DBG_DEBUG, "root_0 = ");
        // Debug.print(DBG_DEBUG, root_0);
        // Debug.print(DBG_DEBUG, "brightness = ");
        // Debug.print(DBG_DEBUG, root_1_brightness);
        // Debug.print(DBG_DEBUG, "name = ");
        // Debug.print(DBG_DEBUG, root_1_name);
        onState = root_1_onState;
        // Debug.print(DBG_DEBUG, "onState = ");
        // Debug.print(DBG_DEBUG, root_1_onState);

        //          strcpy(hostname, root_1_name);

        int root_2 = doc[2]; // 200

        int newBright = atoi(root_1_brightness);

        if (onState != lastState)
        {
          lastState = onState;
          Debug.print(DBG_INFO, "insecure: onState changed!");
          Debug.print(DBG_INFO, "insecure: %s", onState.c_str());
        }

        if (brightVal != newBright)
        {
          brightVal = newBright;
          //            Serial.printf("brightVal = %d\n", brightVal);

          FastLED.setBrightness(brightVal);
          FastLED.show();
        }
        if (hostname != root_1_name)
        {
          hostname = root_1_name;
          updateDisplay();
        }
      }
    }
    else
    {
      Debug.print(DBG_ERROR, "insecure: [HTTP] GET... failed, error: %s\n", http.errorToString(httpCode).c_str());
      // checkSSL();
      SERVER_SECURE = true;
      // return;
    }
    http.end(); // close connection
                //      Serial.println("Closing connection");
  }
  else
  {
    Serial.printf("insecure: [HTTP] Unable to connect\n");
    SERVER_SECURE = true;
  }
}

/***************************************************************************
   secure_connection: sets heartbeat and gets device info using HTTPS
***************************************************************************/
void secure_connection(String protocol)
{
  // Debug.print(DBG_INFO, "secure connection!");
  HTTPClient http;

  WiFiClientSecure *client = new WiFiClientSecure;
  if (client)
  {
    // client->setCACert(rootCACertificate); // comment out this to skip verifying certificate -- less secure
    Debug.print(DBG_DEBUG, "secure: [HTTPS] client created");

    {
      ////// SET HEARTBEAT ////////////////
      Debug.print(DBG_VERBOSE, "secure: setting heartbeat");
      String HB_URL = protocol + apiURL + "hb/" + DEVICE_ID;
      Debug.print(DBG_DEBUG, "secure: HB_URL is %s", HB_URL);

      if (http.begin(*client, HB_URL))
      {
        //  http.begin(apiURL + "HB/" + DEVICE_ID); //Specify destination for HTTP request
        http.addHeader("Content-Type", "application/json"); //Specify content-type header
        http.addHeader("Authorization", "Bearer " + auth_token);
        Debug.print(DBG_DEBUG, "secure: [HTTPS] about to POST");
        int httpResponseCode = http.POST("{}"); //Send the actual POST request
        // Debug.print(DBG_INFO, "POSTing complete");

        if (httpResponseCode > 0)
        {
          String response = http.getString();                    //Get the response to the request
          Debug.print(DBG_INFO, "secure: %i", httpResponseCode); //Print return code
          Debug.print(DBG_INFO, "secure: %s", response);         //Print request answer
        }
        else
        {
          Debug.print(DBG_ERROR, "secure: [HTTPS] Error on sending POST: ");
          Serial.println(http.errorToString(httpResponseCode).c_str());
          // checkSSL();
          SERVER_SECURE = false;
          // return;
        }

        http.end(); //Free resources
      }
      else
      {
        Debug.print(DBG_ERROR, "secure: [HTTPS] Unable to connect\n");
        SERVER_SECURE = false;
      }
      //}

      /////////////////////// get device parameters ///////////////////

      String get_URL = protocol + apiURL + DEVICE_ID;
      Debug.print(DBG_INFO, "secure: [HTTPS] opening connection to %s", get_URL);

      if (http.begin(*client, get_URL))
      {
        http.addHeader("Authorization", "Bearer " + auth_token);

        // Serial.print("[HTTPS] GET...\n");

        int httpCode = http.GET();

        // Check returning httpCode -- will be negative on error
        if (httpCode > 0)
        {
          Debug.print(DBG_INFO, "secure: [HTTPS] GET... code: %d\n", httpCode);

          // file found at server
          if (httpCode == HTTP_CODE_OK)
          {
            Debug.print(DBG_INFO, "secure: Found file at server: ");
            String payload = http.getString();
            Debug.print(DBG_INFO, payload.c_str());

            // parse payload

            const size_t capacity = JSON_ARRAY_SIZE(2) + JSON_OBJECT_SIZE(3) + 100;
            DynamicJsonDocument doc(capacity);

            //                    const char* json = "[\"device_266A08DBF47456428F703EEDF1E208B7117785DF\",{\"brightness\":\"123\",\"name\":\"triangle\",\"onState\":\"true\"}]";

            deserializeJson(doc, payload);

            const char *root_0 = doc[0]; // "device_266A08DBF47456428F703EEDF1E208B7117785DF"

            JsonObject root_1 = doc[1];
            const char *root_1_brightness = root_1["brightness"]; // "123"
            const char *root_1_name = root_1["name"];             // "triangle"
            const char *root_1_onState = root_1["onState"];       // "true"
            // Debug.print(DBG_DEBUG, "secure: root_0 = ");
            // Debug.print(DBG_DEBUG, root_0);
            // Debug.print(DBG_DEBUG, "secure: brightness = ");
            // Debug.print(DBG_DEBUG, root_1_brightness);
            // Debug.print(DBG_DEBUG, "secure: name = ");
            // Debug.print(DBG_DEBUG, root_1_name);
            onState = root_1_onState;
            // Debug.print(DBG_DEBUG, "secure: onState = ");
            // Debug.print(DBG_DEBUG, root_1_onState);

            //          strcpy(hostname, root_1_name);

            int root_2 = doc[2]; // 200

            int newBright = atoi(root_1_brightness);

            if (onState != lastState)
            {
              lastState = onState;
              Debug.print(DBG_INFO, "secure: onState changed!");
              Debug.print(DBG_INFO, onState.c_str());
            }

            if (brightVal != newBright)
            {
              brightVal = newBright;
              //            Serial.printf("brightVal = %d\n", brightVal);

              FastLED.setBrightness(brightVal);
              FastLED.show();
            }
            if (hostname != root_1_name)
            {
              hostname = root_1_name;
              updateDisplay();
            }
          }
        }
        else
        {
          Debug.print(DBG_ERROR, "secure: [HTTPS] GET... failed, error: %s\n", http.errorToString(httpCode).c_str());
          // checkSSL();
          SERVER_SECURE = false;
          // delete client;
          // return;
        }
        http.end(); // close connection
                    //      Serial.println("Closing connection");
      }
      else
      {
        Debug.print(DBG_ERROR, "secure: [HTTPS] Unable to connect\n");
        SERVER_SECURE = false;
        // delete client;
        // return;
      }
    } // end client

    delete client;
  }
  else
  {
    Debug.print(DBG_ERROR, "secure: [HTTPS] Unable to create client  ");
    SERVER_SECURE = false;
  }
}

/*****************************************************************************************************
  updateLEDS: toggles LEDs on/off depending on onState; if LEDs are on then fills with rainbow fade
*****************************************************************************************************/

void updateLEDS()
{

  if (onState == "false")
  {
    fill_solid(leds, NUM_LEDS, CRGB::Black);
    Debug.print(DBG_VERBOSE, "updateLEDS: onstate is false");
    FastLED.show();
  }

  else
  {
    Debug.print(DBG_VERBOSE, "updateLEDS: onState is true");
    fill_rainbow(leds, NUM_LEDS, gHue, speed);
    EVERY_N_MILLISECONDS(10)
    {
      Serial.println("updateLEDS: cycling rainbow hue");
      gHue++; // slowly cycle the "base color" through the rainbow
      FastLED.show();
    }
  }
  //  Serial.println("FastLED.show");
}

/**************************************************************************************************
  updateDisplay: updates the little OLED status screen of device
**************************************************************************************************/

void updateDisplay()
{
  Debug.print(DBG_INFO, "updateDisplay: updating display with hostname %s", hostname.c_str());

  display.clear();
  display.setFont(ArialMT_Plain_24); //11,16,24
  display.setTextAlignment(TEXT_ALIGN_LEFT);
  display.drawString(0, 0, hostname.c_str());
  //display.display(); //draw the screen

  display.setFont(ArialMT_Plain_16);
  display.drawString(0, 22, WiFi.localIP().toString());
  //  display.display(); //draw the screen

  display.setFont(ArialMT_Plain_10);
  display.drawString(0, 36, WiFi.SSID().c_str());
  display.drawString(0, 46, String(FastLED.getFPS()) + "FPS");

  display.display(); //draw the screen
}
