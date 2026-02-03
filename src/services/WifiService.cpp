#include "WifiService.h"
#include "../WifiCredentialStore.h"
#include "WeatherService.h"
#include <HardwareSerial.h>
#include <WiFi.h>

WifiService WifiService::instance;

WifiService &WifiService::getInstance() { return instance; }

void WifiService::begin() {
  WiFi.mode(WIFI_STA);
  Serial.println("[WiFi] Service initialized");
}

void WifiService::autoConnect() {
  if (autoConnectAttempted) {
    Serial.println("[WiFi] Auto-connect already attempted");
    return;
  }

  autoConnectAttempted = true;

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("[WiFi] Already connected");
    return;
  }

  // Load saved credentials
  WIFI_STORE.loadFromFile();

  const auto &credentials = WIFI_STORE.getCredentials();

  if (credentials.empty()) {
    Serial.println("[WiFi] No saved credentials");
    return;
  }

  Serial.printf("[WiFi] Attempting auto-connect with %d saved networks\n",
                credentials.size());

  // Try each saved credential
  for (const auto &cred : credentials) {
    Serial.printf("[WiFi] Trying to connect to: %s\n", cred.ssid.c_str());

    WiFi.begin(cred.ssid.c_str(), cred.password.c_str());

    // Wait up to 10 seconds for connection
    unsigned long startTime = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - startTime < 10000) {
      delay(100);
    }

    if (WiFi.status() == WL_CONNECTED) {
      Serial.printf("[WiFi] Connected to: %s\n", cred.ssid.c_str());
      Serial.printf("[WiFi] IP: %s\n", WiFi.localIP().toString().c_str());

      // Trigger weather refresh after successful connection
      WeatherService::getInstance().refresh();
      return;
    } else {
      Serial.printf("[WiFi] Failed to connect to: %s\n", cred.ssid.c_str());
      WiFi.disconnect();
    }
  }

  Serial.println("[WiFi] Auto-connect failed for all saved networks");
}

bool WifiService::isConnected() const { return WiFi.status() == WL_CONNECTED; }
