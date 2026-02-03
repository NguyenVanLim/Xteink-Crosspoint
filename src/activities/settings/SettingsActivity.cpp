#include "SettingsActivity.h"

#include <GfxRenderer.h>
#include <HardwareSerial.h>

#include "CategorySettingsActivity.h"
#include "CrossPointSettings.h"
#include "MappedInputManager.h"
#include "fontIds.h"

const char *SettingsActivity::categoryNames[categoryCount] = {
    "Hiển thị", "Trình đọc", "Điều khiển", "Hệ thống", "Kết nối", "Nâng cao"};

namespace {
constexpr int displaySettingsCount = 6;
const SettingInfo displaySettings[displaySettingsCount] = {
    // Should match with SLEEP_SCREEN_MODE
    SettingInfo::Enum("Màn hình chờ", &CrossPointSettings::sleepScreen,
                      {"Tối", "Sáng", "Tùy chỉnh", "Bìa sách", "Không"}),
    SettingInfo::Enum("Chế độ bìa sách",
                      &CrossPointSettings::sleepScreenCoverMode,
                      {"Vừa khít", "Cắt"}),
    SettingInfo::Enum("Bộ lọc bìa sách",
                      &CrossPointSettings::sleepScreenCoverFilter,
                      {"Không", "Tương phản", "Đảo ngược"}),
    SettingInfo::Enum("Thanh trạng thái", &CrossPointSettings::statusBar,
                      {"Không", "Không %", "Đầy đủ kèm %", "Đầy đủ kèm thanh",
                       "Thanh tiến trình"}),
    SettingInfo::Enum("Ẩn % Pin", &CrossPointSettings::hideBatteryPercentage,
                      {"Không bao giờ", "Trong sách", "Luôn luôn"}),
    SettingInfo::Enum(
        "Tần số làm mới", &CrossPointSettings::refreshFrequency,
        {"1 trang", "5 trang", "10 trang", "15 trang", "30 trang"})};

constexpr int readerSettingsCount = 9;
const SettingInfo readerSettings[readerSettingsCount] = {
    SettingInfo::Enum("Phông chữ", &CrossPointSettings::fontFamily,
                      {"Bookerly", "Noto Sans", "Open Dyslexic"}),
    SettingInfo::Enum("Cỡ chữ", &CrossPointSettings::fontSize,
                      {"Nhỏ", "Vừa", "Lớn", "Rất lớn"}),
    SettingInfo::Enum("Giãn dòng", &CrossPointSettings::lineSpacing,
                      {"Hẹp", "Bình thường", "Rộng"}),
    SettingInfo::Value("Lề màn hình", &CrossPointSettings::screenMargin,
                       {5, 40, 5}),
    SettingInfo::Enum("Căn lề đoạn", &CrossPointSettings::paragraphAlignment,
                      {"Căn đều", "Trái", "Giữa", "Phải"}),
    SettingInfo::Toggle("Gạch nối từ", &CrossPointSettings::hyphenationEnabled),
    SettingInfo::Enum("Hướng đọc", &CrossPointSettings::orientation,
                      {"Dọc", "Ngang CW", "Đảo ngược", "Ngang CCW"}),
    SettingInfo::Toggle("Giãn cách đoạn",
                        &CrossPointSettings::extraParagraphSpacing),
    SettingInfo::Toggle("Khử răng cưa", &CrossPointSettings::textAntiAliasing)};

constexpr int controlsSettingsCount = 4;
const SettingInfo controlsSettings[controlsSettingsCount] = {
    SettingInfo::Enum("Bố cục nút trước",
                      &CrossPointSettings::frontButtonLayout,
                      {"Bck, Cnfrm, Lft, Rght", "Lft, Rght, Bck, Cnfrm",
                       "Lft, Bck, Cnfrm, Rght", "Bck, Cnfrm, Rght, Lft"}),
    SettingInfo::Enum("Bố cục nút bên", &CrossPointSettings::sideButtonLayout,
                      {"Prev, Next", "Next, Prev"}),
    SettingInfo::Toggle("Nhấn giữ để tua",
                        &CrossPointSettings::longPressChapterSkip),
    SettingInfo::Enum("Nhấn nút nguồn", &CrossPointSettings::shortPwrBtn,
                      {"Bỏ qua", "Ngủ", "Chuyển trang"})};

constexpr int systemSettingsCount = 5;
const SettingInfo systemSettings[systemSettingsCount] = {
    SettingInfo::Enum("Thời gian chờ", &CrossPointSettings::sleepTimeout,
                      {"1 phút", "5 phút", "10 phút", "15 phút", "30 phút"}),
    SettingInfo::Action("Đồng bộ KOReader"), SettingInfo::Action("Duyệt OPDS"),
    SettingInfo::Action("Đặt lại thiết bị"),
    SettingInfo::Action("Kiểm tra cập nhật")};

constexpr int connectivitySettingsCount = 2;
const SettingInfo connectivitySettings[connectivitySettingsCount] = {
    SettingInfo::Action("Kết nối WiFi"),
    SettingInfo::Action("Vị trí thời tiết")};

constexpr int advancedSettingsCount = 1;
const SettingInfo advancedSettings[advancedSettingsCount] = {
    SettingInfo::Toggle("Tải hình ảnh", &CrossPointSettings::loadImages)};
} // namespace

void SettingsActivity::taskTrampoline(void *param) {
  auto *self = static_cast<SettingsActivity *>(param);
  self->displayTaskLoop();
}

void SettingsActivity::onEnter() {
  Activity::onEnter();
  renderingMutex = xSemaphoreCreateMutex();

  // Reset selection to first category
  selectedCategoryIndex = 0;

  // Trigger first update
  updateRequired = true;

  xTaskCreate(&SettingsActivity::taskTrampoline, "SettingsActivityTask",
              4096,              // Stack size
              this,              // Parameters
              1,                 // Priority
              &displayTaskHandle // Task handle
  );
}

void SettingsActivity::onExit() {
  ActivityWithSubactivity::onExit();

  // Wait until not rendering to delete task to avoid killing mid-instruction to
  // EPD
  xSemaphoreTake(renderingMutex, portMAX_DELAY);
  if (displayTaskHandle) {
    vTaskDelete(displayTaskHandle);
    displayTaskHandle = nullptr;
  }
  vSemaphoreDelete(renderingMutex);
  renderingMutex = nullptr;
}

void SettingsActivity::loop() {
  if (subActivity) {
    subActivity->loop();
    return;
  }

  // Handle category selection
  if (mappedInput.wasPressed(MappedInputManager::Button::Confirm)) {
    enterCategory(selectedCategoryIndex);
    return;
  }

  if (mappedInput.wasPressed(MappedInputManager::Button::Back)) {
    SETTINGS.saveToFile();
    onGoHome();
    return;
  }

  // Handle navigation
  if (mappedInput.wasPressed(MappedInputManager::Button::Up) ||
      mappedInput.wasPressed(MappedInputManager::Button::Left)) {
    // Move selection up (with wrap-around)
    selectedCategoryIndex = (selectedCategoryIndex > 0)
                                ? (selectedCategoryIndex - 1)
                                : (categoryCount - 1);
    updateRequired = true;
  } else if (mappedInput.wasPressed(MappedInputManager::Button::Down) ||
             mappedInput.wasPressed(MappedInputManager::Button::Right)) {
    // Move selection down (with wrap around)
    selectedCategoryIndex = (selectedCategoryIndex < categoryCount - 1)
                                ? (selectedCategoryIndex + 1)
                                : 0;
    updateRequired = true;
  }
}

void SettingsActivity::enterCategory(int categoryIndex) {
  if (categoryIndex < 0 || categoryIndex >= categoryCount) {
    return;
  }

  xSemaphoreTake(renderingMutex, portMAX_DELAY);
  exitActivity();

  const SettingInfo *settingsList = nullptr;
  int settingsCount = 0;

  switch (categoryIndex) {
  case 0: // Display
    settingsList = displaySettings;
    settingsCount = displaySettingsCount;
    break;
  case 1: // Reader
    settingsList = readerSettings;
    settingsCount = readerSettingsCount;
    break;
  case 2: // Controls
    settingsList = controlsSettings;
    settingsCount = controlsSettingsCount;
    break;
  case 3: // System
    settingsList = systemSettings;
    settingsCount = systemSettingsCount;
    break;
  case 4: // Connectivity
    settingsList = connectivitySettings;
    settingsCount = connectivitySettingsCount;
    break;
  case 5: // Advanced
    settingsList = advancedSettings;
    settingsCount = advancedSettingsCount;
    break;
  }

  enterNewActivity(new CategorySettingsActivity(
      renderer, mappedInput, categoryNames[categoryIndex], settingsList,
      settingsCount, [this] {
        exitActivity();
        updateRequired = true;
      }));
  xSemaphoreGive(renderingMutex);
}

void SettingsActivity::displayTaskLoop() {
  while (true) {
    if (updateRequired && !subActivity) {
      updateRequired = false;
      xSemaphoreTake(renderingMutex, portMAX_DELAY);
      render();
      xSemaphoreGive(renderingMutex);
    }
    vTaskDelay(10 / portTICK_PERIOD_MS);
  }
}

void SettingsActivity::render() const {
  renderer.clearScreen();

  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();

  // Draw header
  renderer.drawCenteredText(UI_12_FONT_ID, 15, "Cài đặt", true,
                            EpdFontFamily::BOLD);

  // Draw selection
  renderer.fillRect(0, 60 + selectedCategoryIndex * 30 - 2, pageWidth - 1, 30);

  // Draw all categories
  for (int i = 0; i < categoryCount; i++) {
    const int categoryY = 60 + i * 30; // 30 pixels between categories

    // Draw category name
    renderer.drawText(UI_10_FONT_ID, 20, categoryY, categoryNames[i],
                      i != selectedCategoryIndex);
  }

  // Draw version text above button hints (shifted up)
  const int versionY = pageHeight - 100;
  renderer.drawText(
      SMALL_FONT_ID,
      pageWidth - 20 - renderer.getTextWidth(SMALL_FONT_ID, CROSSPOINT_VERSION),
      versionY, CROSSPOINT_VERSION);
  renderer.drawText(
      SMALL_FONT_ID,
      pageWidth - 20 -
          renderer.getTextWidth(SMALL_FONT_ID, "Mrslim Version By Lim Nguyen"),
      versionY + 15, "Mrslim Version By Lim Nguyen");

  // Draw help text
  const auto labels = mappedInput.mapLabels("« Quay lại", "Chọn", "", "");
  renderer.drawButtonHints(UI_10_FONT_ID, labels.btn1, labels.btn2, labels.btn3,
                           labels.btn4);

  // Always use standard refresh for settings screen
  renderer.displayBuffer();
}
