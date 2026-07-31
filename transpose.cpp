#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include <FastLED.h>
#include <SPIFFS.h>

// ============================================
// HARDWARE PINOUT - ESP32-S3
// ============================================
#define RGB_LED_PIN 48
#define NUM_LEDS 1
#define LED_TYPE WS2812B
#define COLOR_ORDER RGB
#define RGB_BRIGHTNESS 30

#define PROBE_LOW 13
#define PROBE_MID 12
#define PROBE_HIGH 11

#define STROBE_GREEN 4
#define STROBE_ORANGE 5
#define STROBE_RED 6
#define SIREN_PIN 7

// ============================================
// TELNET REMOTE CONSOLE
// ============================================
#define TELNET_PORT 2323
#define TELNET_AUTH_TIMEOUT 30000
#define TELNET_IDLE_TIMEOUT 300000
#define TELNET_MAX_FAILED_AUTH 5

// ============================================
// LED PATTERN SYSTEM (RGB VERSION)
// ============================================
struct LedPattern
{
  CRGB color;
  int on_time;
  int off_time;
  int repeats;
  int pause;
  const char *description;
};

// RGB Color Definitions
const CRGB COLOR_BOOTING = CRGB::Purple;
const CRGB COLOR_CONFIG_MODE = CRGB::Cyan;
const CRGB COLOR_NO_WIFI = CRGB::Red;
const CRGB COLOR_CONNECTING = CRGB::Yellow;
const CRGB COLOR_NORMAL = CRGB::Green;
const CRGB COLOR_MQTT_LOST = CRGB::Orange;
const CRGB COLOR_ALERT_YELLOW = CRGB::Yellow;
const CRGB COLOR_ALERT_ORANGE = CRGB::Orange;
const CRGB COLOR_ALERT_RED = CRGB::Red;
const CRGB COLOR_TELEMETRY = CRGB::Blue;
//                                                    ON, OFF, REPEAT(0 for infinite), delay
const LedPattern PATTERN_BOOTING = {CRGB::Purple, 100, 100, 5, 1000, "Booting sequence"};
const LedPattern PATTERN_CONFIG_MODE = {CRGB::Cyan, 500, 500, 0, 0, "Configuration mode"};
const LedPattern PATTERN_NO_WIFI = {CRGB::Red, 1000, 1000, 0, 0, "No WiFi - Blinking Red"};
const LedPattern PATTERN_CONNECTING = {CRGB::Purple, 500, 500, 0, 0, "Connecting - Blinking Purple"};
const LedPattern PATTERN_NORMAL = {CRGB::Green, 500, 500, 0, 0, "Normal - Blinking Green"};
const LedPattern PATTERN_MQTT_LOST = {CRGB::Red, 200, 200, 0, 0, "MQTT Lost - Blinking Red"};
const LedPattern PATTERN_ALERT_YELLOW = {COLOR_ALERT_YELLOW, 300, 300, 2, 2000, "Alert Level: Yellow"};
const LedPattern PATTERN_ALERT_ORANGE = {COLOR_ALERT_ORANGE, 300, 300, 3, 2000, "Alert Level: Orange"};
const LedPattern PATTERN_ALERT_RED = {COLOR_ALERT_RED, 100, 100, 0, 0, "Alert Level: RED"};
const LedPattern PATTERN_TELEMETRY = {COLOR_TELEMETRY, 1000, 0, 1, 0, "Telemetry sent"};

// ============================================
// PAGASA / CBFEWS PROTOCOL
// ============================================

#define SIREN_L1_DURATION_MS 60000
#define SIREN_L1_SILENCE_MS 30000
#define SIREN_L1_CYCLES 3

#define SIREN_L2_DURATION_MS 18000
#define SIREN_L2_SILENCE_MS 15000
#define SIREN_L2_CYCLES 5

#define SIREN_L3_DURATION_MS 180000
#define SIREN_L3_SILENCE_MS 0
#define SIREN_L3_CYCLES 1

#define STROBE_L1_PATTERN_MS 2000
#define STROBE_L2_PATTERN_MS 500
#define STROBE_L3_PATTERN_MS 100

#define DEBOUNCE_MS 50
#define TELEMETRY_INTERVAL_MS 30000
#define CONTINUOUS_INTERVAL_MS 5000
#define WIFI_RECONNECT_DELAY_MS 5000
#define MQTT_RECONNECT_DELAY_MS 5000
#define WIFI_CONNECT_TIMEOUT_MS 30000
#define MAX_SERIAL_CMD_LENGTH 128
#define MAX_TELNET_CMD_LENGTH 128

// ============================================
// CONFIGURATION STRUCTURE
// ============================================
struct Config
{
  char wifi_ssid[64];
  char wifi_password[64];
  char mqtt_broker[128];
  char mqtt_port[6];
  char mqtt_username[64];
  char mqtt_password[64];
  char location[64];
  char site_id[64];
  char node_type[16];
  char admin_password[32];
  bool configured;
};

Config config = {
    "",
    "",
    "da4b7dd506824d31ba0940e25eb20f42.s1.eu.hivemq.cloud",
    "8883",
    "Admin",
    "Admin12345",
    "",
    "",
    "river",
    "Admin12345",
    false};

// ============================================
// ENUMS & TYPES
// ============================================
enum AlertLevel
{
  ALERT_NONE = 0,
  ALERT_YELLOW = 1,
  ALERT_ORANGE = 2,
  ALERT_RED = 3
};

// ============================================
// FUNCTION PROTOTYPES
// ============================================
AlertLevel readProbes();
void startSiren(AlertLevel level);
void sendTelemetry();
void processCommand(String &cmd, Stream &output);
void printNetworkStatus(Stream &output);
void setLedPattern(const LedPattern *pattern, bool temporary = false, unsigned long duration = 0);
void sendMQTT(AlertLevel level, const char *eventType);
void connectMQTT();
void handleWiFi();
void updateStrobeLights();
void handleSiren();
void sampleWaterProbes();
void ledSignalAlert(AlertLevel level);
void ledSignalTelemetry();
void handleTelnetConnection();
void handleTelnetAuth(String &cmd);
void setupTelnet();
void sendTelnetBanner();
void loadConfig();
void saveConfig();
void factoryReset();
void updateStatusLED();
int freeMemory();
String getMessage(AlertLevel level);
String getInstruction(AlertLevel level);

// ============================================
// GLOBALS
// ============================================
CRGB leds[NUM_LEDS];
WiFiClientSecure espClient;
PubSubClient mqtt(espClient);
WiFiServer telnetServer(TELNET_PORT);
WiFiClient telnetClient;

AlertLevel currentAlert = ALERT_NONE;
AlertLevel targetAlert = ALERT_NONE;
int stableSampleCount = 0;
unsigned long lastProbeSample = 0;
unsigned long lastTelemetry = 0;
unsigned long lastContinuousSend = 0;
unsigned long lastMqttRetry = 0;
unsigned long lastWifiAttempt = 0;

int connectionRetryCount = 0;
int maxRetries = 5;

AlertLevel activeSirenLevel = ALERT_NONE;
bool sirenActive = false;
int sirenCyclesRemaining = 0;
bool sirenOn = false;
unsigned long lastSirenStep = 0;

AlertLevel activeStrobeLevel = ALERT_NONE;
bool strobeState = false;
unsigned long lastStrobeToggle = 0;

bool telnetSessionActive = false;
bool telnetAuthenticated = false;
unsigned long telnetLastActivity = 0;
unsigned long telnetSessionStart = 0;
int telnetFailedAttempts = 0;
String telnetCommandBuffer = "";

bool mqtt_connected = false;
bool wifi_connected = false;
bool wifiConnecting = false;
unsigned long wifiConnectStart = 0;

const LedPattern *currentPattern = &PATTERN_BOOTING;
const LedPattern *basePattern = &PATTERN_NORMAL;
bool patternOverride = false;
unsigned long patternOverrideEnd = 0;
unsigned long lastBlinkChange = 0;
int blinkCount = 0;
bool ledState = false;
bool inPause = false;

String lgu_all_topic;
String admin_topic = "drm/admin/all";
String command_topic;

String serialCommandBuffer = "";
StaticJsonDocument<512> mqttDoc;

// ============================================
// RGB LED CONTROL
// ============================================
void setRGB(CRGB color)
{
  leds[0] = color;
  FastLED.setBrightness(RGB_BRIGHTNESS);
  FastLED.show();
}

// ============================================
// CONFIGURATION STORAGE (SPIFFS)
// ============================================
void loadConfig()
{
  if (!SPIFFS.begin(true))
  {
    Serial.println("⚠️ SPIFFS Mount Failed");
    return;
  }

  if (SPIFFS.exists("/config.json"))
  {
    File file = SPIFFS.open("/config.json", "r");
    if (file)
    {
      StaticJsonDocument<1024> doc;
      deserializeJson(doc, file);

      strcpy(config.wifi_ssid, doc["wifi_ssid"] | "");
      strcpy(config.wifi_password, doc["wifi_password"] | "");
      strcpy(config.mqtt_broker, doc["mqtt_broker"] | "da4b7dd506824d31ba0940e25eb20f42.s1.eu.hivemq.cloud");
      strcpy(config.mqtt_port, doc["mqtt_port"] | "8883");
      strcpy(config.mqtt_username, doc["mqtt_username"] | "Admin");
      strcpy(config.mqtt_password, doc["mqtt_password"] | "Admin12345");
      strcpy(config.location, doc["location"] | "");
      strcpy(config.site_id, doc["site_id"] | "");
      strcpy(config.node_type, doc["node_type"] | "river");
      strcpy(config.admin_password, doc["admin_password"] | "Admin12345");
      config.configured = doc["configured"] | false;

      file.close();
      Serial.println("✅ Configuration loaded from SPIFFS");
    }
  }
  else
  {
    Serial.println("⚠️ No valid configuration found, using defaults");
  }
}

void saveConfig()
{
  StaticJsonDocument<1024> doc;
  doc["wifi_ssid"] = config.wifi_ssid;
  doc["wifi_password"] = config.wifi_password;
  doc["mqtt_broker"] = config.mqtt_broker;
  doc["mqtt_port"] = config.mqtt_port;
  doc["mqtt_username"] = config.mqtt_username;
  doc["mqtt_password"] = config.mqtt_password;
  doc["location"] = config.location;
  doc["site_id"] = config.site_id;
  doc["node_type"] = config.node_type;
  doc["admin_password"] = config.admin_password;
  doc["configured"] = config.configured;

  File file = SPIFFS.open("/config.json", "w");
  if (file)
  {
    serializeJson(doc, file);
    file.close();
    Serial.println("✅ Configuration saved to SPIFFS");
  }
}

void factoryReset()
{
  if (SPIFFS.exists("/config.json"))
  {
    SPIFFS.remove("/config.json");
  }
  Serial.println("⚠️ FACTORY RESET COMPLETE - Rebooting...");
  delay(500);
  ESP.restart();
}

// ============================================
// LED PATTERN CONTROL (RGB VERSION)
// ============================================
void setLedPattern(const LedPattern *pattern, bool temporary, unsigned long duration)
{
  if (temporary)
  {
    patternOverride = true;
    patternOverrideEnd = millis() + duration;
  }
  else
  {
    basePattern = pattern;
  }
  currentPattern = pattern;
  blinkCount = 0;
  lastBlinkChange = millis();
  ledState = false;
  inPause = false;
  setRGB(CRGB::Black);
}

void updateStatusLED()
{
  if (patternOverride && millis() > patternOverrideEnd)
  {
    patternOverride = false;
    currentPattern = basePattern;
    blinkCount = 0;
    lastBlinkChange = millis();
    ledState = false;
    inPause = false;
    setRGB(CRGB::Black);
  }

  unsigned long now = millis();
  const LedPattern *p = currentPattern;

  if (p->repeats == 0)
  {
    if (!inPause)
    {
      if (!ledState && (now - lastBlinkChange >= p->off_time))
      {
        ledState = true;
        setRGB(p->color);
        lastBlinkChange = now;
      }
      else if (ledState && (now - lastBlinkChange >= p->on_time))
      {
        ledState = false;
        setRGB(CRGB::Black);
        lastBlinkChange = now;
      }
    }
    return;
  }

  if (inPause)
  {
    if (now - lastBlinkChange >= p->pause)
    {
      inPause = false;
      blinkCount = 0;
      lastBlinkChange = now;
    }
    return;
  }

  if (blinkCount < p->repeats)
  {
    if (!ledState && (now - lastBlinkChange >= p->off_time))
    {
      ledState = true;
      setRGB(p->color);
      lastBlinkChange = now;
      if (p->off_time == 0)
        blinkCount++;
    }
    else if (ledState && (now - lastBlinkChange >= p->on_time))
    {
      ledState = false;
      setRGB(CRGB::Black);
      lastBlinkChange = now;
      blinkCount++;
    }
  }
  else
  {
    inPause = true;
    lastBlinkChange = now;
    setRGB(CRGB::Black);
    ledState = false;
  }
}

void ledSignalTelemetry() { setLedPattern(&PATTERN_TELEMETRY, true, 2000); }

void ledSignalAlert(AlertLevel level)
{
  switch (level)
  {
  case ALERT_YELLOW:
    setLedPattern(&PATTERN_ALERT_YELLOW);
    break;
  case ALERT_ORANGE:
    setLedPattern(&PATTERN_ALERT_ORANGE);
    break;
  case ALERT_RED:
    setLedPattern(&PATTERN_ALERT_RED);
    break;
  default:
    setLedPattern(&PATTERN_NORMAL);
    break;
  }
}

// ============================================
// MEMORY CHECK
// ============================================
int freeMemory()
{
  return ESP.getFreeHeap();
}

// ============================================
// NETWORK STATUS REPORT
// ============================================
void printNetworkStatus(Stream &output)
{
  output.println(F("\n╔══════════════════════════════════════╗"));
  output.println(F("║     DRM NETWORK METRICS REPORT      ║"));
  output.println(F("╚══════════════════════════════════════╝"));
  output.print(F("SSID Target:       "));
  output.println(config.wifi_ssid);
  output.print(F("Device IP:         "));
  output.println(WiFi.localIP());
  output.print(F("Subnet Mask:       "));
  output.println(WiFi.subnetMask());
  output.print(F("Gateway:           "));
  output.println(WiFi.gatewayIP());
  output.print(F("WiFi RSSI:         "));
  output.print(WiFi.RSSI());
  output.println(F(" dBm"));
  output.print(F("MQTT Broker:       "));
  output.print(config.mqtt_broker);
  output.print(F(":"));
  output.println(config.mqtt_port);
  output.print(F("MQTT Status:       "));
  output.println(mqtt.connected() ? F("CONNECTED") : F("OFFLINE"));
  output.print(F("Alert Level:       "));
  output.println(currentAlert);
  output.print(F("Free Memory:       "));
  output.print(freeMemory());
  output.println(F(" bytes"));
  output.println(F("══════════════════════════════════════"));
}

// ============================================
// TELNET REMOTE CONSOLE
// ============================================
void setupTelnet()
{
  telnetServer.begin();
  Serial.print(F("🔧 Telnet console ready on port 2323 -> nc <IP> "));
  Serial.print(WiFi.localIP());
  Serial.println(F(" 2323"));
}

void sendTelnetBanner()
{
  telnetClient.println(F("\n╔══════════════════════════════════════╗"));
  telnetClient.println(F("║   DRM FLOOD MONITOR v4.0 (S3)      ║"));
  telnetClient.print(F("║   Station: "));
  telnetClient.print(config.site_id);
  telnetClient.println(F("\t\t\t║"));
  telnetClient.print(F("║   Location: "));
  telnetClient.print(config.location);
  telnetClient.println(F("\t\t║"));
  telnetClient.println(F("╚══════════════════════════════════════╝"));
  telnetClient.println(F("AUTHENTICATION REQUIRED\nType: auth <password>"));
  telnetClient.print(F("> "));
}

void handleTelnetConnection()
{
  if (telnetClient && !telnetClient.connected())
  {
    telnetClient.stop();
    telnetSessionActive = false;
    telnetAuthenticated = false;
    telnetCommandBuffer = "";
    telnetClient = WiFiClient();
    Serial.println(F("🔌 Telnet client connection dropped. Buffer flushed."));
  }

  if (!telnetClient || !telnetClient.connected())
  {
    WiFiClient newClient = telnetServer.available();
    if (newClient)
    {
      telnetClient = newClient;
      telnetSessionActive = true;
      telnetAuthenticated = false;
      telnetSessionStart = millis();
      telnetLastActivity = millis();
      telnetFailedAttempts = 0;
      telnetCommandBuffer = "";
      sendTelnetBanner();
    }
  }
  else
  {
    WiFiClient rogueClient = telnetServer.available();
    if (rogueClient)
    {
      rogueClient.println(F("BUSY: Another administrator is connected"));
      rogueClient.stop();
    }
  }

  if (telnetClient && telnetClient.connected())
  {
    if (!telnetAuthenticated && (millis() - telnetSessionStart > TELNET_AUTH_TIMEOUT))
    {
      telnetClient.println(F("\n⏰ Authentication timeout. Disconnecting."));
      telnetClient.stop();
      return;
    }

    if (telnetAuthenticated && (millis() - telnetLastActivity > TELNET_IDLE_TIMEOUT))
    {
      telnetClient.println(F("\n⏰ Session timeout (5 min idle). Disconnecting."));
      telnetClient.stop();
      return;
    }

    while (telnetClient.available())
    {
      char c = telnetClient.read();
      telnetLastActivity = millis();

      if (c == '\n' || c == '\r')
      {
        if (telnetCommandBuffer.length() > 0)
        {
          telnetCommandBuffer.trim();
          if (telnetCommandBuffer.length() > MAX_TELNET_CMD_LENGTH)
          {
            telnetCommandBuffer = telnetCommandBuffer.substring(0, MAX_TELNET_CMD_LENGTH);
            telnetClient.println(F("\n⚠️ Command truncated to 128 chars"));
          }
          if (!telnetAuthenticated)
            handleTelnetAuth(telnetCommandBuffer);
          else
          {
            telnetClient.print(F("> "));
            processCommand(telnetCommandBuffer, telnetClient);
          }
          telnetCommandBuffer = "";
        }
      }
      else if (c == 127 || c == 8)
      {
        if (telnetCommandBuffer.length() > 0)
        {
          telnetCommandBuffer.remove(telnetCommandBuffer.length() - 1);
          telnetClient.write(8);
          telnetClient.write(' ');
          telnetClient.write(8);
        }
      }
      else
      {
        if (telnetCommandBuffer.length() < MAX_TELNET_CMD_LENGTH)
        {
          telnetCommandBuffer += c;
          telnetClient.write(c);
        }
      }
    }
  }
}

void handleTelnetAuth(String &cmd)
{
  if (cmd.startsWith("auth "))
  {
    String password = cmd.substring(5);
    password.trim();
    if (password.equals(config.admin_password))
    {
      telnetAuthenticated = true;
      telnetFailedAttempts = 0;
      telnetClient.println(F("\n✅ Authentication successful. Type 'help'."));
      telnetClient.print(F("> "));
    }
    else
    {
      telnetFailedAttempts++;
      telnetClient.println(F("\n❌ Invalid password"));
      if (telnetFailedAttempts >= TELNET_MAX_FAILED_AUTH)
        telnetClient.stop();
      else
      {
        telnetClient.print(F("Attempts remaining: "));
        telnetClient.println(TELNET_MAX_FAILED_AUTH - telnetFailedAttempts);
        telnetClient.print(F("> "));
      }
    }
  }
  else
  {
    telnetClient.println(F("\n⚠️ Please authenticate first: auth <password>"));
    telnetClient.print(F("> "));
  }
}

//                                            ============================================
//                                                    UNIFIED COMMAND PROCESSOR
//                                            ============================================
void processCommand(String &cmd, Stream &output)
{
  cmd.trim();
  cmd.toLowerCase();
  if (cmd.length() == 0)
  {
    output.print(F("> "));
    return;
  }
  output.println();

  if (cmd == "help" || cmd == "?" || cmd == "h")
  {
    output.println(F("━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"));
    output.println(F("DRM SYSTEM MANAGER CLI:"));
    output.println(F("━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"));
    output.println(F("  status                          Show system details"));
    output.println(F("  restart/reboot                  Reboot core hardware"));
    output.println(F("  factory_reset                   Clear flash profiles"));
    output.println(F("  uptime                          Display formatted runtime"));
    output.println(F("  set_wifi <ssid> <p>             Connect local station"));
    output.println(F("  set_mqtt <host> <port> <usr> <pwd>"));
    output.println(F("  set_location/<set_loc> <name>   Set geographical anchor"));
    output.println(F("  set_site_id/<set_id> <id>       Deploy structural tag"));
    output.println(F("  set_node_type/<set_type>        <river|road>"));
    output.println(F("  set_admin <password>            Change admin password"));
    output.println(F("  read_probes                     Query water matrix"));
    output.println(F("  test_siren <level>              Trigger driver (yellow/orange/red)"));
    output.println(F("  telemetry_now/t                 Force encrypted burst"));
    output.println(F("  dump_config                     Print static memory map"));
    output.println(F("  network_diag                    Verbose network analyzer"));
    output.println(F("  stop/s                          Emergency Stop"));
    output.println(F("━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"));
  }
  else if (cmd == "status" || cmd == "network_diag")
  {
    output.println(F("\n=================================================="));
    output.println(F("         DRM FIELD NODE DIAGNOSTIC PROFILE        "));
    output.println(F("=================================================="));
    output.println(F("🤖 HARDWARE PROFILE:"));
    output.print(F("  Architecture Board : "));
    output.println(F("ESP32-S3"));
    output.print(F("  Target Node Type   : "));
    output.println(config.node_type);
    output.print(F("  Station Site ID    : "));
    output.println(config.site_id);
    output.print(F("  LGU Location Tag   : "));
    output.println(config.location);
    output.print(F("  Configured Status  : "));
    output.println(config.configured ? F("ACTIVE_STA") : F("UNINITIALIZED"));
    output.println();

    output.println(F("📡 NETWORK TRACE MATRIX:"));
    output.print(F("  Target Station SSID: "));
    output.println(config.wifi_ssid);
    output.print(F("  Assigned Local IP  : "));
    output.println(WiFi.localIP());
    output.print(F("  Subnet Boundary    : "));
    output.println(WiFi.subnetMask());
    output.print(F("  Gateway Link IP    : "));
    output.println(WiFi.gatewayIP());
    output.print(F("  Signal Layer (RSSI): "));
    output.print(WiFi.RSSI());
    output.println(F(" dBm"));
    output.println();

    output.println(F("📨 CLOUD MQTT AGGREGATOR TUNNEL:"));
    output.print(F("  Cluster Endpoint   : "));
    output.print(config.mqtt_broker);
    output.print(F(":"));
    output.println(config.mqtt_port);
    output.print(F("  Validation User    : "));
    output.println(config.mqtt_username);
    output.print(F("  State Engine Loop  : "));
    output.println(mqtt.connected() ? F("SECURE_OPERATIONAL") : F("DISCONNECTED_RETRY"));
    output.print(F("  Last Error Vector  : "));
    output.println(mqtt.state());
    output.println();

    output.println(F("🔌 PHYSICAL PERIPHERAL INPUT MATRIX:"));
    output.print(F("  LOW PROBE  (GPIO13): "));
    output.println(digitalRead(PROBE_LOW) == LOW ? F("🌊 ACTIVE_WET") : F("⬜ DRY_OPEN"));
    output.print(F("  MID PROBE  (GPIO12): "));
    output.println(digitalRead(PROBE_MID) == LOW ? F("🌊 ACTIVE_WET") : F("⬜ DRY_OPEN"));
    output.print(F("  HIGH PROBE (GPIO11): "));
    output.println(digitalRead(PROBE_HIGH) == LOW ? F("🌊 ACTIVE_WET") : F("⬜ DRY_OPEN"));
    output.print(F("  Siren Driver Output: "));
    output.println(sirenActive ? F("🔊 HOT_OUTPUT_ACTIVE") : F("🔇 COLD_IDLE"));
    output.println();

    output.println(F("⚙️ CORE RUNTIME PROFILES:"));
    unsigned long up = millis();
    int days = up / 86400000;
    int hours = (up % 86400000) / 3600000;
    int minutes = (up % 3600000) / 60000;
    int seconds = (up % 60000) / 1000;
    output.print(F("  Processor Uptime   : "));
    output.print(days);
    output.print(F("d "));
    output.print(hours);
    output.print(F("h "));
    output.print(minutes);
    output.print(F("m "));
    output.print(seconds);
    output.println(F("s"));
    output.print(F("  Stable State Guard : "));
    output.println(currentAlert);
    output.print(F("  Available Static RAM: "));
    output.print(freeMemory());
    output.println(F(" bytes (free_heap)"));
    output.println(F("==================================================\n"));
  }
  else if (cmd == "restart" || cmd == "reboot")
  {
    output.println(F("🔄 Executing soft hardware restart..."));
    delay(1000);
    ESP.restart();
  }
  else if (cmd == "factory_reset")
  {
    factoryReset();
  }
  else if (cmd == "uptime")
  {
    unsigned long up = millis();
    int days = up / 86400000;
    int hours = (up % 86400000) / 3600000;
    int minutes = (up % 3600000) / 60000;
    int seconds = (up % 60000) / 1000;
    output.print(F("Uptime Metrics: "));
    output.print(days);
    output.print(F("d "));
    output.print(hours);
    output.print(F("h "));
    output.print(minutes);
    output.print(F("m "));
    output.print(seconds);
    output.println(F("s"));
  }
  else if (cmd.startsWith("set_admin "))
  {
    String newPass = cmd.substring(10);
    newPass.trim();
    if (newPass.length() > 0 && newPass.length() < 32)
    {
      strcpy(config.admin_password, newPass.c_str());
      saveConfig();
      output.println(F("✅ Admin password updated"));
    }
    else
    {
      output.println(F("❌ Password must be 1-31 characters"));
    }
  }
  else if (cmd.startsWith("set_wifi "))
  {
    String params = cmd.substring(9);
    int spaceIdx = params.indexOf(' ');
    if (spaceIdx > 0)
    {
      strncpy(config.wifi_ssid, params.substring(0, spaceIdx).c_str(), sizeof(config.wifi_ssid) - 1);
      config.wifi_ssid[sizeof(config.wifi_ssid) - 1] = '\0';
      strncpy(config.wifi_password, params.substring(spaceIdx + 1).c_str(), sizeof(config.wifi_password) - 1);
      config.wifi_password[sizeof(config.wifi_password) - 1] = '\0';
      config.configured = true;
      saveConfig();
      output.println(F("✅ Profiles updated. Commencing link re-init..."));
      delay(500);
      ESP.restart();
    }
    else
    {
      output.println(F("❌ Syntax Violation. Usage: set_wifi <ssid> <password>"));
    }
  }
  else if (cmd.startsWith("set_mqtt "))
  {
    String params = cmd.substring(9);
    int sp1 = params.indexOf(' ');
    int sp2 = params.indexOf(' ', sp1 + 1);
    int sp3 = params.indexOf(' ', sp2 + 1);
    if (sp1 > 0 && sp2 > sp1 && sp3 > sp2)
    {
      strncpy(config.mqtt_broker, params.substring(0, sp1).c_str(), sizeof(config.mqtt_broker) - 1);
      config.mqtt_broker[sizeof(config.mqtt_broker) - 1] = '\0';
      strncpy(config.mqtt_port, params.substring(sp1 + 1, sp2).c_str(), sizeof(config.mqtt_port) - 1);
      config.mqtt_port[sizeof(config.mqtt_port) - 1] = '\0';
      strncpy(config.mqtt_username, params.substring(sp2 + 1, sp3).c_str(), sizeof(config.mqtt_username) - 1);
      config.mqtt_username[sizeof(config.mqtt_username) - 1] = '\0';
      strncpy(config.mqtt_password, params.substring(sp3 + 1).c_str(), sizeof(config.mqtt_password) - 1);
      config.mqtt_password[sizeof(config.mqtt_password) - 1] = '\0';
      saveConfig();
      output.println(F("✅ Core secure tunnel parameters updated."));
      delay(500);
      ESP.restart();
    }
    else
    {
      output.println(F("❌ Syntax Violation. Usage: set_mqtt <host> <port> <user> <pass>"));
    }
  }
  else if (cmd.startsWith("set_location ") || cmd.startsWith("set_loc "))
  {
    // Determine the offset based on which alias was matched
    int offset = cmd.startsWith("set_location ") ? 13 : 8;
    strncpy(config.location, cmd.substring(offset).c_str(), sizeof(config.location) - 1);
    config.location[sizeof(config.location) - 1] = '\0';
    saveConfig();
    output.print(F("Location anchor written: "));
    output.println(config.location);
  }
  else if (cmd.startsWith("set_site_id ") || cmd.startsWith("set_id "))
  {
    int offset = cmd.startsWith("set_site_id ") ? 12 : 7;
    strncpy(config.site_id, cmd.substring(offset).c_str(), sizeof(config.site_id) - 1);
    config.site_id[sizeof(config.site_id) - 1] = '\0';
    saveConfig();
    output.print(F("Site identity locked: "));
    output.println(config.site_id);
  }
  else if (cmd.startsWith("set_node_type ") || cmd == ("set_type"))
  {
    String type = cmd.substring(14);
    type.trim();
    if (type == "river" || type == "road")
    {
      strncpy(config.node_type, type.c_str(), sizeof(config.node_type) - 1);
      config.node_type[sizeof(config.node_type) - 1] = '\0';
      saveConfig();
      output.print(F("Node logic mask: "));
      output.println(config.node_type);
    }
    else
    {
      output.println(F("❌ Execution Error: Must be 'river' or 'road'"));
    }
  }
  else if (cmd == "read_probes")
  {
    output.print(F("LOW: "));
    output.print(digitalRead(PROBE_LOW) == LOW ? F("WET") : F("DRY"));
    output.print(F(" | MID: "));
    output.print(digitalRead(PROBE_MID) == LOW ? F("WET") : F("DRY"));
    output.print(F(" | HIGH: "));
    output.println(digitalRead(PROBE_HIGH) == LOW ? F("WET") : F("DRY"));
  }
  
  else if (cmd.startsWith("test_siren "))
  {
    String level = cmd.substring(11);
    level.trim();
    if (level == "yellow")
      startSiren(ALERT_YELLOW);
    else if (level == "orange")
      startSiren(ALERT_ORANGE);
    else if (level == "red")
      startSiren(ALERT_RED);
    else
      output.println(F("❌ Use parameters: yellow, orange, or red"));
  }


  else if (cmd == "telemetry_now" || cmd == "t")
  {
    sendTelemetry();
    output.println(F("📡 Manual Telemetry Sent."));
  }
  
  else if (cmd == "dump_config")
  {
    output.print(F("SSID Location: "));
    output.println(config.wifi_ssid);
    output.print(F("Broker Endpoint: "));
    output.println(config.mqtt_broker);
    output.print(F("Validation User: "));
    output.println(config.mqtt_username);
    output.print(F("LGU Identifier: "));
    output.println(config.location);
    output.print(F("Site Map ID: "));
    output.println(config.site_id);
    output.print(F("Node Profiler: "));
    output.println(config.node_type);
    output.print(F("Admin Password: "));
    output.println("********");
  }

  else if (cmd == "stop" || cmd == "s")
  {
    output.println(F("🛑 EMERGENCY STOP COMMAND RECEIVED"));

    // 1. Immediately kill the physical siren output
    sirenActive = false;
    sirenOn = false;
    sirenCyclesRemaining = 0;
    digitalWrite(SIREN_PIN, LOW);
    output.println(F("  -> Siren hardware driver forced COLD_IDLE"));

    // 2. Extinguish all strobe lights
    activeStrobeLevel = ALERT_NONE;
    strobeState = false;
    digitalWrite(STROBE_GREEN, LOW);
    digitalWrite(STROBE_ORANGE, LOW);
    digitalWrite(STROBE_RED, LOW);
    output.println(F("  -> Strobe matrix forced OFF"));

    // 3. Reset internal alert state machine engines
    currentAlert = ALERT_NONE;
    targetAlert = ALERT_NONE;
    stableSampleCount = 0;

    // 4. Reset Status LED back to normal operating pattern
    setLedPattern(&PATTERN_NORMAL);

    output.println(F("✅ System state engine forced to nominal standby."));
  }

  else
  {
    output.println(F("❌ Command rejected. Query 'help' for configuration maps."));
  }
  output.print(F("> "));
}

// ============================================
// ASYNC CORE NETWORK ENGINE
// ============================================
void handleWiFi()
{
  uint8_t current_status = WiFi.status();
  unsigned long now = millis();

  if (current_status == WL_CONNECTED)
  {
    if (!wifi_connected)
    {
      wifi_connected = true;
      wifiConnecting = false;
      connectionRetryCount = 0;
      lastWifiAttempt = 0;
      Serial.println(F("\n✅ Wireless Link Secured."));
      printNetworkStatus(Serial);
      setupTelnet();
      setLedPattern(&PATTERN_NORMAL);
    }
    return;
  }

  if (wifi_connected && current_status != WL_CONNECTED)
  {
    wifi_connected = false;
    wifiConnecting = false;
    lastWifiAttempt = now;
    Serial.println(F("⚠️ Link Severed. Entering Async Recovery state..."));
    setLedPattern(&PATTERN_NO_WIFI);
    return;
  }

  if (!wifi_connected && !wifiConnecting)
  {
    if (strlen(config.wifi_ssid) == 0)
      return;

    if (now - lastWifiAttempt >= WIFI_RECONNECT_DELAY_MS)
    {
      lastWifiAttempt = now;
      connectionRetryCount++;

      if (connectionRetryCount > maxRetries)
      {
        Serial.println(F("💀 Multi-Attempt Loop Collapse. Resetting architecture hardware vectors..."));
        delay(500);
        ESP.restart();
      }

      Serial.print(F("🔄 Outbound frame trace fired. Reconnection: "));
      Serial.print(connectionRetryCount);
      Serial.print(F("/"));
      Serial.println(maxRetries);
      WiFi.begin(config.wifi_ssid, config.wifi_password);
      wifiConnecting = true;
      wifiConnectStart = now;
      setLedPattern(&PATTERN_CONNECTING);
    }
    return;
  }

  if (wifiConnecting)
  {
    if (now - wifiConnectStart > WIFI_CONNECT_TIMEOUT_MS)
    {
      wifiConnecting = false;
      lastWifiAttempt = now;
      Serial.println(F("❌ Handshake Timeout. Flushing interface cache..."));
      setLedPattern(&PATTERN_NO_WIFI);
      WiFi.disconnect();
    }
  }
}

void mqttCallback(char *topic, byte *payload, unsigned int length)
{
  char msg[128];
  unsigned int copyLen = (length > 127) ? 127 : length;
  memcpy(msg, payload, copyLen);
  msg[copyLen] = 0;
  if (String(topic) == command_topic)
  {
    String msgStr = String(msg);
    processCommand(msgStr, Serial);
  }
}

void connectMQTT()
{
  if (WiFi.status() != WL_CONNECTED)
    return;
  if (mqtt.connected())
  {
    mqtt_connected = true;
    return;
  }
  if (millis() - lastMqttRetry < MQTT_RECONNECT_DELAY_MS)
    return;
  lastMqttRetry = millis();

  if (strlen(config.site_id) == 0 || strlen(config.location) == 0)
  {
    Serial.println(F("❌ MQTT Aborted: Location or Site ID is unconfigured."));
    return;
  }

  String clientId = "DRM_" + String(config.node_type) + "_" + String(config.site_id) + "_" + String(random(0xffff), HEX);
  mqtt.setBufferSize(1024);

  if (mqtt.connect(clientId.c_str(), config.mqtt_username, config.mqtt_password))
  {
    mqtt_connected = true;
    Serial.println(F("✅ Secure Encrypted Tunnel established with Cluster broker instance."));
    mqtt.subscribe(command_topic.c_str());
    mqtt.subscribe(admin_topic.c_str());
  }
  else
  {
    mqtt_connected = false;
    Serial.print(F("❌ MQTT Gateway Connection Aborted. Error Code: "));
    Serial.println(mqtt.state());
  }
}

// ============================================
// MESSAGE HANDLERS
// ============================================
String getMessage(AlertLevel level)
{
  bool isRoad = (strcmp(config.node_type, "road") == 0);

  if (isRoad)
  {
    switch (level)
    {
    case ALERT_YELLOW:
      return F("Standing water on road - Drive slowly");
    case ALERT_ORANGE:
      return F("High flooding - Light vehicles cannot pass");
    case ALERT_RED:
      return F("ROAD CLOSED - Impassable due to flooding");
    default:
      return F("Road clear - No flooding detected");
    }
  }
  else
  {
    switch (level)
    {
    case ALERT_YELLOW:
      return F("River level rising - Monitor closely");
    case ALERT_ORANGE:
      return F("River approaching critical level - Prepare evacuation");
    case ALERT_RED:
      return F("CRITICAL - River overflow expected - EVACUATE NOW");
    default:
      return F("River normal - No threat");
    }
  }
}

String getInstruction(AlertLevel level)
{
  bool isRoad = (strcmp(config.node_type, "road") == 0);

  if (isRoad)
  {
    switch (level)
    {
    case ALERT_YELLOW:
      return F("Reduce speed, use caution");
    case ALERT_ORANGE:
      return F("Only 4x4 vehicles allowed");
    case ALERT_RED:
      return F("ROAD CLOSED - Use alternate route");
    default:
      return F("Normal traffic flow");
    }
  }
  else
  {
    switch (level)
    {
    case ALERT_YELLOW:
      return F("Alert residents in flood zones");
    case ALERT_ORANGE:
      return F("Prepare evacuation centers");
    case ALERT_RED:
      return F("FORCED EVACUATION - Leave immediately");
    default:
      return F("No action required");
    }
  }
}

// ============================================
// MQTT & TELEMETRY
// ============================================
void sendMQTT(AlertLevel level, const char *eventType)
{
  if (!mqtt.connected())
    return;
  mqttDoc.clear();
  mqttDoc["sensor_type"] = config.node_type;
  mqttDoc["site_id"] = config.site_id;
  mqttDoc["location"] = config.location;
  mqttDoc["timestamp"] = millis();
  mqttDoc["event"] = eventType;

  const char *alertName = (level == ALERT_RED) ? "RED" : (level == ALERT_ORANGE) ? "ORANGE"
                                                     : (level == ALERT_YELLOW)   ? "YELLOW"
                                                                                 : "NORMAL";
  mqttDoc["alert"] = alertName;
  mqttDoc["severity"] = level;
  mqttDoc["message"] = getMessage(level);
  mqttDoc["instruction"] = getInstruction(level);
  mqttDoc["rssi"] = WiFi.RSSI();

  char buffer[512];
  serializeJson(mqttDoc, buffer);
  mqtt.publish(lgu_all_topic.c_str(), buffer);
  mqtt.publish(admin_topic.c_str(), buffer);
}

void sendTelemetry()
{
  if (!mqtt.connected())
    return;
  mqttDoc.clear();
  mqttDoc["type"] = "telemetry";
  mqttDoc["sensor_type"] = config.node_type;
  mqttDoc["site_id"] = config.site_id;
  mqttDoc["location"] = config.location;
  mqttDoc["uptime"] = millis() / 1000;
  mqttDoc["current_status"] = currentAlert;
  mqttDoc["rssi"] = WiFi.RSSI();
  mqttDoc["free_heap"] = freeMemory();

  const char *alertName = (currentAlert == ALERT_RED) ? "RED" : (currentAlert == ALERT_ORANGE) ? "ORANGE"
                                                            : (currentAlert == ALERT_YELLOW)   ? "YELLOW"
                                                                                               : "NORMAL";
  mqttDoc["alert"] = alertName;
  mqttDoc["message"] = getMessage(currentAlert);
  mqttDoc["instruction"] = getInstruction(currentAlert);

  char buffer[512];
  serializeJson(mqttDoc, buffer);
  mqtt.publish(lgu_all_topic.c_str(), buffer);
  mqtt.publish(admin_topic.c_str(), buffer);

  Serial.println(F("📡 Secure Telemetry Burst Dispatched."));
  ledSignalTelemetry();
}

//
//
//
//
//
//
//
//
//
//
//
//
// ============================================
// HARDWARE PERIPHERAL STEP CONTROLLERS
// ============================================
void updateStrobeLights()
{
  unsigned long now = millis();
  if (currentAlert != activeStrobeLevel)
  {
    activeStrobeLevel = currentAlert;
    strobeState = false;
    lastStrobeToggle = now;
    digitalWrite(STROBE_GREEN, LOW);
    digitalWrite(STROBE_ORANGE, LOW);
    digitalWrite(STROBE_RED, LOW);
  }
  int patternMs = 0;
  int pin = -1;
  switch (currentAlert)
  {
  case ALERT_YELLOW:
    patternMs = STROBE_L1_PATTERN_MS;
    pin = STROBE_GREEN;
    break;
  case ALERT_ORANGE:
    patternMs = STROBE_L2_PATTERN_MS;
    pin = STROBE_ORANGE;
    break;
  case ALERT_RED:
    patternMs = STROBE_L3_PATTERN_MS;
    pin = STROBE_RED;
    break;
  default:
    return;
  }
  if (now - lastStrobeToggle >= patternMs)
  {
    strobeState = !strobeState;
    lastStrobeToggle = now;
  }
  digitalWrite(pin, strobeState ? HIGH : LOW);
}

void handleSiren()
{
  if (!sirenActive)
    return;
  if (currentAlert == ALERT_RED)
  {
    digitalWrite(SIREN_PIN, HIGH);
    return;
  }
  unsigned long now = millis();
  if (!sirenOn)
  {
    if (now - lastSirenStep >= 1000)
    {
      digitalWrite(SIREN_PIN, HIGH);
      sirenOn = true;
      lastSirenStep = now;
    }
  }
  else
  {
    unsigned long duration = (currentAlert == ALERT_YELLOW) ? SIREN_L1_DURATION_MS : SIREN_L2_DURATION_MS;
    if (now - lastSirenStep >= duration)
    {
      digitalWrite(SIREN_PIN, LOW);
      sirenOn = false;
      sirenCyclesRemaining--;
      lastSirenStep = now;
      if (sirenCyclesRemaining <= 0)
        sirenActive = false;
    }
  }
}

void startSiren(AlertLevel level)
{
  if (level == ALERT_YELLOW)
    sirenCyclesRemaining = SIREN_L1_CYCLES;
  else if (level == ALERT_ORANGE)
    sirenCyclesRemaining = SIREN_L2_CYCLES;
  else if (level == ALERT_RED)
    sirenCyclesRemaining = SIREN_L3_CYCLES;
  else
    sirenCyclesRemaining = 0;

  sirenActive = (sirenCyclesRemaining > 0);
  sirenOn = false;
  lastSirenStep = millis();
  digitalWrite(SIREN_PIN, LOW);
}

AlertLevel readProbes()
{
  bool lowWet = (digitalRead(PROBE_LOW) == LOW);
  bool midWet = (digitalRead(PROBE_MID) == LOW);
  bool highWet = (digitalRead(PROBE_HIGH) == LOW);

  if (highWet)
    return ALERT_RED;
  if (midWet)
    return ALERT_ORANGE;
  if (lowWet)
    return ALERT_YELLOW;
  return ALERT_NONE;
}

void sampleWaterProbes()
{
  if (millis() - lastProbeSample < DEBOUNCE_MS)
    return;
  lastProbeSample = millis();
  AlertLevel newAlert = readProbes();
  if (newAlert != targetAlert)
  {
    targetAlert = newAlert;
    stableSampleCount = 0;
    return;
  }
  stableSampleCount++;
  if (stableSampleCount >= 10 && currentAlert != targetAlert)
  {
    currentAlert = targetAlert;
    stableSampleCount = 0;
    if (currentAlert != ALERT_NONE)
    {
      sendMQTT(currentAlert, "trigger");
      startSiren(currentAlert);
      lastContinuousSend = millis();
      ledSignalAlert(currentAlert);
    }
    else
    {
      sendMQTT(ALERT_NONE, "release");
      setLedPattern(&PATTERN_NORMAL);
    }
  }
}

void processSerialCommands()
{
  while (Serial.available() > 0)
  {
    char c = Serial.read();
    if (c == '\n' || c == '\r')
    {
      if (serialCommandBuffer.length() > 0)
      {
        serialCommandBuffer.trim();
        if (serialCommandBuffer.length() > MAX_SERIAL_CMD_LENGTH)
        {
          serialCommandBuffer = serialCommandBuffer.substring(0, MAX_SERIAL_CMD_LENGTH);
          Serial.println(F("\n⚠️ Command truncated to 128 chars"));
        }
        processCommand(serialCommandBuffer, Serial);
        serialCommandBuffer = "";
      }
    }
    else if (c == 127 || c == 8)
    {
      if (serialCommandBuffer.length() > 0)
      {
        serialCommandBuffer.remove(serialCommandBuffer.length() - 1);
      }
    }
    else
    {
      serialCommandBuffer += c;
    }
  }
}
//
//
//
//
//
//
//
//
//
//
//
//
//
//
//
//
//
//
//
//
//
//                                                    ============================================
//                                                               Setup & Loop
//                                                    ============================================
void setup()
{
  Serial.begin(115200);
  unsigned long start = millis();
  while (!Serial && (millis() - start < 2000))
  {
    delay(10);
  }

  // Initialize RGB LED
  FastLED.addLeds<LED_TYPE, RGB_LED_PIN, COLOR_ORDER>(leds, NUM_LEDS);
  FastLED.setBrightness(RGB_BRIGHTNESS);
  setLedPattern(&PATTERN_BOOTING, true, 3000);

  // Initialize pins
  pinMode(PROBE_LOW, INPUT_PULLUP);
  pinMode(PROBE_MID, INPUT_PULLUP);
  pinMode(PROBE_HIGH, INPUT_PULLUP);
  pinMode(STROBE_GREEN, OUTPUT);
  pinMode(STROBE_ORANGE, OUTPUT);
  pinMode(STROBE_RED, OUTPUT);
  pinMode(SIREN_PIN, OUTPUT);
  digitalWrite(STROBE_GREEN, LOW);
  digitalWrite(STROBE_ORANGE, LOW);
  digitalWrite(STROBE_RED, LOW);
  digitalWrite(SIREN_PIN, LOW);

  loadConfig();

  lgu_all_topic = "lgu/" + String(config.location) + "/all";
  command_topic = "drm/" + String(config.site_id) + "/command";
  admin_topic = "drm/admin/all";

  // Setup MQTT
  espClient.setInsecure();
  mqtt.setServer(config.mqtt_broker, atoi(config.mqtt_port));
  mqtt.setCallback(mqttCallback);

  if (!config.configured || strlen(config.wifi_ssid) == 0)
  {
    Serial.println(F("\n⚠️ STANDBY: Local configuration uninitialized. Use Serial to configure."));
    Serial.println(F("   Connect via: telnet <IP> 2323"));
    Serial.println(F("   Or use serial commands: set_wifi <ssid> <password>"));
    setLedPattern(&PATTERN_CONFIG_MODE);
  }
  else
  {
    Serial.println(F("📡 Attempting WiFi connection..."));
    WiFi.begin(config.wifi_ssid, config.wifi_password);
    wifiConnecting = true;
    wifiConnectStart = millis();
    setLedPattern(&PATTERN_CONNECTING);
  }
  Serial.print(F("\n> "));
}

void loop()
{
  processSerialCommands();
  updateStatusLED();
  handleTelnetConnection();

  handleWiFi();

  if (!wifi_connected)
  {
    delay(1);
    return;
  }

  if (!mqtt.connected())
  {
    connectMQTT();
  }
  else
  {
    mqtt_connected = true;
    mqtt.loop();
  }

  sampleWaterProbes();
  handleSiren();
  updateStrobeLights();

  unsigned long cur = millis();
  if (mqtt.connected() && (cur - lastTelemetry > TELEMETRY_INTERVAL_MS))
  {
    sendTelemetry();
    lastTelemetry = cur;
  }
  if (currentAlert != ALERT_NONE && mqtt.connected() && (cur - lastContinuousSend > CONTINUOUS_INTERVAL_MS))
  {
    sendMQTT(currentAlert, "continuous");
    lastContinuousSend = cur;
  }
  delay(1);
}