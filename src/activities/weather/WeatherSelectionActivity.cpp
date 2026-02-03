#include "WeatherSelectionActivity.h"
#include "../../CrossPointSettings.h"
#include "../../MappedInputManager.h"
#include "../../fontIds.h"
#include "../../services/WeatherService.h"
#include <GfxRenderer.h>
#include <HardwareSerial.h>

void WeatherSelectionActivity::onEnter() {
  Activity::onEnter();

  // Start with current selected province
  selectedIndex = SETTINGS.weatherCityIndex;
  if (selectedIndex >= WeatherService::getCityCount()) {
    selectedIndex = 23; // Default to Ha Noi
  }

  // Calculate scroll offset to show selected item
  scrollOffset = selectedIndex;
  if (scrollOffset > WeatherService::getCityCount() - 10) {
    scrollOffset = WeatherService::getCityCount() - 10;
  }
  if (scrollOffset < 0)
    scrollOffset = 0;

  Serial.printf("[Weather] Selection activity entered, current: %s\n",
                WeatherService::getCityName(selectedIndex));

  render();
}

void WeatherSelectionActivity::loop() {
  Activity::loop();

  bool needsRender = false;

  // Handle input
  if (mappedInput.wasPressed(MappedInputManager::Button::Up)) {
    if (selectedIndex > 0) {
      selectedIndex--;
      if (selectedIndex < scrollOffset) {
        scrollOffset = selectedIndex;
      }
      needsRender = true;
    }
  }

  if (mappedInput.wasPressed(MappedInputManager::Button::Down)) {
    if (selectedIndex < WeatherService::getCityCount() - 1) {
      selectedIndex++;
      if (selectedIndex >= scrollOffset + 10) {
        scrollOffset = selectedIndex - 9;
      }
      needsRender = true;
    }
  }

  if (mappedInput.wasPressed(MappedInputManager::Button::Confirm)) {
    // Save selection
    SETTINGS.weatherCityIndex = selectedIndex;
    SETTINGS.saveToFile();

    Serial.printf("[Weather] Selected: %s (index %d)\n",
                  WeatherService::getCityName(selectedIndex), selectedIndex);

    // Trigger weather refresh
    WeatherService::getInstance().refresh();

    // Call completion callback
    if (onComplete) {
      onComplete();
    }
    return;
  }

  if (mappedInput.wasPressed(MappedInputManager::Button::Back)) {
    // Cancel without saving
    if (onComplete) {
      onComplete();
    }
    return;
  }

  if (needsRender) {
    render();
  }
}

void WeatherSelectionActivity::render() const {
  renderer.clearScreen();

  // Title
  // Title
  renderer.drawText(UI_12_FONT_ID, 10, 10, "Chọn tỉnh/thành phố", true,
                    EpdFontFamily::BOLD);

  // Draw list of provinces (10 visible at a time)
  int y = 40;
  int visibleCount = 10;

  for (int i = 0;
       i < visibleCount && (scrollOffset + i) < WeatherService::getCityCount();
       i++) {
    int provinceIndex = scrollOffset + i;
    const char *provinceName = WeatherService::getCityName(provinceIndex);

    // Highlight selected item
    if (provinceIndex == selectedIndex) {
      renderer.fillRect(5, y - 2, renderer.getScreenWidth() - 10, 18, true);
      renderer.drawText(UI_12_FONT_ID, 10, y, provinceName,
                        false); // White text
    } else {
      renderer.drawText(UI_12_FONT_ID, 10, y, provinceName,
                        true); // Black text
    }

    y += 20;
  }

  // Draw scrollbar indicator if needed
  if (WeatherService::getCityCount() > visibleCount) {
    int scrollbarHeight = (visibleCount * 200) / WeatherService::getCityCount();
    int scrollbarY = 40 + (scrollOffset * 200) / WeatherService::getCityCount();
    renderer.drawRect(renderer.getScreenWidth() - 8, scrollbarY, 4,
                      scrollbarHeight, true);
  }

  // Instructions at bottom
  renderer.drawText(UI_10_FONT_ID, 10, renderer.getScreenHeight() - 20,
                    "↑↓: Di chuyển  OK: Chọn  BACK: Hủy");

  renderer.displayBuffer();
}

std::string
WeatherSelectionActivity::getProvinceDisplayName(uint8_t index) const {
  return std::string(WeatherService::getCityName(index));
}
