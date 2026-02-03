#include "HomeActivity.h"
#include "HomeIcons.h"

#include <Bitmap.h>
#include <Epub.h>
#include <GfxRenderer.h>
#include <SDCardManager.h>
#include <Xtc.h>

#include <cstring>
#include <vector>

#include "../../services/WeatherService.h"
#include "Battery.h"
#include "CrossPointSettings.h"
#include "CrossPointState.h"
#include "MappedInputManager.h"
#include "ScreenComponents.h"
#include "fontIds.h"
#include "util/StringUtils.h"
#include <WiFi.h>

void HomeActivity::taskTrampoline(void *param) {
  auto *self = static_cast<HomeActivity *>(param);
  self->displayTaskLoop();
}

int HomeActivity::getMenuItemCount() const {
  int count = 6; // Main Book, Recent, Books, Files, Transfer, Settings
  if (hasOpdsUrl)
    count++;
  // WiFi and Weather removed from grid (moved to Settings)
  return count;
}

void HomeActivity::onEnter() {
  Activity::onEnter();

  renderingMutex = xSemaphoreCreateMutex();

  // Load recent books and process top items
  const auto &allRecent = RECENT_BOOKS.getBooks();
  recentBooks.clear();

  if (allRecent.empty()) {
    hasContinueReading = false;
    return;
  }

  // 1. First item is always the most recently opened one
  std::vector<RecentBooksStore::RecentBook> finalOrder;
  finalOrder.push_back(allRecent[0]);

  // 2. Sort the rest by progress descending
  if (allRecent.size() > 1) {
    std::vector<RecentBooksStore::RecentBook> remaining(allRecent.begin() + 1,
                                                        allRecent.end());
    std::sort(remaining.begin(), remaining.end(),
              [](const RecentBooksStore::RecentBook &a,
                 const RecentBooksStore::RecentBook &b) {
                return a.progress > b.progress;
              });
    finalOrder.insert(finalOrder.end(), remaining.begin(), remaining.end());
  }

  // Take top 5 books (Main + 4 in Progress list)
  for (size_t i = 0; i < std::min(finalOrder.size(), (size_t)5); ++i) {
    const auto &book = finalOrder[i];
    if (!SdMan.exists(book.path.c_str()))
      continue;

    RecentBookInfo info;
    info.book = book;

    // Handle title/author extraction if empty (e.g. for newly added books from
    // file system)
    std::string filename = book.path;
    const size_t lastSlash = filename.find_last_of('/');
    if (lastSlash != std::string::npos) {
      filename = filename.substr(lastSlash + 1);
    }

    if (info.book.title.empty()) {
      info.book.title = filename;
    }

    // Try to get metadata and cover
    if (StringUtils::checkFileExtension(filename, ".epub")) {
      Epub epub(book.path, "/.crosspoint");
      epub.load(false);
      if (info.book.title == filename && !epub.getTitle().empty()) {
        info.book.title = std::string(epub.getTitle());
      }
      if (info.book.author.empty() && !epub.getAuthor().empty()) {
        info.book.author = std::string(epub.getAuthor());
      }
      if (epub.generateThumbBmp()) {
        info.coverBmpPath = epub.getThumbBmpPath();
        info.hasCoverImage = true;
      }
    } else if (StringUtils::checkFileExtension(filename, ".xtch") ||
               StringUtils::checkFileExtension(filename, ".xtc")) {
      Xtc xtc(book.path, "/.crosspoint");
      if (xtc.load()) {
        if (info.book.title == filename && !xtc.getTitle().empty()) {
          info.book.title = std::string(xtc.getTitle());
        }
        if (info.book.author.empty() && !xtc.getAuthor().empty()) {
          info.book.author = std::string(xtc.getAuthor());
        }
        if (xtc.generateThumbBmp()) {
          info.coverBmpPath = xtc.getThumbBmpPath();
          info.hasCoverImage = true;
        }
      }
      // Remove extension if title still has it
      if (info.book.title.length() > 5 &&
          info.book.title.substr(info.book.title.length() - 5) == ".xtch") {
        info.book.title.resize(info.book.title.length() - 5);
      } else if (info.book.title.length() > 4 &&
                 info.book.title.substr(info.book.title.length() - 4) ==
                     ".xtc") {
        info.book.title.resize(info.book.title.length() - 4);
      }
    } else if (StringUtils::checkFileExtension(filename, ".txt")) {
      // For TXT, just clean up extension if needed
      if (info.book.title.length() > 4 &&
          info.book.title.substr(info.book.title.length() - 4) == ".txt") {
        info.book.title.resize(info.book.title.length() - 4);
      }
    }

    recentBooks.push_back(info);
  }

  hasContinueReading = !recentBooks.empty();

  // Check if OPDS browser URL is configured
  hasOpdsUrl = strlen(SETTINGS.opdsServerUrl) > 0;

  selectorIndex = 0;

  // Trigger first update
  updateRequired = true;

  xTaskCreate(&HomeActivity::taskTrampoline, "HomeActivityTask",
              4096, // Stack size (increased for cover image rendering)
              this, // Parameters
              1,    // Priority
              &displayTaskHandle // Task handle
  );
}

void HomeActivity::onExit() {
  Activity::onExit();

  // Wait until not rendering to delete task to avoid killing mid-instruction to
  // EPD
  xSemaphoreTake(renderingMutex, portMAX_DELAY);
  if (displayTaskHandle) {
    vTaskDelete(displayTaskHandle);
    displayTaskHandle = nullptr;
  }
  vSemaphoreDelete(renderingMutex);
  renderingMutex = nullptr;

  // Free the stored cover buffer if any
  freeCoverBuffer();
}

bool HomeActivity::storeCoverBuffer() {
  uint8_t *frameBuffer = renderer.getFrameBuffer();
  if (!frameBuffer) {
    return false;
  }

  // Free any existing buffer first
  freeCoverBuffer();

  const size_t bufferSize = GfxRenderer::getBufferSize();
  coverBuffer = static_cast<uint8_t *>(malloc(bufferSize));
  if (!coverBuffer) {
    return false;
  }

  memcpy(coverBuffer, frameBuffer, bufferSize);
  return true;
}

bool HomeActivity::restoreCoverBuffer() {
  if (!coverBuffer) {
    return false;
  }

  uint8_t *frameBuffer = renderer.getFrameBuffer();
  if (!frameBuffer) {
    return false;
  }

  const size_t bufferSize = GfxRenderer::getBufferSize();
  memcpy(frameBuffer, coverBuffer, bufferSize);
  return true;
}

void HomeActivity::freeCoverBuffer() {
  if (coverBuffer) {
    free(coverBuffer);
    coverBuffer = nullptr;
  }
  coverBufferStored = false;
}

void HomeActivity::loop() {
  const bool upPressed = mappedInput.wasPressed(MappedInputManager::Button::Up);
  const bool downPressed =
      mappedInput.wasPressed(MappedInputManager::Button::Down);
  const bool leftPressed =
      mappedInput.wasPressed(MappedInputManager::Button::Left);
  const bool rightPressed =
      mappedInput.wasPressed(MappedInputManager::Button::Right);

  // Hidden WiFi menu sequence: L, L, L, R, L
  if (leftPressed || rightPressed || upPressed || downPressed) {
    auto btn = leftPressed    ? MappedInputManager::Button::Left
               : rightPressed ? MappedInputManager::Button::Right
               : upPressed    ? MappedInputManager::Button::Up
                              : MappedInputManager::Button::Down;
    inputSequence.push_back(btn);
    if (inputSequence.size() > 5) {
      inputSequence.erase(inputSequence.begin());
    }

    // Hidden WiFi menu logic removed
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    switch (selectorIndex) {
    case 0: // Main Book (Left)
      if (hasContinueReading)
        onContinueReading();
      break;
    case 1: // Progress List (Right/Recent)
      onOpenRecent();
      break;
    case 2: // Tủ sách
      onOpenBooks();
      break;
    case 3: // Tệp
      onOpenFiles();
      break;
    case 4: // Chuyển file
      onFileTransferOpen();
      break;
    case 5: // Duyệt OPDS or Settings
      if (hasOpdsUrl)
        onOpdsBrowserOpen();
      else
        onSettingsOpen();
      break;
    case 6: // Settings
      onSettingsOpen();
      break;
    }
  }

  int nextIndex = selectorIndex;

  if (upPressed) {
    if (selectorIndex >= 2)
      nextIndex = selectorIndex - 2;
  } else if (downPressed) {
    if (selectorIndex <= getMenuItemCount() - 3) {
      nextIndex = selectorIndex + 2;
      // Clamp to available items
      int maxIdx = getMenuItemCount() - 1;
      if (nextIndex > maxIdx)
        nextIndex = maxIdx;
    }
  } else if (leftPressed) {
    if (selectorIndex % 2 != 0)
      nextIndex = selectorIndex - 1;
  } else if (rightPressed) {
    int maxIdx = getMenuItemCount() - 1;
    if (selectorIndex % 2 == 0 && selectorIndex < maxIdx)
      nextIndex = selectorIndex + 1;
  }

  if (nextIndex != selectorIndex) {
    selectorIndex = nextIndex;
    updateRequired = true;
  }
}

// onFactoryReset implementation removed

void HomeActivity::displayTaskLoop() {
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

void HomeActivity::render() {
  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();

  if (fullRedrawRequired) {
    renderer.clearScreen();
    const int midX = pageWidth / 2;

    // Status Bar Branding - MOVED TO BOTTOM
    // renderer.drawCenteredText(UI_10_FONT_ID, 10, "MRSLIM", true,
    //                          EpdFontFamily::BOLD);
    drawWeatherInfo();

    // Layout Constants
    const int yStatusBar = 35;
    const int topH = 360;
    const int margin = 10;
    const int padding = 15;

    // Row 0: Left (Main Book) | Right (Progress List)
    const int row0Y = yStatusBar;

    // Main Book Card
    const int bookX = margin;
    const int bookY = row0Y;
    const int bookW = midX - 1.5 * margin;
    const int bookH = topH;

    renderer.drawRect(bookX, bookY, bookW, bookH, true);

    if (hasContinueReading && !recentBooks.empty()) {
      const auto &info = recentBooks[0];
      bool coverDrawn = false;
      if (info.hasCoverImage && !info.coverBmpPath.empty()) {
        FsFile file;
        if (SdMan.openFileForRead("HOME", info.coverBmpPath, file)) {
          Bitmap bitmap(file);
          if (bitmap.parseHeaders() == BmpReaderError::Ok) {
            renderer.drawBitmap(bitmap, bookX + 5, bookY + 5, bookW - 10,
                                bookH - 10);
            coverDrawn = true;
          }
          file.close();
        }
      }

      if (!coverDrawn) {
        // Center "BÌA SÁCH" in its box
        int cw = renderer.getTextWidth(UI_12_FONT_ID, "BÌA SÁCH");
        renderer.drawText(UI_12_FONT_ID, bookX + (bookW - cw) / 2,
                          bookY + bookH / 2 - 10, "BÌA SÁCH", true);
      }

      // Overlay "ĐỌC TIẾP"
      const int overlayH = 45;
      const int overlayY = bookY + bookH - overlayH - 5;
      renderer.fillRect(bookX + 5, overlayY, bookW - 10, overlayH, true);
      const char *dtLabel = "ĐỌC TIẾP";
      int dtW =
          renderer.getTextWidth(UI_12_FONT_ID, dtLabel, EpdFontFamily::BOLD);
      renderer.drawText(UI_12_FONT_ID, bookX + (bookW - dtW) / 2, overlayY + 12,
                        dtLabel, false, EpdFontFamily::BOLD);

      // Title
      std::string title = info.book.title;
      if (renderer.getTextWidth(UI_10_FONT_ID, title.c_str()) > bookW - 20) {
        while (renderer.getTextWidth(UI_10_FONT_ID, (title + "...").c_str()) >
                   bookW - 20 &&
               title.length() > 0) {
          StringUtils::utf8RemoveLastChar(title);
        }
        title += "...";
      }
      int tW = renderer.getTextWidth(UI_10_FONT_ID, title.c_str());
      renderer.drawText(UI_10_FONT_ID, bookX + (bookW - tW) / 2, overlayY - 25,
                        title.c_str(), true);
    } else {
      renderer.drawCenteredText(UI_10_FONT_ID, bookY + bookH / 2, "Trống",
                                true);
    }

    // Progress List
    const int progX = midX + 0.5 * margin;
    const int progW = pageWidth - progX - margin;
    renderer.drawRect(progX, bookY, progW, bookH, true);
    const char *hLabel = "TIẾN TRÌNH";
    int hW = renderer.getTextWidth(UI_10_FONT_ID, hLabel, EpdFontFamily::BOLD);
    renderer.drawText(UI_10_FONT_ID, progX + (progW - hW) / 2, bookY + 10,
                      hLabel, true, EpdFontFamily::BOLD);

    int currentY = bookY + 50;
    for (size_t i = 1; i < recentBooks.size(); ++i) {
      if (currentY + 65 > bookY + bookH)
        break;
      const auto &book = recentBooks[i].book;
      std::string bTitle = book.title;
      if (renderer.getTextWidth(UI_10_FONT_ID, bTitle.c_str()) > progW - 20) {
        while (renderer.getTextWidth(UI_10_FONT_ID, (bTitle + "...").c_str()) >
                   progW - 20 &&
               bTitle.length() > 0) {
          StringUtils::utf8RemoveLastChar(bTitle);
        }
        bTitle += "...";
      }
      renderer.drawText(UI_10_FONT_ID, progX + 10, currentY, bTitle.c_str(),
                        true);
      const int barY = currentY + 28;
      const int barH = 8;
      renderer.drawRect(progX + 10, barY, progW - 20, barH, true);
      int fillW = (progW - 22) * book.progress / 100;
      if (fillW > 0)
        renderer.fillRect(progX + 11, barY + 1, fillW, barH - 2, true);
      char pStr[8];
      snprintf(pStr, sizeof(pStr), "%d%%", book.progress);
      renderer.drawText(UI_10_FONT_ID, progX + 10, barY + barH + 4, pStr, true);
      currentY += 80;
    }

    // Grid Items
    const int gridRowH = 160;
    struct GridItem {
      const char *label;
      const uint8_t *icon;
    };
    std::vector<GridItem> items = {{"Tủ sách", icon_books},
                                   {"Tệp", icon_files},
                                   {"Chuyển file", icon_network}};
    if (hasOpdsUrl) {
      items.push_back({"Duyệt OPDS", icon_network});
    }
    items.push_back({"Cài đặt", icon_settings});
    // WiFi and Weather removed from grid

    for (size_t i = 0; i < items.size(); ++i) {
      int row = (i / 2) + 1;
      int col = i % 2;

      int x, w;
      if (i == items.size() - 1 && items.size() % 2 != 0) {
        // Center last item if it's the only one on its row
        w = midX - 1.5 * margin;
        x = (pageWidth - w) / 2;
      } else {
        x = (col == 0) ? margin : midX + 0.5 * margin;
        w = (col == 0) ? midX - 1.5 * margin : pageWidth - x - margin;
      }

      int y = row0Y + topH + margin + (row - 1) * gridRowH;
      int h = gridRowH - 12;
      renderer.drawRect(x, y, w, h, true);
      const int iconSize = 48;
      renderer.drawImage(items[i].icon, x + (w - iconSize) / 2,
                         y + (h - iconSize) / 2 - 10, iconSize, iconSize);
      int lW = renderer.getTextWidth(UI_12_FONT_ID, items[i].label);
      renderer.drawText(UI_12_FONT_ID, x + (w - lW) / 2, y + h - 35,
                        items[i].label, true);
    }

    // WiFi menu logic removed

    fullRedrawRequired = false;
  }

  // Draw selector logic for selective refresh...

  // Selective Partial Refresh for Selection Box
  if (selectorIndex != lastRenderedSelectorIndex) {
    auto drawSelector = [&](int idx, bool state) {
      const auto pageWidth = renderer.getScreenWidth();
      const int midX = pageWidth / 2;
      const int margin = 10;
      const int yStatusBar = 35;
      const int topH = 360;
      const int gridRowH = 160;

      int x, y, w, h;
      if (idx == 0) { // Main Book
        x = margin;
        y = yStatusBar;
        w = midX - 1.5 * margin;
        h = topH;
      } else if (idx == 1) { // Progress List
        x = midX + 0.5 * margin;
        y = yStatusBar;
        w = pageWidth - x - margin;
        h = topH;
      } else { // Grid items
        int gridIdx = idx - 2;
        int row = (gridIdx / 2) + 1;
        int col = gridIdx % 2;

        int itemCount = getMenuItemCount() - 2;
        if (gridIdx == itemCount - 1 && itemCount % 2 != 0) {
          // Centered last item logic
          w = midX - 1.5 * margin;
          x = (pageWidth - w) / 2;
        } else {
          x = (col == 0) ? margin : midX + 0.5 * margin;
          w = (col == 0) ? midX - 1.5 * margin : pageWidth - x - margin;
        }
        y = yStatusBar + topH + margin + (row - 1) * gridRowH;
        h = gridRowH - 12;
      }

      // Thick border (3 pixels)
      renderer.drawRect(x - 1, y - 1, w + 2, h + 2, state);
      renderer.drawRect(x - 2, y - 2, w + 4, h + 4, state);
      renderer.drawRect(x - 3, y - 3, w + 6, h + 6, state);
    };

    if (lastRenderedSelectorIndex != -1) {
      drawSelector(lastRenderedSelectorIndex, false);
    }
    drawSelector(selectorIndex, true);
    lastRenderedSelectorIndex = selectorIndex;
    renderer.displayBuffer(EInkDisplay::FAST_REFRESH);
  }

  // Constant elements
  const auto hints = mappedInput.mapLabels("", "Chọn", "Trái", "Phải");
  renderer.drawButtonHints(UI_10_FONT_ID, hints.btn1, hints.btn2, hints.btn3,
                           hints.btn4);

  const uint16_t percentage = battery.readPercentage();
  const std::string pctStr = std::to_string(percentage) + "%";
  const int batteryX = renderer.getScreenWidth() - 25 -
                       renderer.getTextWidth(SMALL_FONT_ID, pctStr.c_str());

  // Draw WiFi signal to the left of battery (leave 35px for bars)
  drawWifiSignal(batteryX - 35);

  ScreenComponents::drawBattery(renderer, batteryX, 10, true);

  // Draw MRSLIM branding at bottom left
  renderer.drawText(UI_12_FONT_ID, 10, pageHeight - 35, "MRSLIM", true,
                    EpdFontFamily::BOLD);

  renderer.displayBuffer(EInkDisplay::FAST_REFRESH);
}

void HomeActivity::drawWifiSignal(int xRoot) {
  if (WiFi.status() == WL_CONNECTED) {
    int32_t rssi = WiFi.RSSI();
    int bars = 0;
    if (rssi >= -50)
      bars = 4;
    else if (rssi >= -60)
      bars = 3;
    else if (rssi >= -70)
      bars = 2;
    else if (rssi >= -80)
      bars = 1;

    const int yRoot = 10;
    for (int i = 0; i < 4; i++) {
      int h = (i + 1) * 4;
      renderer.fillRect(xRoot + i * 6, yRoot + (16 - h), 4, h, i < bars);
    }
  }
}

void HomeActivity::drawWeatherInfo() {
  std::string weatherInfo = WeatherService::getInstance().getCachedWeather();
  if (!weatherInfo.empty()) {
    // Check if text width fits, otherwise truncate?
    // For now just draw it. The helper getCachedWeather includes City Name +
    // Temp.
    renderer.drawText(UI_10_FONT_ID, 10, 10, weatherInfo.c_str(), true);
  }
}
