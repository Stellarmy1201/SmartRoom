/*
  ============================================================
  3707ICT - SMART ROOM IoT AUTOMATION SYSTEM
  ESP32 + Wokwi

  SENSORS
  ------------------------------------------------------------
  PIR Motion Sensor       -> GPIO 14
  DHT22 Temp/Humidity     -> GPIO 27
  LDR Light Sensor        -> GPIO 34
  Door Sensor/Switch      -> GPIO 26

  ACTUATORS
  ------------------------------------------------------------
  Servo - Automated Blind -> GPIO 25
  Room LED                -> GPIO 23
  Status / Fault LED      -> GPIO 2
  NeoPixel LED Strip      -> GPIO 4

  MANUAL CONTROLS
  ------------------------------------------------------------
  Light Override Button   -> GPIO 32
  Blind Override Button   -> GPIO 33

  FEATURES
  ------------------------------------------------------------
  1. Automatic lighting with hysteresis
  2. Automatic blinds with hysteresis (3 bands)
  3. Temperature alert with hysteresis (28C on / 26C off)
  4. Door/security monitoring
  5. Named occupancy states: Active / Idle / Unoccupied
  6. Manual override buttons for light and blinds, reset to
     AUTO automatically when the room becomes unoccupied
  7. Failsafe on DHT22 sensor fault (safe state + fast LED flash)
  8. MQTT cloud publishing to ThingSpeak over TLS
  9. MQTT username/password authentication
  ============================================================
*/


// ============================================================
// LIBRARIES
// ============================================================
#include <Arduino.h>

#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <PubSubClient.h>
#include <DHTesp.h>
#include <ESP32Servo.h>
#include <Adafruit_NeoPixel.h>
#include <secrets.h>

// ============================================================
// PIN DEFINITIONS
// ============================================================

// Sensors
#define PIR_PIN          14
#define DHT_PIN          27
#define LDR_PIN          34
#define DOOR_PIN         26

// Actuators
#define SERVO_PIN        25
#define ROOM_LED_PIN     23
#define STATUS_LED_PIN   2
#define LED_STRIP_PIN    4

#define LED_COUNT        8

#define LIGHT_BTN_PIN    32
#define BLIND_BTN_PIN    33


// ============================================================
// SENSOR / AUTOMATION SETTINGS
// ============================================================

// LDR thresholds
//
// You may need to slightly adjust these depending on the
// photoresistor behaviour in Wokwi.
//
#define DARK_THRESHOLD       1500
#define BRIGHT_THRESHOLD     3000

// Hysteresis: how far past a threshold the reading must go
// before the state actually flips, to stop rapid flicker
#define LIGHT_MARGIN         200

// Temperature thresholds
#define TEMP_HIGH            28.0
#define TEMP_NORMAL          26.0

// Servo positions
#define BLINDS_OPEN          160
#define BLINDS_HALF          90
#define BLINDS_CLOSED        20

// Keep occupancy active for 30 seconds after last movement
#define OCCUPANCY_TIMEOUT    30000UL

// Still considered "Active" this long after the last motion;
// beyond this but within OCCUPANCY_TIMEOUT counts as "Idle"
#define ACTIVE_WINDOW_MS      5000UL

// Sensor refresh
#define SENSOR_INTERVAL      2000UL

// Cloud update interval
#define MQTT_INTERVAL        20000UL
#define RECONNECT_INTERVAL   15000UL

// If the DHT22 gives no valid reading for this long, enter failsafe
#define DHT_TIMEOUT_MS 10000UL

// Button debounce
#define DEBOUNCE_MS      50UL

// ============================================================
// WIFI
// ============================================================

// Wokwi provides this virtual Wi-Fi network
const char* WIFI_SSID = "Wokwi-GUEST";
const char* WIFI_PASSWORD = "";


// ============================================================
// THINGSPEAK MQTT
// ============================================================
//
// Credentials live in secrets.h (kept out of GitHub).
//

const char* MQTT_SERVER = "mqtt3.thingspeak.com";
const int   MQTT_PORT   = 8883;   // TLS


// ============================================================
// OBJECTS
// ============================================================

DHTesp dhtSensor;

Servo blindServo;

Adafruit_NeoPixel ledStrip(
  LED_COUNT,
  LED_STRIP_PIN,
  NEO_GRB + NEO_KHZ800
);

WiFiClientSecure secureClient;
PubSubClient mqtt(secureClient);


// ============================================================
// MQTT TOPICS
// ============================================================

String topicPublish;


// ============================================================
// SENSOR VARIABLES
// ============================================================

float temperature = 0;
float humidity = 0;

int lightLevel = 0;

bool motionDetected = false;
bool doorOpen = false;


// ============================================================
// INTELLIGENT / EDGE VARIABLES
// ============================================================

bool occupied = false;
bool motionHasOccurred = false;

int occupancyScore = 0;

unsigned long lastMotionTime = 0;
bool sensorFault = false;
unsigned long lastGoodDhtTime = 0;

enum OccupancyState
{
  STATE_UNOCCUPIED,
  STATE_IDLE,
  STATE_ACTIVE
};

OccupancyState occupancyState = STATE_UNOCCUPIED;

// ============================================================
// ACTUATOR STATES
// ============================================================

bool roomLightOn = false;
bool climateAlert = false;
bool securityAlert = false;

int blindPosition = BLINDS_HALF;

bool roomIsDark = false;

enum BlindBand
{
  BAND_OPEN,
  BAND_HALF,
  BAND_CLOSED
};

BlindBand blindBand = BAND_HALF;

// ============================================================
// MANUAL OVERRIDES
// ============================================================
//
// Each actuator has its own override, set by a physical button.
// AUTO means the automation rules decide. Overrides reset to
// AUTO automatically once the room becomes unoccupied.
// ============================================================

enum OverrideMode
{
  OVERRIDE_AUTO,
  OVERRIDE_ON,
  OVERRIDE_OFF
};

OverrideMode lightOverride = OVERRIDE_AUTO;
OverrideMode blindOverride = OVERRIDE_AUTO;

bool lightBtnLastState = HIGH;
bool lightBtnStableState = HIGH;
unsigned long lightBtnLastChange = 0;

bool blindBtnLastState = HIGH;
bool blindBtnStableState = HIGH;
unsigned long blindBtnLastChange = 0;

bool wasOccupied = false;

// ============================================================
// TIMERS
// ============================================================

unsigned long previousSensorTime = 0;
unsigned long previousMQTTTime = 0;
unsigned long previousReconnectTime = 0;


// ============================================================
// HELPER: CLOUD CONFIGURED?
// ============================================================

bool cloudConfigured()
{
  return
    String(TS_USERNAME) != "YOUR_MQTT_USERNAME" &&
    String(TS_PASSWORD) != "YOUR_MQTT_PASSWORD";
}


// ============================================================
// BUILD MQTT TOPIC
// ============================================================

void createMQTTTopics()
{
  topicPublish = "channels/" + String(TS_CHANNEL_ID) + "/publish";
}

// ============================================================
// WIFI CONNECTION
// ============================================================

void connectWiFi()
{
  Serial.println();
  Serial.println("Connecting to WiFi...");

  WiFi.begin(WIFI_SSID, WIFI_PASSWORD, 6);

  int attempts = 0;

  while (WiFi.status() != WL_CONNECTED && attempts < 30)
  {
    delay(500);
    Serial.print(".");
    attempts++;
  }

  Serial.println();

  if (WiFi.status() == WL_CONNECTED)
  {
    Serial.println("WiFi connected!");
    Serial.print("ESP32 IP Address: ");
    Serial.println(WiFi.localIP());
  }
  else
  {
    Serial.println("WiFi connection failed.");
    Serial.println("Local automation will continue.");
  }
}


// ============================================================
// CONNECT MQTT
// ============================================================

void connectMQTT()
{
  if (!cloudConfigured())
  {
    return;
  }

  if (WiFi.status() != WL_CONNECTED)
  {
    return;
  }

  if (mqtt.connected())
  {
    return;
  }

  Serial.println("Connecting to ThingSpeak MQTT...");

  if (mqtt.connect(TS_CLIENT_ID, TS_USERNAME, TS_PASSWORD))
  {
    Serial.println("MQTT connected!");
  }
  else
  {
    Serial.print("MQTT failed. State = ");
    Serial.println(mqtt.state());
  }
}


// ============================================================
// ROOM LIGHT
// ============================================================

void setRoomLighting(bool enabled)
{
  roomLightOn = enabled;

  digitalWrite(
    ROOM_LED_PIN,
    enabled ? HIGH : LOW
  );


  if (enabled)
  {
    // Warm-white simulated smart-room lighting
    for (int i = 0; i < LED_COUNT; i++)
    {
      ledStrip.setPixelColor(
        i,
        ledStrip.Color(
          255,
          170,
          60
        )
      );
    }
  }

  else
  {
    ledStrip.clear();
  }

  ledStrip.show();
}


// ============================================================
// READ SENSORS
// ============================================================

void readSensors()
{
  // ----------------------------------------------------------
  // PIR motion sensor
  // ----------------------------------------------------------

  motionDetected =
    digitalRead(PIR_PIN) == HIGH;


  // ----------------------------------------------------------
  // Door sensor
  //
  // INPUT_PULLUP means:
  // HIGH = open
  // LOW = closed
  // ----------------------------------------------------------

  doorOpen =
    digitalRead(DOOR_PIN) == HIGH;


  // ----------------------------------------------------------
  // LDR
  // ----------------------------------------------------------

  lightLevel = analogRead(LDR_PIN);


  // ----------------------------------------------------------
  // DHT22
  // ----------------------------------------------------------

  TempAndHumidity data =
    dhtSensor.getTempAndHumidity();

  bool dhtOk =
    !isnan(data.temperature) &&
    !isnan(data.humidity);

  if (dhtOk)
  {
    temperature = data.temperature;
    humidity = data.humidity;
    lastGoodDhtTime = millis();
  }

  sensorFault =
    (millis() - lastGoodDhtTime) > DHT_TIMEOUT_MS;


  // ----------------------------------------------------------
  // Motion memory
  // ----------------------------------------------------------

  if (motionDetected)
  {
    lastMotionTime = millis();
    motionHasOccurred = true;
  }
}


// ============================================================
// EDGE INTELLIGENCE
// ============================================================
//
// Rule-based occupancy inference.
//
// Instead of simply saying:
//
// PIR ON  = occupied
// PIR OFF = empty
//
// the ESP32 remembers recent movement and uses multiple signals
// to make a more useful local occupancy decision.
//
// ============================================================

void calculateOccupancy()
{
  bool recentMotion = false;

  if (motionHasOccurred)
  {
    recentMotion =
      millis() - lastMotionTime
      < OCCUPANCY_TIMEOUT;
  }


  occupancyScore = 0;


  // Strong evidence
  if (motionDetected)
  {
    occupancyScore += 70;
  }


  // Recent movement provides weaker evidence
  if (recentMotion)
  {
    occupancyScore += 25;
  }


  // Door opening slightly increases occupancy confidence
  if (doorOpen)
  {
    occupancyScore += 10;
  }


  if (occupancyScore > 100)
  {
    occupancyScore = 100;
  }


  // ----------------------------------------------------------
  // Named occupancy state, based on time since last motion
  // ----------------------------------------------------------

  bool recentlyActive = false;

  if (motionHasOccurred)
  {
    recentlyActive =
      (millis() - lastMotionTime) < ACTIVE_WINDOW_MS;
  }

  if (motionDetected || recentlyActive)
  {
    occupancyState = STATE_ACTIVE;
  }

  else if (recentMotion)
  {
    // Within the timeout window but no motion for a while:
    // occupant may be sitting still
    occupancyState = STATE_IDLE;
  }

  else
  {
    occupancyState = STATE_UNOCCUPIED;
  }


  occupied =
    occupancyState != STATE_UNOCCUPIED;


  if (wasOccupied && !occupied)
  {
    lightOverride = OVERRIDE_AUTO;
    blindOverride = OVERRIDE_AUTO;

    Serial.println("Room unoccupied: overrides reset to AUTO.");
  }

  wasOccupied = occupied;
}


// ============================================================
// AUTOMATION RULE 1
// SMART LIGHTING
// ============================================================
//
// IF room is occupied
// AND room is dark
// THEN turn lighting ON.
//
// Otherwise turn it OFF.
//
// ============================================================

void smartLightingRule()
{
  if (lightOverride == OVERRIDE_ON)
  {
    setRoomLighting(true);
    return;
  }

  if (lightOverride == OVERRIDE_OFF)
  {
    setRoomLighting(false);
    return;
  }


  // Only flip the dark/light state once the reading is clearly
  // past the threshold, not just brushing it (hysteresis)
  if (lightLevel < (DARK_THRESHOLD - LIGHT_MARGIN))
  {
    roomIsDark = true;
  }
  else if (lightLevel > (DARK_THRESHOLD + LIGHT_MARGIN))
  {
    roomIsDark = false;
  }
  // else: keep whatever it already was


  if (occupied && roomIsDark)
  {
    setRoomLighting(true);
  }

  else
  {
    setRoomLighting(false);
  }
}


// ============================================================
// AUTOMATION RULE 2
// AUTOMATIC BLINDS
// ============================================================
//
// Bright light  -> close blinds
// Medium light  -> half open
// Dark          -> open blinds
//
// ============================================================

void automaticBlindsRule()
{
  int newPosition = blindPosition;

  if (blindOverride == OVERRIDE_ON)
  {
    newPosition = BLINDS_OPEN;
  }

  else if (blindOverride == OVERRIDE_OFF)
  {
    newPosition = BLINDS_CLOSED;
  }

  else
  {
    // Automatic: move between bands only once the reading is
    // clearly past a boundary, so the servo stops hunting
    // back and forth near 1500 / 3000

    if (
      blindBand == BAND_OPEN &&
      lightLevel >= (DARK_THRESHOLD + LIGHT_MARGIN)
    )
    {
      blindBand = BAND_HALF;
    }

    else if (blindBand == BAND_HALF)
    {
      if (lightLevel < (DARK_THRESHOLD - LIGHT_MARGIN))
      {
        blindBand = BAND_OPEN;
      }
      else if (lightLevel >= (BRIGHT_THRESHOLD + LIGHT_MARGIN))
      {
        blindBand = BAND_CLOSED;
      }
    }

    else if (
      blindBand == BAND_CLOSED &&
      lightLevel < (BRIGHT_THRESHOLD - LIGHT_MARGIN)
    )
    {
      blindBand = BAND_HALF;
    }


    if (blindBand == BAND_OPEN)
    {
      newPosition = BLINDS_OPEN;
    }
    else if (blindBand == BAND_HALF)
    {
      newPosition = BLINDS_HALF;
    }
    else
    {
      newPosition = BLINDS_CLOSED;
    }
  }


  // Only move the servo if the target actually changed
  if (newPosition != blindPosition)
  {
    blindPosition = newPosition;
    blindServo.write(blindPosition);
  }
}


// ============================================================
// AUTOMATION RULE 3
// TEMPERATURE / CLIMATE CONTROL
// ============================================================
//
// High temperature turns the Status/AC LED ON.
//
// Hysteresis prevents rapid ON/OFF switching:
//
// ON  >= 28C
// OFF <= 26C
//
// ============================================================

void temperatureRule()
{
  if (temperature >= TEMP_HIGH)
  {
    climateAlert = true;
  }

  else if (temperature <= TEMP_NORMAL)
  {
    climateAlert = false;
  }
}


// ============================================================
// AUTOMATION RULE 4
// DOOR SECURITY MONITORING
// ============================================================
//
// If the door opens but the ESP32 does not currently believe
// someone is occupying the room, raise a security alert.
//
// ============================================================

void securityRule()
{
  securityAlert =
    doorOpen && !occupied;
}


// ============================================================
// STATUS LED
// ============================================================

void updateStatusLED()
{
  if (sensorFault)
  {
    // Fast flash: sensor fault (highest priority)

    bool flash =
      (millis() / 150) % 2;

    digitalWrite(
      STATUS_LED_PIN,
      flash
    );
  }

  else if (securityAlert)
  {
    // Slower flash for security alert

    bool flash =
      (millis() / 300) % 2;

    digitalWrite(
      STATUS_LED_PIN,
      flash
    );
  }

  else if (climateAlert)
  {
    // Solid LED for high temperature

    digitalWrite(
      STATUS_LED_PIN,
      HIGH
    );
  }

  else
  {
    digitalWrite(
      STATUS_LED_PIN,
      LOW
    );
  }
}

// ============================================================
// MANUAL OVERRIDE BUTTONS
// ============================================================

void cycleLightOverride();
void cycleBlindOverride();

void checkButtons()
{
  bool lightBtnRaw = digitalRead(LIGHT_BTN_PIN);

  if (lightBtnRaw != lightBtnLastState)
  {
    lightBtnLastChange = millis();
  }

  if ((millis() - lightBtnLastChange) > DEBOUNCE_MS)
  {
    if (lightBtnRaw != lightBtnStableState)
    {
      lightBtnStableState = lightBtnRaw;

      if (lightBtnStableState == LOW)
      {
        cycleLightOverride();
      }
    }
  }

  lightBtnLastState = lightBtnRaw;


  bool blindBtnRaw = digitalRead(BLIND_BTN_PIN);

  if (blindBtnRaw != blindBtnLastState)
  {
    blindBtnLastChange = millis();
  }

  if ((millis() - blindBtnLastChange) > DEBOUNCE_MS)
  {
    if (blindBtnRaw != blindBtnStableState)
    {
      blindBtnStableState = blindBtnRaw;

      if (blindBtnStableState == LOW)
      {
        cycleBlindOverride();
      }
    }
  }

  blindBtnLastState = blindBtnRaw;
}


void cycleLightOverride()
{
  if (lightOverride == OVERRIDE_AUTO)
  {
    lightOverride = OVERRIDE_ON;
  }
  else if (lightOverride == OVERRIDE_ON)
  {
    lightOverride = OVERRIDE_OFF;
  }
  else
  {
    lightOverride = OVERRIDE_AUTO;
  }

  Serial.print("Light override: ");
  Serial.println(
    lightOverride == OVERRIDE_AUTO ? "AUTO" :
    lightOverride == OVERRIDE_ON   ? "ON"   : "OFF"
  );
    if (!sensorFault)
  {
    smartLightingRule();
  }
}


void cycleBlindOverride()
{
  if (blindOverride == OVERRIDE_AUTO)
  {
    blindOverride = OVERRIDE_ON;
  }
  else if (blindOverride == OVERRIDE_ON)
  {
    blindOverride = OVERRIDE_OFF;
  }
  else
  {
    blindOverride = OVERRIDE_AUTO;
  }

  Serial.print("Blind override: ");
  Serial.println(
    blindOverride == OVERRIDE_AUTO ? "AUTO"   :
    blindOverride == OVERRIDE_ON   ? "OPEN"   : "CLOSED"
  );
    if (!sensorFault)
  {
    automaticBlindsRule();
  }
}


// ============================================================
// RUN AUTOMATION
// ============================================================

void enterFailsafe();
void runAutomation()
{
  calculateOccupancy();

  securityRule();


  if (sensorFault)
  {
    enterFailsafe();
  }

  else
  {
    temperatureRule();

    smartLightingRule();

    automaticBlindsRule();
  }
  updateStatusLED();
}


// ============================================================
// FAILSAFE
// ============================================================
//
// Entered when the DHT22 has given no valid reading for
// DHT_TIMEOUT_MS. Temperature-dependent automation is untrusted,
// so the system falls back to a known-safe state instead of
// acting on a stale value.
// ============================================================

void enterFailsafe()
{
  setRoomLighting(false);

  blindPosition = BLINDS_HALF;
  blindServo.write(blindPosition);

  climateAlert = false;
}


// ============================================================
// SERIAL MONITOR
// ============================================================

void printSystemStatus()
{
  Serial.println();
  Serial.println("======================================");
  Serial.println("       SMART ROOM STATUS");
  Serial.println("======================================");

  Serial.print("Temperature: ");
  Serial.print(temperature);
  Serial.println(" C");

  Serial.print("Humidity: ");
  Serial.print(humidity);
  Serial.println(" %");

  Serial.print("Light Sensor: ");
  Serial.println(lightLevel);

  Serial.print("Motion: ");

  if (motionDetected)
    Serial.println("YES");
  else
    Serial.println("NO");


  Serial.print("Door: ");

  if (doorOpen)
    Serial.println("OPEN");
  else
    Serial.println("CLOSED");


  Serial.print("Occupancy Score: ");
  Serial.print(occupancyScore);
  Serial.println("%");


  Serial.print("Occupancy State: ");

  if (occupancyState == STATE_ACTIVE)
    Serial.println("ACTIVE");
  else if (occupancyState == STATE_IDLE)
    Serial.println("IDLE");
  else
    Serial.println("UNOCCUPIED");


  Serial.print("Room Light: ");

  if (roomLightOn)
    Serial.println("ON");
  else
    Serial.println("OFF");


  Serial.print("Blind Angle: ");
  Serial.print(blindPosition);
  Serial.println(" degrees");


  Serial.print("Temperature Alert: ");

  if (climateAlert)
    Serial.println("YES");
  else
    Serial.println("NO");


  Serial.print("Security Alert: ");

  if (securityAlert)
    Serial.println("YES");
  else
    Serial.println("NO");


  Serial.println("Mode: AUTOMATIC (rule-based)");


  Serial.println("======================================");
}

// ============================================================
// PUBLISH CLOUD DATA
// ============================================================

void publishMQTTData()
{
  if (!mqtt.connected())
  {
    return;
  }

  // Alert code: 0 = normal, 1 = high temp, 2 = security, 3 = sensor fault
  int alertCode = 0;

  if (sensorFault)
  {
    alertCode = 3;
  }
  else if (securityAlert)
  {
    alertCode = 2;
  }
  else if (climateAlert)
  {
    alertCode = 1;
  }

  // All 8 ThingSpeak fields in one message
  String payload = "field1=" + String(temperature, 1);
  payload += "&field2=" + String(humidity, 1);
  payload += "&field3=" + String(lightLevel);
  payload += "&field4=" + String(occupancyScore);
  payload += "&field5=" + String(roomLightOn ? 1 : 0);
  payload += "&field6=" + String(blindPosition);
  payload += "&field7=" + String(doorOpen ? 1 : 0);
  payload += "&field8=" + String(alertCode);

  bool ok = mqtt.publish(topicPublish.c_str(), payload.c_str());

  Serial.println();
  Serial.print("ThingSpeak publish: ");
  Serial.println(ok ? "OK" : "FAILED");
  Serial.println(payload);
}


// ============================================================
// SETUP
// ============================================================

void setup()
{
  Serial.begin(115200);

  delay(500);


  Serial.println();
  Serial.println("======================================");
  Serial.println("3707ICT SMART ROOM");
  Serial.println("ESP32 IoT Automation System");
  Serial.println("======================================");


  // ----------------------------------------------------------
  // GPIO
  // ----------------------------------------------------------

  pinMode(
    PIR_PIN,
    INPUT
  );

  pinMode(
    DOOR_PIN,
    INPUT_PULLUP
  );

  pinMode(
    LIGHT_BTN_PIN,
    INPUT_PULLUP
  );

  pinMode(
    BLIND_BTN_PIN,
    INPUT_PULLUP
  );

  pinMode(
    ROOM_LED_PIN,
    OUTPUT
  );

  pinMode(
    STATUS_LED_PIN,
    OUTPUT
  );


  // ----------------------------------------------------------
  // DHT22
  // ----------------------------------------------------------

  dhtSensor.setup(
    DHT_PIN,
    DHTesp::DHT22
  );
    lastGoodDhtTime = millis();


  // ----------------------------------------------------------
  // Servo
  // ----------------------------------------------------------

  blindServo.setPeriodHertz(50);

  blindServo.attach(
    SERVO_PIN,
    500,
    2400
  );

  blindServo.write(
    BLINDS_HALF
  );


  // ----------------------------------------------------------
  // NeoPixel
  // ----------------------------------------------------------

  ledStrip.begin();

  ledStrip.setBrightness(
    100
  );

  ledStrip.clear();

  ledStrip.show();


  // ----------------------------------------------------------
  // LEDs
  // ----------------------------------------------------------

  digitalWrite(
    ROOM_LED_PIN,
    LOW
  );

  digitalWrite(
    STATUS_LED_PIN,
    LOW
  );


  // ----------------------------------------------------------
  // MQTT topics
  // ----------------------------------------------------------

  createMQTTTopics();


  // ----------------------------------------------------------
  // CLOUD
  // ----------------------------------------------------------

  if (cloudConfigured())
  {
    connectWiFi();


    /*
      Wokwi simulation convenience.

      Communication is encrypted using TLS, but certificate
      validation is disabled here for simulator simplicity.

      For a real physical deployment, install and validate
      the correct root CA certificate.
    */

    secureClient.setInsecure();


    mqtt.setServer(
      MQTT_SERVER,
      MQTT_PORT
    );

    mqtt.setBufferSize(
      512
    );
    mqtt.setSocketTimeout(
      5
    );


    connectMQTT();
  }

  else
  {
    Serial.println();
    Serial.println("--------------------------------------");
    Serial.println("Cloud credentials not configured.");
    Serial.println("Local Wokwi automation will still work.");
    Serial.println("Add Adafruit IO username/key later.");
    Serial.println("--------------------------------------");
  }
}


// ============================================================
// MAIN LOOP
// ============================================================

void loop()
{
  // ----------------------------------------------------------
  // Maintain MQTT connection
  // ----------------------------------------------------------

    if (cloudConfigured())
  {
    if (mqtt.connected())
    {
      mqtt.loop();
    }
    else if (
      WiFi.status() == WL_CONNECTED &&
      millis() - previousReconnectTime >= RECONNECT_INTERVAL
    )
    {
      previousReconnectTime = millis();
      connectMQTT();
    }
  }


  unsigned long currentTime =
    millis();


  // ----------------------------------------------------------
  // Read sensors + automation
  // ----------------------------------------------------------

  if (
    currentTime - previousSensorTime
    >= SENSOR_INTERVAL
  )
  {
    previousSensorTime =
      currentTime;


    readSensors();

    runAutomation();

    printSystemStatus();
  }


  // ----------------------------------------------------------
  // Keep flashing security LED responsive
  // ----------------------------------------------------------

  updateStatusLED();
  checkButtons();


  // ----------------------------------------------------------
  // Cloud upload
  // ----------------------------------------------------------

  if (
    cloudConfigured() &&
    currentTime - previousMQTTTime
    >= MQTT_INTERVAL
  )
  {
    previousMQTTTime =
      currentTime;


    publishMQTTData();
  }
}