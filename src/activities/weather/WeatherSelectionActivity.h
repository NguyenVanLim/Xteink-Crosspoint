#pragma once
#include "activities/Activity.h"
#include <cstdint>
#include <functional>

/**
 * WeatherSelectionActivity displays a scrollable list of 63 Vietnamese
 * provinces for weather location selection. When a province is selected, it
 * saves the index and triggers a weather refresh.
 */
class WeatherSelectionActivity final : public Activity {
  int selectedIndex = 0;
  int scrollOffset = 0;
  const std::function<void()> onComplete;

  void render() const;
  std::string getProvinceDisplayName(uint8_t index) const;

public:
  explicit WeatherSelectionActivity(GfxRenderer &renderer,
                                    MappedInputManager &mappedInput,
                                    const std::function<void()> &onComplete)
      : Activity("WeatherSelection", renderer, mappedInput),
        onComplete(onComplete), selectedIndex(0) {}

  void onEnter() override;
  void loop() override;
};
