#include <Arduino.h>
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

class ReliableWiFiClientSecure : public WiFiClientSecure {
 public:
  size_t write(uint8_t data) override {
    return write(&data, 1);
  }

  size_t write(const uint8_t* buffer, size_t size) override {
    size_t totalWritten = 0;
    const uint32_t startedAt = millis();

    while (totalWritten < size && connected()) {
      const size_t written =
          WiFiClientSecure::write(buffer + totalWritten, size - totalWritten);

      if (written > 0) {
        totalWritten += written;
      } else if (millis() - startedAt >= 15000) {
        break;
      } else {
        delay(1);
      }
    }

    return totalWritten;
  }
};

ReliableWiFiClientSecure secureClient;
PubSubClient mqttClient(secureClient);
FeederController feeder;

String activeMessageId;
String lastResult = "none";
String mqttClientId;

uint32_t nextWifiAttempt = 0;
uint32_t nextMqttAttempt = 0;
bool timeSyncStarted = false;

bool retryDue(uint32_t now, uint32_t scheduledAt) {
  return static_cast<int32_t>(now - scheduledAt) >= 0;
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
      "\"rejected_message_id\":\"%s\",\"manual\":%s,\"position\":%d,"
      "\"uptime_s\":%lu,\"rssi\":%ld}",
      state,
      result,
      messageId.c_str(),
      rejectedMessageId.c_str(),
      feeder.isManual() ? "true" : "false",
      feeder.manualPosition(),
      millis() / 1000UL,
      static_cast<long>(WiFi.RSSI()));

  mqttClient.publish(config::MQTT_STATUS_TOPIC, payload, true);
  Serial.printf("Status: %s\n", payload);
}

void handleFeedCommand(const String& payload) {
  if (payload != "1") {
    publishStatus(
        feeder.isFeeding() ? "feeding" : "ready",
        "invalid_feed_value",
        activeMessageId);
    return;
  }

  if (feeder.isManual()) {
    publishStatus("manual", "manual_mode", activeMessageId, payload);
    return;
  }

  if (feeder.isBusy()) {
    publishStatus(
        feeder.isFeeding() ? "feeding" : "returning",
        "busy",
        activeMessageId,
        payload);
    return;
  }

  activeMessageId = payload;
  lastResult = "in_progress";

  if (!feeder.startFeed()) {
    lastResult = "failed";
    publishStatus("error", "servo_attach_failed", activeMessageId);
    activeMessageId = "";
    return;
  }

  publishStatus("feeding", "accepted", activeMessageId);
}

void handleManualCommand(const String& payload) {
  if (payload == "1") {
    if (feeder.isManual()) {
      publishStatus("manual", "already_enabled");
      return;
    }

    if (feeder.isFeeding()) {
      lastResult = "manual_override";
      activeMessageId = "";
    }

    if (!feeder.enableManual()) {
      publishStatus("error", "servo_attach_failed");
      return;
    }

    publishStatus("manual", "enabled");
    return;
  }

  if (payload == "0") {
    if (feeder.disableManual()) {
      publishStatus("returning", "manual_disabled");
    } else {
      publishStatus(
          feeder.isFeeding() ? "feeding" : "ready",
          "already_automatic",
          feeder.isFeeding() ? activeMessageId : "");
    }
    return;
  }

  publishStatus(
      feeder.isManual() ? "manual" : (feeder.isFeeding() ? "feeding" : "ready"),
      "invalid_manual_value",
      feeder.isFeeding() ? activeMessageId : "");
}

void handlePositionCommand(const String& payload) {
  char* end = nullptr;
  const long position = strtol(payload.c_str(), &end, 10);

  if (payload.isEmpty() || *end != '\0' || position < -60 || position > 60) {
    publishStatus(
        feeder.isManual() ? "manual" : (feeder.isFeeding() ? "feeding" : "ready"),
        "invalid_position",
        feeder.isFeeding() ? activeMessageId : "");
    return;
  }

  if (!feeder.setManualPosition(static_cast<int8_t>(position))) {
    publishStatus(
        feeder.isFeeding() ? "feeding" : "ready",
        "manual_disabled",
        feeder.isFeeding() ? activeMessageId : "");
    return;
  }

  publishStatus("manual", "position_set");
}

void mqttMessageReceived(char* topic, byte* payload, unsigned int length) {
  String message;
  message.reserve(length);

  for (unsigned int i = 0; i < length; ++i) {
    message += static_cast<char>(payload[i]);
  }

  message.trim();

  if (strcmp(topic, config::MQTT_COMMAND_TOPIC) == 0) {
    handleFeedCommand(message);
  } else if (strcmp(topic, config::MQTT_MANUAL_TOPIC) == 0) {
    handleManualCommand(message);
  } else if (strcmp(topic, config::MQTT_POSITION_TOPIC) == 0) {
    handlePositionCommand(message);
  }
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
      feeder.isMoving() ||
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
    char tlsError[128] = {};
    const int tlsErrorCode = secureClient.lastError(tlsError, sizeof(tlsError));

    if (tlsErrorCode < 0) {
      Serial.printf(
          "Wi-Fi RSSI=%ld dBm, TLS=%d (%s)\n",
          static_cast<long>(WiFi.RSSI()),
          tlsErrorCode,
          tlsError);
    } else {
      Serial.printf(
          "Wi-Fi RSSI=%ld dBm, TLS connected (socket=%d), MQTT timed out\n",
          static_cast<long>(WiFi.RSSI()),
          tlsErrorCode);
    }
    return;
  }

  mqttClient.subscribe(config::MQTT_MANUAL_TOPIC, 1);
  mqttClient.subscribe(config::MQTT_POSITION_TOPIC, 1);
  mqttClient.subscribe(config::MQTT_COMMAND_TOPIC, 0);
  publishStatus(
      feeder.isManual() ? "manual" : "ready",
      lastResult.c_str());
  Serial.println("MQTT connected.");
}

void setup() {
  Serial.begin(115200);
  delay(200);

  feeder.begin();

  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);
  WiFi.persistent(false);

  const uint64_t chipId = ESP.getEfuseMac();
  mqttClientId = config::DEVICE_NAME;
  mqttClientId += "-";
  mqttClientId += String(static_cast<uint32_t>(chipId), HEX);

  secureClient.setCACert(config::ROOT_CA);
  secureClient.setHandshakeTimeout(20);
  secureClient.setTimeout(15);

  mqttClient.setServer(config::MQTT_HOST, config::MQTT_PORT);
  mqttClient.setCallback(mqttMessageReceived);
  mqttClient.setKeepAlive(30);
  mqttClient.setSocketTimeout(15);
  mqttClient.setBufferSize(512);

  connectWifi(millis());
}

void loop() {
  feeder.update();

  if (feeder.consumeCompleted()) {
    lastResult = "completed";
    publishStatus("ready", lastResult.c_str(), activeMessageId);
    activeMessageId = "";
  }

  if (feeder.consumeManualReturnCompleted()) {
    publishStatus("ready", "manual_disabled");
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
