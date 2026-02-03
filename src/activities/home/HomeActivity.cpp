#include "HomeActivity.h"
#include "HomeIcons.h"

#include <Bitmap.h>
#include <Epub.h>
#include <GfxRenderer.h>
#include <SDCardManager.h>
#include <Xtc.h>

#include <cstring>
#include <vector>

#include "Battery.h"
#include "CrossPointSettings.h"
#include "CrossPointState.h"
#include "MappedInputManager.h"
#include "ScreenComponents.h"
#include "fontIds.h"
#include "util/StringUtils.h"

void HomeActivity::taskTrampoline(void *param) {
  auto *self = static_cast<HomeActivity *>(param);
  self->displayTaskLoop();
}

int HomeActivity::getMenuItemCount() const {
  int count = 3; // My Library, File transfer, Settings
  if (hasContinueReading)
    count++;
  if (hasOpdsUrl)
    count++;
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

  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    switch (selectorIndex) {
    case 0: // Continue Reading
      if (hasContinueReading)
        onContinueReading();
      break;
    case 1: // Books
      onOpenBooks();
      break;
    case 2: // Files
      onOpenFiles();
      break;
    case 3: // Network / Browser
      if (hasOpdsUrl) {
        onOpdsBrowserOpen();
      } else {
        onFileTransferOpen(); // Fallback if no OPDS, or maybe show a menu?
        // For now let's map "Network" to File Transfer if OPDS is missing, or
        // OPDS if present.
      }
      break;
    case 4: // Settings
      onSettingsOpen();
      break;
    }
  }

  // Navigation Logic
  // 0: Top
  // 1: Grid TL, 2: Grid TR
  // 3: Grid BL, 4: Grid BR

  int nextIndex = selectorIndex;

  if (upPressed) {
    if (selectorIndex == 1 || selectorIndex == 2)
      nextIndex = 0; // Go to Book from Library/Files
    else if (selectorIndex == 3)
      nextIndex = 1;
    else if (selectorIndex == 4)
      nextIndex = 2;
  } else if (downPressed) {
    if (selectorIndex == 0)
      nextIndex = 1;
    else if (selectorIndex == 1)
      nextIndex = 3;
    else if (selectorIndex == 2)
      nextIndex = 4;
  } else if (leftPressed) {
    if (selectorIndex == 2)
      nextIndex = 1;
    else if (selectorIndex == 4)
      nextIndex = 3;
  } else if (rightPressed) {
    if (selectorIndex == 0) {
      // Maybe move to progress list? For now just stay.
    } else if (selectorIndex == 1)
      nextIndex = 2;
    else if (selectorIndex == 3)
      nextIndex = 4;
  }

  if (nextIndex != selectorIndex) {
    selectorIndex = nextIndex;
    updateRequired = true;
  }
}

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
  renderer.clearScreen();

  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();
  const int midX = pageWidth / 2;

  // Layout Constants
  const int yStatusBar = 35;
  const int topH = 360;     // Slightly reduced
  const int gridRowH = 160; // Reduced to avoid bottom overlap
  const int margin = 10;
  const int padding = 15;

  // --- Row 0: Left (Main Book) | Right (Progress List) ---
  const int row0Y = yStatusBar;

  // 1. Main Book Card (Left)
  const int bookX = margin;
  const int bookY = row0Y;
  const int bookW = midX - 1.5 * margin;
  const int bookH = topH;
  const bool bookSelected = (selectorIndex == 0);

  // Draw Container
  if (bookSelected) {
    renderer.drawRect(bookX, bookY, bookW, bookH, true);
    renderer.drawRect(bookX + 1, bookY + 1, bookW - 2, bookH - 2, true);
    renderer.drawRect(bookX + 2, bookY + 2, bookW - 4, bookH - 4, true);
  } else {
    renderer.drawRect(bookX, bookY, bookW, bookH, true);
  }

  if (hasContinueReading && !recentBooks.empty()) {
    const auto &info = recentBooks[0];

    // Draw Cover (Scaled to fit)
    bool coverDrawn = false;
    if (info.hasCoverImage && !info.coverBmpPath.empty()) {
      FsFile file;
      if (SdMan.openFileForRead("HOME", info.coverBmpPath, file)) {
        Bitmap bitmap(file);
        if (bitmap.parseHeaders() == BmpReaderError::Ok) {
          // Try to fill as much as possible while maintaining aspect ratio
          renderer.drawBitmap(bitmap, bookX + 5, bookY + 5, bookW - 10,
                              bookH - 10);
          coverDrawn = true;
        }
        file.close();
      }
    }

    if (!coverDrawn) {
      renderer.drawCenteredText(UI_12_FONT_ID, bookY + bookH / 2 - 10,
                                "BÌA SÁCH", true);
    }

    // Overlay "ĐỌC TIẾP" at the bottom of the card
    const int overlayH = 45;
    const int overlayY = bookY + bookH - overlayH - 5;
    renderer.fillRect(bookX + 5, overlayY, bookW - 10, overlayH, true);

    // Manual centering for "ĐỌC TIẾP" within the book box
    const char *dtLabel = "ĐỌC TIẾP";
    int dtW =
        renderer.getTextWidth(UI_12_FONT_ID, dtLabel, EpdFontFamily::BOLD);
    renderer.drawText(UI_12_FONT_ID, bookX + (bookW - dtW) / 2, overlayY + 12,
                      dtLabel, false, EpdFontFamily::BOLD);

    // Title above the overlay
    std::string title = info.book.title;
    if (renderer.getTextWidth(UI_10_FONT_ID, title.c_str()) > bookW - 20) {
      while (renderer.getTextWidth(UI_10_FONT_ID, (title + "...").c_str()) >
                 bookW - 20 &&
             title.length() > 0) {
        StringUtils::utf8RemoveLastChar(title);
      }
      title += "...";
    }
    // Centering title above overlay
    int tW = renderer.getTextWidth(UI_10_FONT_ID, title.c_str());
    renderer.drawText(UI_10_FONT_ID, bookX + (bookW - tW) / 2, overlayY - 25,
                      title.c_str(), true);

  } else {
    renderer.drawCenteredText(UI_10_FONT_ID, bookY + bookH / 2, "Trống", true);
  }

  // 2. Progress List (Right)
  const int progX = midX + 0.5 * margin;
  const int progW = pageWidth - progX - margin;
  renderer.drawRect(progX, bookY, progW, bookH, true);

  // Center "TIẾN TRÌNH" header within its column
  const char *hLabel = "TIẾN TRÌNH";
  int hW = renderer.getTextWidth(UI_10_FONT_ID, hLabel, EpdFontFamily::BOLD);
  renderer.drawText(UI_10_FONT_ID, progX + (progW - hW) / 2, bookY + 10, hLabel,
                    true, EpdFontFamily::BOLD);

  int currentY = bookY + 50;                        // Increased start Y
  for (size_t i = 1; i < recentBooks.size(); ++i) { // Show other books
    if (currentY + 65 > bookY + bookH)
      break;

    const auto &book = recentBooks[i].book;

    // Title
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

    // Bar - increased gap from title (was 20, now 28)
    const int barY = currentY + 28;
    const int barH = 8;
    renderer.drawRect(progX + 10, barY, progW - 20, barH, true);
    int fillW = (progW - 22) * book.progress / 100;
    if (fillW > 0)
      renderer.fillRect(progX + 11, barY + 1, fillW, barH - 2, true);

    char pStr[8];
    snprintf(pStr, sizeof(pStr), "%d%%", book.progress);
    renderer.drawText(UI_10_FONT_ID, progX + 10, barY + barH + 4, pStr, true);

    currentY += 80; // Increased spacing between items (was 65, now 80)
  }

  // --- Grid Items (Middle & Bottom Rows) ---

  struct GridItem {
    const char *label;
    int index;
    const uint8_t *icon;
    int col; // 0=Left, 1=Right
    int row; // 1=Middle, 2=Bottom
  };

  GridItem items[] = {
      {"Tủ sách", 1, icon_books, 0, 1},
      {"Tệp", 2, icon_files, 1, 1},
      {hasOpdsUrl ? "Duyệt OPDS" : "Truyền tệp", 3, icon_network, 0, 2},
      {"Cài đặt", 4, icon_settings, 1, 2}};

  for (const auto &item : items) {
    int x = (item.col == 0) ? margin : midX + 0.5 * margin;
    int y = row0Y + topH + margin + (item.row - 1) * gridRowH;
    int w = (item.col == 0) ? midX - 1.5 * margin : pageWidth - x - margin;
    int h = gridRowH - 12; // Shorter to avoid hints

    bool selected = (selectorIndex == item.index);
    if (selected) {
      renderer.drawRect(x, y, w, h, true);
      renderer.drawRect(x + 1, y + 1, w - 2, h - 2, true);
      renderer.drawRect(x + 2, y + 2, w - 4, h - 4, true);
    } else {
      renderer.drawRect(x, y, w, h, true);
    }

    const int iconSize = 48;
    renderer.drawImage(item.icon, x + (w - iconSize) / 2,
                       y + (h - iconSize) / 2 - 10, iconSize, iconSize);

    // Manual centering for labels within grid buttons
    int lW = renderer.getTextWidth(UI_12_FONT_ID, item.label);
    renderer.drawText(UI_12_FONT_ID, x + (w - lW) / 2, y + h - 35, item.label,
                      true);
  }

  // --- Hint Bar ---
  const auto hints = mappedInput.mapLabels("", "Chọn", "Trái", "Phải");
  renderer.drawButtonHints(UI_10_FONT_ID, hints.btn1, hints.btn2, hints.btn3,
                           hints.btn4);

  // Draw "MrSlim" logo in the first slot (x=25, w=106) if it's empty
  if (hints.btn1 == nullptr || hints.btn1[0] == '\0') {
    const GfxRenderer::Orientation orig_orientation = renderer.getOrientation();
    renderer.setOrientation(GfxRenderer::Portrait);

    const int portraitHeight = renderer.getScreenHeight();
    constexpr int btnX = 25; // Position for button 1
    constexpr int btnW = 106;
    constexpr int btnYDist = 40;
    const int btnTopY = portraitHeight - btnYDist;
    constexpr int textYOff = 7;

    const int logoW = renderer.getTextWidth(SMALL_FONT_ID, "MrSlim");
    const int startX = btnX + (btnW - 1 - logoW) / 2;

    renderer.drawText(SMALL_FONT_ID, startX, btnTopY + textYOff, "MrSlim");

    renderer.setOrientation(orig_orientation);
  }

  // Battery
  const bool showBattery =
      SETTINGS.hideBatteryPercentage !=
      CrossPointSettings::HIDE_BATTERY_PERCENTAGE::HIDE_ALWAYS;
  const uint16_t percentage = battery.readPercentage();
  const std::string pctStr =
      showBattery ? std::to_string(percentage) + "%" : "";
  const int batteryX =
      pageWidth - 25 - renderer.getTextWidth(SMALL_FONT_ID, pctStr.c_str());
  ScreenComponents::drawBattery(renderer, batteryX, 10, showBattery);

  renderer.displayBuffer();
}
