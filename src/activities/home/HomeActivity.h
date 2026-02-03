#pragma once
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>

#include <functional>

#include "../Activity.h"

#include "RecentBooksStore.h"

class HomeActivity final : public Activity {
  TaskHandle_t displayTaskHandle = nullptr;
  SemaphoreHandle_t renderingMutex = nullptr;
  int selectorIndex = 0;
  bool updateRequired = false;
  bool hasContinueReading = false;
  bool hasOpdsUrl = false;

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
  const std::function<void()> onOpenBooks;
  const std::function<void()> onOpenFiles;
  const std::function<void()> onSettingsOpen;
  const std::function<void()> onFileTransferOpen;
  const std::function<void()> onOpdsBrowserOpen;

  static void taskTrampoline(void *param);
  [[noreturn]] void displayTaskLoop();
  void render();
  int getMenuItemCount() const;
  bool storeCoverBuffer();   // Store frame frame buffer for cover image
  bool restoreCoverBuffer(); // Restore frame buffer from stored cover
  void freeCoverBuffer();    // Free the stored cover buffer

public:
  explicit HomeActivity(GfxRenderer &renderer, MappedInputManager &mappedInput,
                        const std::function<void()> &onContinueReading,
                        const std::function<void()> &onOpenBooks,
                        const std::function<void()> &onOpenFiles,
                        const std::function<void()> &onSettingsOpen,
                        const std::function<void()> &onFileTransferOpen,
                        const std::function<void()> &onOpdsBrowserOpen)
      : Activity("Home", renderer, mappedInput),
        onContinueReading(onContinueReading), onOpenBooks(onOpenBooks),
        onOpenFiles(onOpenFiles), onSettingsOpen(onSettingsOpen),
        onFileTransferOpen(onFileTransferOpen),
        onOpdsBrowserOpen(onOpdsBrowserOpen) {}
  void onEnter() override;
  void onExit() override;
  void loop() override;
};
