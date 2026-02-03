#include "ClearCacheActivity.h"

#include <GfxRenderer.h>
#include <HardwareSerial.h>
#include <SDCardManager.h>

#include "MappedInputManager.h"
#include "RecentBooksStore.h"
#include "fontIds.h"

void ClearCacheActivity::taskTrampoline(void *param) {
  auto *self = static_cast<ClearCacheActivity *>(param);
  self->displayTaskLoop();
}

void ClearCacheActivity::onEnter() {
  ActivityWithSubactivity::onEnter();

  renderingMutex = xSemaphoreCreateMutex();
  state = WARNING;
  updateRequired = true;

  xTaskCreate(&ClearCacheActivity::taskTrampoline, "ClearCacheActivityTask",
              4096,              // Stack size
              this,              // Parameters
              1,                 // Priority
              &displayTaskHandle // Task handle
  );
}

void ClearCacheActivity::onExit() {
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

void ClearCacheActivity::displayTaskLoop() {
  while (true) {
    if (updateRequired) {
      updateRequired = false;
      xSemaphoreTake(renderingMutex, portMAX_DELAY);
      render();
      xSemaphoreGive(renderingMutex);
    }
    vTaskDelay(10 / portTICK_PERIOD_MS);
  }
}

void ClearCacheActivity::render() {
  const auto pageHeight = renderer.getScreenHeight();

  renderer.clearScreen();
  renderer.drawCenteredText(UI_12_FONT_ID, 15, "Đặt lại thiết bị", true,
                            EpdFontFamily::BOLD);

  if (state == WARNING) {
    renderer.drawCenteredText(UI_10_FONT_ID, pageHeight / 2 - 60,
                              "Toàn bộ dữ liệu đệm và lịch sử đọc", true);
    renderer.drawCenteredText(UI_10_FONT_ID, pageHeight / 2 - 30,
                              "sẽ bị xóa sạch hoàn toàn!", true,
                              EpdFontFamily::BOLD);
    renderer.drawCenteredText(UI_10_FONT_ID, pageHeight / 2 + 10,
                              "Thiết bị sẽ khởi động lại", true);
    renderer.drawCenteredText(UI_10_FONT_ID, pageHeight / 2 + 30,
                              "sau khi hoàn tất.", true);

    const auto labels = mappedInput.mapLabels("« Hủy", "Đặt lại", "", "");
    renderer.drawButtonHints(UI_10_FONT_ID, labels.btn1, labels.btn2,
                             labels.btn3, labels.btn4);
    renderer.displayBuffer();
    return;
  }

  if (state == CLEARING) {
    renderer.drawCenteredText(UI_10_FONT_ID, pageHeight / 2, "Đang dọn dẹp...",
                              true, EpdFontFamily::BOLD);
    renderer.displayBuffer();
    return;
  }

  if (state == SUCCESS) {
    renderer.drawCenteredText(UI_10_FONT_ID, pageHeight / 2 - 20,
                              "Cache Cleared", true, EpdFontFamily::BOLD);
    String resultText = String(clearedCount) + " items removed";
    if (failedCount > 0) {
      resultText += ", " + String(failedCount) + " failed";
    }
    renderer.drawCenteredText(UI_10_FONT_ID, pageHeight / 2 + 10,
                              resultText.c_str());

    const auto labels = mappedInput.mapLabels("« Back", "", "", "");
    renderer.drawButtonHints(UI_10_FONT_ID, labels.btn1, labels.btn2,
                             labels.btn3, labels.btn4);
    renderer.displayBuffer();
    return;
  }

  if (state == FAILED) {
    renderer.drawCenteredText(UI_10_FONT_ID, pageHeight / 2 - 20,
                              "Failed to clear cache", true,
                              EpdFontFamily::BOLD);
    renderer.drawCenteredText(UI_10_FONT_ID, pageHeight / 2 + 10,
                              "Check serial output for details");

    const auto labels = mappedInput.mapLabels("« Back", "", "", "");
    renderer.drawButtonHints(UI_10_FONT_ID, labels.btn1, labels.btn2,
                             labels.btn3, labels.btn4);
    renderer.displayBuffer();
    return;
  }
}

void ClearCacheActivity::clearCache() {
  Serial.printf("[%lu] [FACTORY_RESET] Performing full reset...\n", millis());

  char filename[128];

  // 1. Clear /cover
  if (SdMan.exists("/cover")) {
    auto root = SdMan.open("/cover");
    while (auto file = root.openNextFile()) {
      file.getName(filename, sizeof(filename));
      SdMan.remove((std::string("/cover/") + filename).c_str());
      file.close();
    }
    root.close();
  }

  // 2. Clear /.crosspoint
  if (SdMan.exists("/.crosspoint")) {
    auto root = SdMan.open("/.crosspoint");
    while (auto file = root.openNextFile()) {
      file.getName(filename, sizeof(filename));
      SdMan.remove((std::string("/.crosspoint/") + filename).c_str());
      file.close();
    }
    root.close();
  }

  // 3. Clear Recent Books
  RECENT_BOOKS.clear();
  RECENT_BOOKS.saveToFile();

  Serial.printf("[%lu] [FACTORY_RESET] Reset complete. Rebooting...\n",
                millis());
  ESP.restart();
}

void ClearCacheActivity::loop() {
  if (state == WARNING) {
    if (mappedInput.wasPressed(MappedInputManager::Button::Confirm)) {
      Serial.printf(
          "[%lu] [CLEAR_CACHE] User confirmed, starting cache clear\n",
          millis());
      xSemaphoreTake(renderingMutex, portMAX_DELAY);
      state = CLEARING;
      xSemaphoreGive(renderingMutex);
      updateRequired = true;
      vTaskDelay(10 / portTICK_PERIOD_MS);

      clearCache();
    }

    if (mappedInput.wasPressed(MappedInputManager::Button::Back)) {
      Serial.printf("[%lu] [CLEAR_CACHE] User cancelled\n", millis());
      goBack();
    }
    return;
  }

  if (state == SUCCESS || state == FAILED) {
    if (mappedInput.wasPressed(MappedInputManager::Button::Back)) {
      goBack();
    }
    return;
  }
}
