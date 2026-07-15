/*
 * ==========================================================
 * CONFIGURATION this code for first implementation
 * ==========================================================
 */

#define ENABLE_USER_AUTH
#define ENABLE_DATABASE

#include <Arduino.h>

#if defined(ESP32)
#include <WiFi.h>
#elif defined(ESP8266)
#include <ESP8266WiFi.h>
#endif

#include <WiFiClientSecure.h>
#include <FirebaseClient.h>
#include <ArduinoJson.h>
#include <PZEM004Tv30.h>
#include "ExampleFunctions.h"

/* WiFi */
#define WIFI_SSID      "Me"
#define WIFI_PASSWORD  "intelligent"

/* Firebase */
#define API_KEY        "AIzaSyBZLtf0wO0jivZXzfebOIehXAXuF40KCCA"
#define DATABASE_URL   "https://esp32-918b2-default-rtdb.firebaseio.com/"
#define USER_EMAIL     "esp32@gmail.com"
#define USER_PASS      "emmy4012"

/* PZEM Pins (ESP32 Serial2 defaults: RX=16, TX=17) */
#if defined(ESP32)
#define PZEM_RX_PIN    16
#define PZEM_TX_PIN    17
#else
#define PZEM_RX_PIN    12 
#define PZEM_TX_PIN    13
#endif

/* Database Paths */
String infoPath   = "board1/info/";
String meterPath  = "board1/meter/";


/*
 * ==========================================================
 * HARDWARE & FIREBASE OBJECTS
 * ==========================================================
 */

/* PZEM Sensor Instance */
#if defined(ESP32)
PZEM004Tv30 pzem(Serial2, PZEM_RX_PIN, PZEM_TX_PIN);
#else
PZEM004Tv30 pzem(PZEM_RX_PIN, PZEM_TX_PIN);
#endif

/* Firebase Infrastructure */
UserAuth userAuth(API_KEY, USER_EMAIL, USER_PASS);
SSL_CLIENT sslClient;
FirebaseApp app;
RealtimeDatabase Database;

using AsyncClient = AsyncClientClass;
AsyncClient client(sslClient);


/*
 * ==========================================================
 * FUNCTION PROTOTYPES
 * ==========================================================
 */

void connectWiFi();
void setupFirebase();
void authHandler(AsyncResult &result);
void writeBoardInfo();
void readAndUploadMeterData();
void handleBlink(unsigned long currentTime);


/*
 * ==========================================================
 * VARIABLES
 * ==========================================================
 */
#define LED_PIN 2

unsigned long previousUploadMillis = 0;
const unsigned long uploadInterval = 10000; // Send data every 10 seconds

unsigned long previousHistoryMillis = 0;
const unsigned long historyInterval = 600000;  // 600,000 ms = 10 Minutes (AI Dataset Logger)

unsigned long previousBlinkMillis = 0;
const unsigned long blinkInterval = 1000;   // Toggle LED every 1 second
bool ledState = false;


// NEW: Persistent tracking variables for running totals
//ouble totalEnergyAccumulated = 0.0;


/*
 * ==========================================================
 * SETUP
 * ==========================================================
 */

void setup()
{

  // UNCOMMENT THE LINE BELOW ONCE TO RESET ENERGY TO ZERO, THEN COMMENT IT OUT AGAIN
    // pzem.resetEnergy(); 
    
    //Serial.println("PZEM Energy register cleared back to 0.00 kWh!");
    pinMode(LED_PIN, OUTPUT);
    Serial.begin(115200);

    connectWiFi();
    setupFirebase();
}


/*
 * ==========================================================
 * LOOP
 * ==========================================================
 */

void loop()
{
    app.loop();

    unsigned long now = millis();
    handleBlink(now);

    if (!app.ready())
        return;

    if (now - previousUploadMillis >= uploadInterval)
    {
        previousUploadMillis = now;

        Serial.println("Reading sensors and pushing to Firebase...");
        
        writeBoardInfo();
        readAndUploadMeterData(now);
    }
}


/*
 * ==========================================================
 * NON-BLOCKING LED BLINK
 * ==========================================================
 */
void handleBlink(unsigned long currentTime)
{
    if (currentTime - previousBlinkMillis >= blinkInterval)
    {
        previousBlinkMillis = currentTime;
        ledState = !ledState; 
        digitalWrite(LED_PIN, ledState);
    }
}


/*
 * ==========================================================
 * WIFI
 * ==========================================================
 */

void connectWiFi()
{
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    Serial.print("Connecting to WiFi");

    while (WiFi.status() != WL_CONNECTED)
    {
        Serial.print(".");
        delay(500); 
    }

    Serial.println();
    Serial.print("IP: ");
    Serial.println(WiFi.localIP());
}


/*
 * ==========================================================
 * FIREBASE SETUP
 * ==========================================================
 */

void setupFirebase()
{
    sslClient.setInsecure();

#if defined(ESP32)
    sslClient.setConnectionTimeout(2000);
    sslClient.setHandshakeTimeout(10);
#endif

    initializeApp(
        client,
        app,
        getAuth(userAuth),
        authHandler,
        "authTask");

    app.getApp<RealtimeDatabase>(Database);
    Database.url(DATABASE_URL);
}


/*
 * ==========================================================
 * WRITE SYSTEM DATA TO FIREBASE
 * ==========================================================
 */

void writeBoardInfo()
{
    Database.set<String>(client, infoPath + "status", "ESP32 Online");
    Database.set<int>(client, infoPath + "uptime", millis() / 1000);
}


/*
 * ==========================================================
 * READ PZEM & UPLOAD SMART METER DATA
 * ==========================================================
 */

void readAndUploadMeterData(unsigned long currentTime)
{
    float voltage   = pzem.voltage();
    float current   = pzem.current();
    float power     = pzem.power();
    float energy    = pzem.energy();
    float frequency = pzem.frequency();
    float pf        = pzem.pf();

    if (isnan(voltage)) 
    {
        Serial.println("Error reading from PZEM sensor!");
        Database.set<String>(client, meterPath + "error", "Sensor Read Error");
        return;
    }
// --- MATH: Add fresh sensor readings to the running lifetime totals ---
    //totalEnergyAccumulated += energy;
   

    // Process structured document inside ArduinoJson
    DynamicJsonDocument doc(256);
    doc["voltage"]       = voltage;
    doc["current"]       = current;
    doc["power"]         = power;
    doc["energy"]        = energy;
    doc["frequency"]     = frequency;
    doc["power_factor"]  = pf;
// New total fields added to the flat metrics JSON layout
   // doc["total_energy_sum"]     = totalEnergyAccumulated;
    
    //THE FIX: Tells Firebase to insert the exact server time ---

    JsonObject tsObj     = doc.createNestedObject("timestamp");
    tsObj[".sv"]         = "timestamp";
  

    String jsonString;
    serializeJson(doc, jsonString);

    // FIXED: Convert the string representation into Firebase's native object_t wrapper
    object_t jsonPayload(jsonString.c_str());

    // FIXED: Use <object_t> template definition to match FirebaseClient v2 expectations
    Database.set<object_t>(client, meterPath + "metrics", jsonPayload);
  
    Database.set<String>(client, meterPath + "error", "None");

    Serial.printf("V: %.1fV | I: %.2fA | P: %.1fW | E: %.2fkWh\n", voltage, current, power, energy);


    // 3. TIME CHECK: Only append to history log if 10 minutes have passed
    if (currentTime - previousHistoryMillis >= historyInterval || previousHistoryMillis == 0)
    {
        previousHistoryMillis = currentTime;
        
        // This pushes to the timeline node for your AI model dataset
        Database.push<object_t>(client, meterPath + "history", jsonPayload);
        Serial.println(">> Success: 10-Minute Historical Snapshot logged to Firebase! <<");
    }
}


/*
 * ==========================================================
 * AUTH HANDLER
 * ==========================================================
 */

void authHandler(AsyncResult &result)
{
    if (!result.available())
        return;
}