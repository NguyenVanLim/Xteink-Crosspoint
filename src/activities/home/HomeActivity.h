#pragma once
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>

#include <functional>
#include <string>
#include <vector>

#include "../../MappedInputManager.h"
#include "../Activity.h"
#include "RecentBooksStore.h"

class HomeActivity final : public Activity {
  TaskHandle_t displayTaskHandle = nullptr;
  SemaphoreHandle_t renderingMutex = nullptr;
  int selectorIndex = 0;
  bool updateRequired = false;
  bool hasContinueReading = false;
  bool hasOpdsUrl = false;
  mutable int lastRenderedSelectorIndex = -1;
  mutable bool fullRedrawRequired = true;
  bool isWifiMenuUnlocked = false;
  std::vector<MappedInputManager::Button> inputSequence;

  struct RecentBookInfo {
    RecentBooksStore::RecentBook book;
    std::string coverBmpPath;
    bool hasCoverImage = false;
  };

  std::vector<RecentBookInfo> recentBooks;

  bool coverRendered = false;     // Track if cover has been rendered once
  bool coverBufferStored = false; // Track if cover buffer is stored
  uint8_t *coverBuffer = nullptr; // HomeActivity's own buffer for cover image
  const std::function<void()> onContinueReading;
  const std::function<void()> onOpenRecent;
  const std::function<void()> onOpenBooks;
  const std::function<void()> onOpenFiles;
  const std::function<void()> onSettingsOpen;
  const std::function<void()> onFileTransferOpen;
  const std::function<void()> onOpdsBrowserOpen;
  const std::function<void()> onWifiSettingsOpen;
  const std::function<void()> onWeatherSettingsOpen;

  static void taskTrampoline(void *param);
  [[noreturn]] void displayTaskLoop();
  void render();
  int getMenuItemCount() const;
  bool storeCoverBuffer();   // Store frame frame buffer for cover image
  bool restoreCoverBuffer(); // Restore frame buffer from stored cover
  void freeCoverBuffer();    // Free the stored cover buffer
  void drawWifiSignal(int xRoot);
  void drawWeatherInfo();

public:
  explicit HomeActivity(GfxRenderer &renderer, MappedInputManager &mappedInput,
                        const std::function<void()> &onContinueReading,
                        const std::function<void()> &onOpenRecent,
                        const std::function<void()> &onOpenBooks,
                        const std::function<void()> &onOpenFiles,
                        const std::function<void()> &onSettingsOpen,
                        const std::function<void()> &onFileTransferOpen,
                        const std::function<void()> &onOpdsBrowserOpen,
                        const std::function<void()> &onWifiSettingsOpen,
                        const std::function<void()> &onWeatherSettingsOpen)
      : Activity("Home", renderer, mappedInput),
        onContinueReading(onContinueReading), onOpenRecent(onOpenRecent),
        onOpenBooks(onOpenBooks), onOpenFiles(onOpenFiles),
        onSettingsOpen(onSettingsOpen), onFileTransferOpen(onFileTransferOpen),
        onOpdsBrowserOpen(onOpdsBrowserOpen),
        onWifiSettingsOpen(onWifiSettingsOpen),
        onWeatherSettingsOpen(onWeatherSettingsOpen) {}
  void onEnter() override;
  void onExit() override;
  void loop() override;
};
