#pragma once
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <string>

class WeatherService {
public:
  static WeatherService &getInstance();

  // Refresh weather data if WiFi is connected
  void refresh();

  // Get cached weather info
  std::string getCachedWeather() const;

  // Get city name by index
  static const char *getCityName(uint8_t cityIndex);

  // Get total city count
  static uint8_t getCityCount();

private:
  WeatherService() = default;
  static WeatherService instance;
  TaskHandle_t refreshTaskHandle = nullptr;

  // City data structure
  struct CityData {
    const char *name;
    float lat;
    float lon;
  };

  // 63 Vietnamese provinces
  static constexpr CityData CITIES[63] = {
      {"An Giang", 10.51f, 105.12f},       {"Ba Ria Vung Tau", 10.54f, 107.24f},
      {"Bac Lieu", 9.29f, 105.72f},        {"Bac Kan", 22.14f, 105.83f},
      {"Bac Giang", 21.27f, 106.19f},      {"Bac Ninh", 21.18f, 106.07f},
      {"Ben Tre", 10.24f, 106.37f},        {"Binh Duong", 11.16f, 106.66f},
      {"Binh Dinh", 14.15f, 109.00f},      {"Binh Phuoc", 11.75f, 106.84f},
      {"Binh Thuan", 11.09f, 108.16f},     {"Ca Mau", 9.17f, 104.91f},
      {"Cao Bang", 22.66f, 106.25f},       {"Can Tho", 10.03f, 105.78f},
      {"Da Nang", 16.05f, 108.20f},        {"Dak Lak", 12.71f, 108.05f},
      {"Dak Nong", 12.16f, 107.69f},       {"Dien Bien", 21.38f, 103.02f},
      {"Dong Nai", 10.94f, 106.91f},       {"Dong Thap", 10.45f, 105.63f},
      {"Gia Lai", 13.98f, 108.00f},        {"Ha Giang", 22.82f, 104.98f},
      {"Ha Nam", 20.54f, 105.91f},         {"Ha Noi", 21.02f, 105.85f},
      {"Ha Tinh", 18.33f, 105.90f},        {"Hai Duong", 20.93f, 106.31f},
      {"Hai Phong", 20.84f, 106.68f},      {"Hau Giang", 9.78f, 105.47f},
      {"Hoa Binh", 20.81f, 105.33f},       {"Ho Chi Minh", 10.82f, 106.62f},
      {"Hung Yen", 20.64f, 106.05f},       {"Khanh Hoa", 12.24f, 109.19f},
      {"Kien Giang", 9.92f, 105.11f},      {"Kon Tum", 14.35f, 108.00f},
      {"Lai Chau", 22.39f, 103.46f},       {"Lang Son", 21.85f, 106.76f},
      {"Lao Cai", 22.48f, 103.97f},        {"Lam Dong", 11.94f, 108.45f},
      {"Long An", 10.69f, 106.40f},        {"Nam Dinh", 20.42f, 106.16f},
      {"Nghe An", 18.67f, 105.68f},        {"Ninh Binh", 20.25f, 105.97f},
      {"Ninh Thuan", 11.56f, 108.98f},     {"Phu Tho", 21.32f, 105.21f},
      {"Phu Yen", 13.08f, 109.30f},        {"Quang Binh", 17.47f, 106.60f},
      {"Quang Nam", 15.56f, 108.48f},      {"Quang Ngai", 15.12f, 108.80f},
      {"Quang Ninh", 20.94f, 107.07f},     {"Quang Tri", 16.74f, 107.18f},
      {"Soc Trang", 9.60f, 105.97f},       {"Son La", 21.32f, 103.91f},
      {"Tay Ninh", 11.31f, 106.10f},       {"Thai Binh", 20.44f, 106.33f},
      {"Thai Nguyen", 21.59f, 105.84f},    {"Thanh Hoa", 19.80f, 105.77f},
      {"Thua Thien Hue", 16.46f, 107.59f}, {"Tien Giang", 10.42f, 106.34f},
      {"Tra Vinh", 9.93f, 106.33f},        {"Tuyen Quang", 21.81f, 105.21f},
      {"Vinh Long", 10.25f, 105.97f},      {"Vinh Phuc", 21.30f, 105.59f},
      {"Yen Bai", 21.70f, 104.87f}};
};
