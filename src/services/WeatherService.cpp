#include "WeatherService.h"
#include "../CrossPointSettings.h"
#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <HardwareSerial.h>
#include <WiFi.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

WeatherService WeatherService::instance;

// Define static constexpr array
constexpr WeatherService::CityData WeatherService::CITIES[63];

WeatherService &WeatherService::getInstance() { return instance; }

const char *WeatherService::getCityName(uint8_t cityIndex) {
  if (cityIndex >= 63)
    return "Unknown";
  return CITIES[cityIndex].name;
}

uint8_t WeatherService::getCityCount() { return 63; }

void WeatherService::refresh() {
  if (refreshTaskHandle != nullptr) {
    Serial.println("[Weather] Refresh already in progress");
    return;
  }

  xTaskCreate(
      [](void *param) {
        auto *self = static_cast<WeatherService *>(param);
        if (WiFi.status() == WL_CONNECTED) {
          uint8_t cityIndex = SETTINGS.weatherCityIndex;
          if (cityIndex >= 63) {
            Serial.println("[Weather] Invalid city index");
            self->refreshTaskHandle = nullptr;
            vTaskDelete(NULL);
            return;
          }

          const auto &city = CITIES[cityIndex];
          HTTPClient http;

          // Open-Meteo API endpoint
          char url[256];
          snprintf(
              url, sizeof(url),
              "http://api.open-meteo.com/v1/"
              "forecast?latitude=%.2f&longitude=%.2f&current=temperature_2m",
              city.lat, city.lon);

          Serial.printf("[Weather] Fetching from: %s\n", url);
          http.begin(url);
          http.setTimeout(5000);
          int httpCode = http.GET();

          if (httpCode == HTTP_CODE_OK) {
            String payload = http.getString();

            // Parse JSON response
            JsonDocument doc;
            DeserializationError error = deserializeJson(doc, payload);

            if (!error) {
              float temp = doc["current"]["temperature_2m"];

              // Format: "25.5°C"
              snprintf(SETTINGS.lastWeatherText,
                       sizeof(SETTINGS.lastWeatherText), "%.1f°C", temp);

              SETTINGS.saveToFile();
              Serial.printf("[Weather] Updated: %s - %s\n", city.name,
                            SETTINGS.lastWeatherText);
            } else {
              Serial.printf("[Weather] JSON parse error: %s\n", error.c_str());
            }
          } else {
            Serial.printf("[Weather] HTTP error: %d\n", httpCode);
          }
          http.end();
        }
        self->refreshTaskHandle = nullptr;
        vTaskDelete(NULL);
      },
      "WeatherRefresh", 8192, this, 1, &refreshTaskHandle);
}

std::string WeatherService::getCachedWeather() const {
  if (strlen(SETTINGS.lastWeatherText) == 0)
    return "";
  return std::string(getCityName(SETTINGS.weatherCityIndex)) + ": " +
         std::string(SETTINGS.lastWeatherText);
}
