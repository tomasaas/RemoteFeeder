#include <Arduino.h>
#include <Preferences.h>
#include <PubSubClient.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <time.h>

#include "FeederController.h"
#include "config.h"

#if __has_include("secrets.h")
#include "secrets.h"
#else
#error "Copy include/secrets.example.h to include/secrets.h and add your credentials."
#endif

WiFiClientSecure secureClient;
PubSubClient mqttClient(secureClient);
Preferences preferences;
FeederController feeder;

String lastMessageId;
String activeMessageId;
String lastResult = "none";
String mqttClientId;

uint32_t nextWifiAttempt = 0;
uint32_t nextMqttAttempt = 0;
bool timeSyncStarted = false;

bool retryDue(uint32_t now, uint32_t scheduledAt) {
  return static_cast<int32_t>(now - scheduledAt) >= 0;
}

bool validMessageId(const String& messageId) {
  if (messageId.isEmpty() || messageId.length() > 64) {
    return false;
  }

  for (size_t i = 0; i < messageId.length(); ++i) {
    const char character = messageId[i];
    if (!isalnum(static_cast<unsigned char>(character)) &&
        character != '-' && character != '_') {
      return false;
    }
  }

  return true;
}

void publishStatus(
    const char* state,
    const char* result,
    const String& messageId = "",
    const String& rejectedMessageId = "") {
  if (!mqttClient.connected()) {
    return;
  }

  char payload[384];
  snprintf(
      payload,
      sizeof(payload),
      "{\"state\":\"%s\",\"result\":\"%s\",\"message_id\":\"%s\","
      "\"rejected_message_id\":\"%s\",\"uptime_s\":%lu,\"rssi\":%ld}",
      state,
      result,
      messageId.c_str(),
      rejectedMessageId.c_str(),
      millis() / 1000UL,
      static_cast<long>(WiFi.RSSI()));

  mqttClient.publish(config::MQTT_STATUS_TOPIC, payload, true);
  Serial.printf("Status: %s\n", payload);
}

void handleFeedCommand(const String& messageId) {
  if (!validMessageId(messageId)) {
    publishStatus(
        feeder.isBusy() ? "feeding" : "ready",
        "invalid_message_id",
        activeMessageId);
    return;
  }

  if (messageId == lastMessageId) {
    publishStatus(
        feeder.isBusy() ? "feeding" : "ready",
        "duplicate",
        messageId);
    return;
  }

  if (feeder.isBusy()) {
    publishStatus("feeding", "busy", activeMessageId, messageId);
    return;
  }

  lastMessageId = messageId;
  activeMessageId = messageId;
  lastResult = "in_progress";
  preferences.putString("last_id", lastMessageId);
  preferences.putString("last_result", lastResult);

  if (!feeder.startFeed()) {
    activeMessageId = "";
    lastResult = "failed";
    preferences.putString("last_result", lastResult);
    publishStatus("error", "servo_attach_failed", lastMessageId);
    return;
  }

  publishStatus("feeding", "accepted", activeMessageId);
}

void mqttMessageReceived(char* topic, byte* payload, unsigned int length) {
  if (strcmp(topic, config::MQTT_COMMAND_TOPIC) != 0) {
    return;
  }

  String messageId;
  messageId.reserve(length);

  for (unsigned int i = 0; i < length; ++i) {
    messageId += static_cast<char>(payload[i]);
  }

  messageId.trim();
  handleFeedCommand(messageId);
}

void connectWifi(uint32_t now) {
  if (WiFi.status() == WL_CONNECTED || !retryDue(now, nextWifiAttempt)) {
    return;
  }

  Serial.printf("Connecting to Wi-Fi: %s\n", secrets::WIFI_SSID);
  WiFi.begin(secrets::WIFI_SSID, secrets::WIFI_PASSWORD);
  nextWifiAttempt = now + config::WIFI_RETRY_MS;
}

bool clockIsReady() {
  return time(nullptr) > 1700000000;
}

void startTimeSync() {
  if (timeSyncStarted) {
    return;
  }

  configTime(0, 0, "pool.ntp.org", "time.cloudflare.com");
  timeSyncStarted = true;
  Serial.println("Synchronizing clock for TLS...");
}

void connectMqtt(uint32_t now) {
  if (mqttClient.connected() ||
      WiFi.status() != WL_CONNECTED ||
      !clockIsReady() ||
      feeder.isBusy() ||
      !retryDue(now, nextMqttAttempt)) {
    return;
  }

  nextMqttAttempt = now + config::MQTT_RETRY_MS;
  Serial.printf("Connecting to MQTT broker: %s\n", config::MQTT_HOST);

  const bool connected = mqttClient.connect(
      mqttClientId.c_str(),
      secrets::MQTT_USERNAME,
      secrets::MQTT_PASSWORD,
      config::MQTT_STATUS_TOPIC,
      1,
      true,
      "{\"state\":\"offline\"}");

  if (!connected) {
    Serial.printf("MQTT connection failed, state=%d\n", mqttClient.state());
    return;
  }

  mqttClient.subscribe(config::MQTT_COMMAND_TOPIC, 1);
  publishStatus("ready", lastResult.c_str(), lastMessageId);
  Serial.println("MQTT connected.");
}

void setup() {
  Serial.begin(115200);
  delay(200);

  preferences.begin("dog-feeder", false);
  lastMessageId = preferences.getString("last_id", "");
  lastResult = preferences.getString("last_result", "none");

  if (lastResult == "in_progress") {
    lastResult = "interrupted";
    preferences.putString("last_result", lastResult);
  }

  feeder.begin();

  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);
  WiFi.persistent(false);

  const uint64_t chipId = ESP.getEfuseMac();
  mqttClientId = config::DEVICE_NAME;
  mqttClientId += "-";
  mqttClientId += String(static_cast<uint32_t>(chipId), HEX);

  secureClient.setCACert(config::ROOT_CA);
  secureClient.setHandshakeTimeout(10);
  secureClient.setTimeout(3);

  mqttClient.setServer(config::MQTT_HOST, config::MQTT_PORT);
  mqttClient.setCallback(mqttMessageReceived);
  mqttClient.setKeepAlive(30);
  mqttClient.setSocketTimeout(3);
  mqttClient.setBufferSize(512);

  connectWifi(millis());
}

void loop() {
  feeder.update();

  if (feeder.consumeCompleted()) {
    lastResult = "completed";
    preferences.putString("last_result", lastResult);
    publishStatus("ready", lastResult.c_str(), activeMessageId);
    activeMessageId = "";
  }

  const uint32_t now = millis();
  connectWifi(now);

  if (WiFi.status() == WL_CONNECTED) {
    startTimeSync();
  }

  if (mqttClient.connected()) {
    mqttClient.loop();
  } else {
    connectMqtt(now);
  }

  delay(5);
}
