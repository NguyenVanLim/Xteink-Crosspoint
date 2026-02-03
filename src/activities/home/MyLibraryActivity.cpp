#include "MyLibraryActivity.h"

#include <Epub.h> // Include proper header
#include <GfxRenderer.h>
#include <SDCardManager.h>

#include <algorithm>

#include "MappedInputManager.h"
#include "RecentBooksStore.h"
#include "ScreenComponents.h"
#include "fontIds.h"
#include "util/StringUtils.h"

namespace {
// Layout constants
constexpr int TAB_BAR_Y = 15;
constexpr int CONTENT_START_Y = 60;
constexpr int LINE_HEIGHT = 30; // For list view
constexpr int LEFT_MARGIN = 20;
constexpr int RIGHT_MARGIN = 40; // Extra space for scroll indicator

// Grid constants
constexpr int GRID_ROWS = 2;
constexpr int GRID_COLS = 2;
constexpr int GRID_ITEM_WIDTH = 250;
constexpr int GRID_ITEM_HEIGHT = 200; // Half screen height roughly?
// Screen is 600x800 or similar?
// 10.3 inch is usually 1404x1872.
// Let's check GfxRenderer.h or use percentages/calc.
// Wait, this device is smaller. ESP32-C3... likely 4-6 inch?
// "open-x4-sdk" suggests X4 (4 inch?).
// If 4 inch, screen might be 480x800 or 400x300?
// MyLibraryActivity.cpp: renderer.drawImage(CrossLarge, ... 128, 128)
// SleepActivity.cpp: y=117.
// Let's calculate dynamically in render.

// Timing thresholds
constexpr int SKIP_PAGE_MS = 700;
constexpr unsigned long GO_HOME_MS = 1000;

void sortFileList(std::vector<std::string> &strs) {
  std::sort(begin(strs), end(strs),
            [](const std::string &str1, const std::string &str2) {
              if (str1.back() == '/' && str2.back() != '/')
                return true;
              if (str1.back() != '/' && str2.back() == '/')
                return false;
              return lexicographical_compare(
                  begin(str1), end(str1), begin(str2), end(str2),
                  [](const char &char1, const char &char2) {
                    return tolower(char1) < tolower(char2);
                  });
            });
}
} // namespace

int MyLibraryActivity::getPageItems() const {
  // Default to Recent if unset or invalid (constructor sets initialTab)
  // Reorder default: Recent -> Files -> Books
  // No changes needed here logic-wise for getPageItems, just confirm logic is
  // generic.
  if (currentTab == Tab::Books) {
    return 4; // Fixed 2x2 grid
  }

  const int screenHeight = renderer.getScreenHeight();
  const int bottomBarHeight = 60; // Space for button hints
  const int availableHeight = screenHeight - CONTENT_START_Y - bottomBarHeight;
  int items = availableHeight / LINE_HEIGHT;
  if (items < 1) {
    items = 1;
  }
  return items;
}

int MyLibraryActivity::getCurrentItemCount() const {
  if (currentTab == Tab::Recent) {
    return static_cast<int>(bookTitles.size());
  } else if (currentTab == Tab::Books) {
    return static_cast<int>(gridBooks.size());
  }
  return static_cast<int>(files.size());
}

int MyLibraryActivity::getTotalPages() const {
  const int itemCount = getCurrentItemCount();
  const int pageItems = getPageItems();
  if (itemCount == 0)
    return 1;
  return (itemCount + pageItems - 1) / pageItems;
}

int MyLibraryActivity::getCurrentPage() const {
  const int pageItems = getPageItems();
  return selectorIndex / pageItems + 1;
}

void MyLibraryActivity::loadRecentBooks() {
  constexpr size_t MAX_RECENT_BOOKS = 20;

  bookTitles.clear();
  bookPaths.clear();
  const auto &books = RECENT_BOOKS.getBooks();
  bookTitles.reserve(std::min(books.size(), MAX_RECENT_BOOKS));
  bookPaths.reserve(std::min(books.size(), MAX_RECENT_BOOKS));

  for (const auto &book : books) {
    // Limit to maximum number of recent books
    if (bookTitles.size() >= MAX_RECENT_BOOKS) {
      break;
    }

    // Skip if file no longer exists
    if (!SdMan.exists(book.path.c_str())) {
      continue;
    }

    // Use stored title if available, otherwise extract from path
    std::string title = book.title;
    if (title.empty()) {
      title = book.path;
      const size_t lastSlash = title.find_last_of('/');
      if (lastSlash != std::string::npos) {
        title = title.substr(lastSlash + 1);
      }
    }

    bookTitles.push_back(title);
    bookPaths.push_back(book.path);
  }
}

void MyLibraryActivity::loadFiles() {
  files.clear();

  auto root = SdMan.open(basepath.c_str());
  if (!root || !root.isDirectory()) {
    if (root)
      root.close();
    return;
  }

  root.rewindDirectory();

  char name[500];
  for (auto file = root.openNextFile(); file; file = root.openNextFile()) {
    file.getName(name, sizeof(name));
    if (name[0] == '.' || strcmp(name, "System Volume Information") == 0) {
      file.close();
      continue;
    }

    if (file.isDirectory()) {
      files.emplace_back(std::string(name) + "/");
    } else {
      auto filename = std::string(name);
      if (StringUtils::checkFileExtension(filename, ".epub") ||
          StringUtils::checkFileExtension(filename, ".xtch") ||
          StringUtils::checkFileExtension(filename, ".xtc") ||
          StringUtils::checkFileExtension(filename, ".txt") ||
          StringUtils::checkFileExtension(filename, ".md")) {
        files.emplace_back(filename);
      }
    }
    file.close();
  }
  root.close();
  root.close();
  sortFileList(files);
}

void MyLibraryActivity::loadBooksFromDir() {
  gridBooks.clear();

  // Create /books and /cover directories if they don't exist
  if (!SdMan.exists("/books")) {
    SdMan.mkdir("/books");
  }
  if (!SdMan.exists("/cover")) {
    SdMan.mkdir("/cover");
  }

  auto root = SdMan.open("/books");
  if (!root || !root.isDirectory()) {
    if (root)
      root.close();
    return;
  }

  root.rewindDirectory();
  char name[500];
  while (auto file = root.openNextFile()) {
    if (file.isDirectory()) {
      file.close();
      continue;
    }

    file.getName(name, sizeof(name));
    file.close();

    std::string filename(name);
    if (filename[0] == '.')
      continue;

    if (StringUtils::checkFileExtension(filename, ".epub") ||
        StringUtils::checkFileExtension(filename, ".xtc") ||
        StringUtils::checkFileExtension(filename, ".xtch") ||
        StringUtils::checkFileExtension(filename, ".txt")) {

      // Create BookItem
      BookItem item;
      item.title = filename;
      item.path = std::string("/books/") + filename;

      // Use thumbnail for grid view
      Epub tempEpub(item.path, "/cover");
      item.coverPath = tempEpub.getThumbBmpPath();

      // Persistent check: if .no marker exists, don't try again
      if (SdMan.exists((item.coverPath + ".no").c_str())) {
        item.checked = true;
      } else if (SdMan.exists(item.coverPath.c_str())) {
        item.coverChecked = true;
        item.hasCover = true;
      }

      gridBooks.push_back(item);
    }
  }
  root.close();

  // Sort books alphabetically
  std::sort(
      gridBooks.begin(), gridBooks.end(),
      [](const BookItem &a, const BookItem &b) { return a.title < b.title; });
}

void MyLibraryActivity::ensureCoversForPage(int pageIndex) const {
  // Signal the background task
  // We cast away constness because we need to update the volatile flag
  const_cast<MyLibraryActivity *>(this)->pageToRequest = pageIndex;

  if (coverTaskHandle) {
    xTaskNotifyGive(coverTaskHandle);
  }
}

size_t MyLibraryActivity::findEntry(const std::string &name) const {
  for (size_t i = 0; i < files.size(); i++) {
    if (files[i] == name)
      return i;
  }
  return 0;
}

void MyLibraryActivity::taskTrampoline(void *param) {
  auto *self = static_cast<MyLibraryActivity *>(param);
  self->displayTaskLoop();
}

void MyLibraryActivity::coverTaskTrampoline(void *param) {
  auto *self = static_cast<MyLibraryActivity *>(param);
  self->coverTaskLoop();
}

void MyLibraryActivity::onEnter() {
  Activity::onEnter();

  renderingMutex = xSemaphoreCreateMutex();

  // Load data for both tabs
  // Load data for all tabs
  loadRecentBooks();
  loadFiles();
  loadBooksFromDir();

  // Ensure covers for the first page of books are ready
  // NOTE: Calling this in onEnter might still block slightly if covers are
  // being generated but with mutex protection it won't crash.
  if (currentTab == Tab::Books) {
    ensureCoversForPage(1);
  }

  selectorIndex = 0;
  generatingBookIndex = -1;
  updateRequired = true;

  xTaskCreate(&MyLibraryActivity::taskTrampoline, "MyLibraryActivityTask",
              4096, // Stack size (increased for epub metadata loading)
              this, // Parameters
              1,    // Priority
              &displayTaskHandle // Task handle
  );

  // Create low priority background task for cover generation
  // Stack size needs to be large enough for Epub processing
  xTaskCreate(&MyLibraryActivity::coverTaskTrampoline, "CoverGenTask",
              6144, // Generous stack for EPUB processing
              this,
              0, // Lowest priority
              &coverTaskHandle);
}

void MyLibraryActivity::onExit() {
  Activity::onExit();

  // Wait until not rendering to delete task to avoid killing mid-instruction to
  // EPD
  xSemaphoreTake(renderingMutex, portMAX_DELAY);
  if (displayTaskHandle) {
    vTaskDelete(displayTaskHandle);
    displayTaskHandle = nullptr;
  }

  if (coverTaskHandle) {
    vTaskDelete(coverTaskHandle);
    coverTaskHandle = nullptr;
  }

  vSemaphoreDelete(renderingMutex);
  renderingMutex = nullptr;

  bookTitles.clear();
  bookPaths.clear();
  files.clear();
  gridBooks.clear();
}

void MyLibraryActivity::loop() {
  const int itemCount = getCurrentItemCount();
  const int pageItems = getPageItems();

  // Long press BACK (1s+) in Files tab goes to root folder
  if (currentTab == Tab::Files &&
      mappedInput.isPressed(MappedInputManager::Button::Back) &&
      mappedInput.getHeldTime() >= GO_HOME_MS) {
    if (basepath != "/") {
      basepath = "/";
      loadFiles();
      selectorIndex = 0;
      updateRequired = true;
    }
    return;
  }

  const bool upReleased =
      mappedInput.wasReleased(MappedInputManager::Button::Up);
  const bool downReleased =
      mappedInput.wasReleased(MappedInputManager::Button::Down);
  const bool leftReleased =
      mappedInput.wasReleased(MappedInputManager::Button::Left);
  const bool rightReleased =
      mappedInput.wasReleased(MappedInputManager::Button::Right);

  const bool skipPage = mappedInput.getHeldTime() > SKIP_PAGE_MS;

  // Confirm button - open selected item
  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    if (currentTab == Tab::Recent) {
      if (!bookPaths.empty() &&
          selectorIndex < static_cast<int>(bookPaths.size())) {
        onSelectBook(bookPaths[selectorIndex], currentTab);
      }
    } else if (currentTab == Tab::Books) {
      if (!gridBooks.empty() &&
          selectorIndex < static_cast<int>(gridBooks.size())) {
        onSelectBook(gridBooks[selectorIndex].path, currentTab);
      }
    } else {
      // Files tab
      if (!files.empty() && selectorIndex < static_cast<int>(files.size())) {
        if (basepath.back() != '/')
          basepath += "/";
        if (files[selectorIndex].back() == '/') {
          // Enter directory
          basepath +=
              files[selectorIndex].substr(0, files[selectorIndex].length() - 1);
          loadFiles();
          selectorIndex = 0;
          updateRequired = true;
        } else {
          // Open file
          onSelectBook(basepath + files[selectorIndex], currentTab);
        }
      }
    }
    return;
  }

  // Back button
  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    if (mappedInput.getHeldTime() < GO_HOME_MS) {
      if (currentTab == Tab::Files && basepath != "/") {
        // Go up one directory, remembering the directory we came from
        const std::string oldPath = basepath;
        basepath.replace(basepath.find_last_of('/'), std::string::npos, "");
        if (basepath.empty())
          basepath = "/";
        loadFiles();

        // Select the directory we just came from
        const auto pos = oldPath.find_last_of('/');
        const std::string dirName = oldPath.substr(pos + 1) + "/";
        selectorIndex = static_cast<int>(findEntry(dirName));

        updateRequired = true;
      } else {
        // Go home
        onGoHome();
      }
    }
    return;
  }

  // Navigation Logic (Directional Grid)
  if (currentTab == Tab::Books) {
    if (leftReleased) {
      if (selectorIndex % 2 != 0) {
        selectorIndex--;
        updateRequired = true;
      }
    } else if (rightReleased) {
      if (selectorIndex % 2 == 0 && selectorIndex + 1 < itemCount) {
        selectorIndex++;
        updateRequired = true;
      }
    } else if (upReleased && itemCount > 0) {
      int newIndex = selectorIndex - 2;
      if (newIndex >= 0) {
        selectorIndex = newIndex;
        updateRequired = true;
      }
    } else if (downReleased && itemCount > 0) {
      int newIndex = selectorIndex + 2;
      if (newIndex < itemCount) {
        selectorIndex = newIndex;
        updateRequired = true;
      }
    }

    if (updateRequired) {
      ensureCoversForPage(getCurrentPage());
      return;
    }
  }

  // Navigation for other tabs (List View) - Left/Right disabled for tab
  // switching
  if (currentTab != Tab::Books) {
    const bool prevReleased = upReleased;
    const bool nextReleased = downReleased;

    if (prevReleased && itemCount > 0) {
      if (skipPage) {
        selectorIndex =
            ((selectorIndex / pageItems - 1) * pageItems + itemCount) %
            itemCount;
      } else {
        selectorIndex = (selectorIndex + itemCount - 1) % itemCount;
      }
      updateRequired = true;
    } else if (nextReleased && itemCount > 0) {
      if (skipPage) {
        selectorIndex =
            ((selectorIndex / pageItems + 1) * pageItems) % itemCount;
      } else {
        selectorIndex = (selectorIndex + 1) % itemCount;
      }
      updateRequired = true;
    }
  }
}

void MyLibraryActivity::displayTaskLoop() {
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

void MyLibraryActivity::coverTaskLoop() {
  while (true) {
    // Wait for a request (notification)
    // Returns nonzero if notified
    ulTaskNotifyTake(pdTRUE, portMAX_DELAY);

    int reqPage = pageToRequest;
    if (reqPage < 1)
      continue;

    const int pageItems = getPageItems();
    const int startIndex = (reqPage - 1) * pageItems;
    const int count = static_cast<int>(gridBooks.size());

    for (int i = startIndex; i < startIndex + pageItems && i < count; i++) {
      // Check if generation is still needed for this page?
      if (pageToRequest != reqPage)
        break;

      auto &book = gridBooks[i];
      if (book.checked)
        continue;

      // Use cached flag if already checked
      if (book.coverChecked)
        continue;

      // Check existence on disk (in case it was generated by another
      // session/process)
      if (SdMan.exists(book.coverPath.c_str())) {
        book.coverChecked = true;
        book.hasCover = true;
        continue;
      }

      // Found a missing cover. Mark as generating and request update to show
      // "Loading..."
      generatingBookIndex = i;
      updateRequired = true;

      // Small delay to allow UI to show "Loading..."
      vTaskDelay(100 / portTICK_PERIOD_MS);

      // Generate
      Serial.printf("[CoverGen] Generating for %s\n", book.title.c_str());

      bool success = false;
      // Heavy lifting with mutex lock for SD access safety
      if (renderingMutex) {
        xSemaphoreTake(renderingMutex, portMAX_DELAY);
        Epub epub(book.path, "/cover");
        if (StringUtils::checkFileExtension(book.path, ".epub")) {
          if (epub.load(true)) {
            success = epub.generateThumbBmp();
          }
        }

        if (!success) {
          // Persistent "No Cover" marker
          epub.setupCacheDir();
          FsFile noFile;
          if (SdMan.openFileForWrite("EPUB", book.coverPath + ".no", noFile)) {
            noFile.close();
          }
        }
        xSemaphoreGive(renderingMutex);
      }

      // Mark as checked regardless of success to prevent re-tries
      book.checked = true;
      book.coverChecked = true;
      book.hasCover = success;
      generatingBookIndex = -1;
      fullRedrawRequired = true;
      updateRequired = true;

      // Smaller delay between books for better performance
      vTaskDelay(50 / portTICK_PERIOD_MS);
    }
  }
}

void MyLibraryActivity::render() const {
  int currentPage = getCurrentPage();
  bool pageChanged = (currentPage != lastRenderedPage);
  bool tabOrPathChanged = fullRedrawRequired;

  if (pageChanged || tabOrPathChanged || generatingBookIndex != -1) {
    // FULL REDRAW
    renderer.clearScreen();

    // Section Header at top
    const char *sectionTitle = "THƯ VIỆN";
    if (currentTab == Tab::Recent)
      sectionTitle = "GẦN ĐÂY";
    else if (currentTab == Tab::Books)
      sectionTitle = "TỦ SÁCH";
    else if (currentTab == Tab::Files)
      sectionTitle = "TỆP";

    renderer.drawCenteredText(UI_12_FONT_ID, 15, sectionTitle, true,
                              EpdFontFamily::BOLD);
    renderer.drawRect(0, 45, renderer.getScreenWidth(), 2, true);

    // Draw content based on current tab
    if (currentTab == Tab::Recent) {
      renderRecentTab();
    } else if (currentTab == Tab::Books) {
      renderBooksGrid();
    } else {
      renderFilesTab();
    }

    // Draw scroll indicator
    const int screenHeight = renderer.getScreenHeight();
    const int contentHeight = screenHeight - CONTENT_START_Y - 60;
    ScreenComponents::drawScrollIndicator(
        renderer, currentPage, getTotalPages(), CONTENT_START_Y, contentHeight);

    // Draw button hints
    renderer.drawSideButtonHints(UI_10_FONT_ID, ">", "<");
    const auto labels = mappedInput.mapLabels("« Quay lại", "Mở", "<", ">");
    renderer.drawButtonHints(UI_10_FONT_ID, labels.btn1, labels.btn2,
                             labels.btn3, labels.btn4);

    fullRedrawRequired = false;
    lastRenderedPage = currentPage;
    lastRenderedSelectorIndex = selectorIndex;
    renderer.displayBuffer(EInkDisplay::FAST_REFRESH);
  } else if (selectorIndex != lastRenderedSelectorIndex) {
    // PARTIAL REDRAW (of buffer highlights)
    if (currentTab == Tab::Books) {
      // Books grid partial update
      const int pageItems = 4;
      const int cellWidth =
          (renderer.getScreenWidth() - LEFT_MARGIN - RIGHT_MARGIN) / 2;
      const int gridAreaHeight =
          renderer.getScreenHeight() - CONTENT_START_Y - 60;
      const int cellHeight = gridAreaHeight / 2;

      auto drawSelector = [&](int idx, bool state) {
        int i = idx % pageItems;
        int row = i / 2;
        int col = i % 2;
        int x = LEFT_MARGIN + col * cellWidth;
        int y = CONTENT_START_Y + row * cellHeight;
        int w = cellWidth - 10;
        int h = cellHeight - 10;
        // Bolder border (3 pixels)
        renderer.drawRect(x - 4, y - 4, w + 8, h + 8, state);
        renderer.drawRect(x - 5, y - 5, w + 10, h + 10, state);
        renderer.drawRect(x - 6, y - 6, w + 12, h + 12, state);
      };

      // Erase old border (white)
      drawSelector(lastRenderedSelectorIndex, false);
      // Draw new border (black)
      drawSelector(selectorIndex, true);
    } else {
      // List view partial update (Recent/Files)
      const int pageItems = getPageItems();
      const auto pageWidth = renderer.getScreenWidth();

      auto drawRowSelection = [&](int idx, bool state) {
        int lineY = CONTENT_START_Y + (idx % pageItems) * LINE_HEIGHT;
        // Erase highlight
        renderer.fillRect(0, lineY - 2, pageWidth - RIGHT_MARGIN, LINE_HEIGHT,
                          false);

        // Redraw title (inverted color if state is true)
        const std::string &title =
            (currentTab == Tab::Recent) ? bookTitles[idx] : files[idx];
        auto item =
            renderer.truncatedText(UI_10_FONT_ID, title.c_str(),
                                   pageWidth - LEFT_MARGIN - RIGHT_MARGIN);

        if (state) {
          // Re-draw highlight box
          renderer.fillRect(0, lineY - 2, pageWidth - RIGHT_MARGIN, LINE_HEIGHT,
                            true);
          // Draw text on top (white)
          renderer.drawText(UI_10_FONT_ID, LEFT_MARGIN, lineY, item.c_str(),
                            false);
        } else {
          // Draw text on white (black)
          renderer.drawText(UI_10_FONT_ID, LEFT_MARGIN, lineY, item.c_str(),
                            true);
        }
      };

      // Restore old row to normal (white bg, black text)
      drawRowSelection(lastRenderedSelectorIndex, false);
      // Draw new row as selected (black bg, white text)
      drawRowSelection(selectorIndex, true);
    }

    lastRenderedSelectorIndex = selectorIndex;
    renderer.displayBuffer(EInkDisplay::FAST_REFRESH);
  }
}

void MyLibraryActivity::renderRecentTab() const {
  const auto pageWidth = renderer.getScreenWidth();
  const int pageItems = getPageItems();
  const int bookCount = static_cast<int>(bookTitles.size());

  if (bookCount == 0) {
    renderer.drawText(UI_10_FONT_ID, LEFT_MARGIN, CONTENT_START_Y,
                      "Không có sách nào");
    return;
  }

  const auto pageStartIndex = selectorIndex / pageItems * pageItems;

  // Draw selection highlight
  renderer.fillRect(
      0, CONTENT_START_Y + (selectorIndex % pageItems) * LINE_HEIGHT - 2,
      pageWidth - RIGHT_MARGIN, LINE_HEIGHT);

  // Draw items
  for (int i = pageStartIndex; i < bookCount && i < pageStartIndex + pageItems;
       i++) {
    auto item = renderer.truncatedText(UI_10_FONT_ID, bookTitles[i].c_str(),
                                       pageWidth - LEFT_MARGIN - RIGHT_MARGIN);
    renderer.drawText(UI_10_FONT_ID, LEFT_MARGIN,
                      CONTENT_START_Y + (i % pageItems) * LINE_HEIGHT,
                      item.c_str(), i != selectorIndex);
  }
}

void MyLibraryActivity::renderFilesTab() const {
  const auto pageWidth = renderer.getScreenWidth();
  const int pageItems = getPageItems();
  const int fileCount = static_cast<int>(files.size());

  if (fileCount == 0) {
    renderer.drawText(UI_10_FONT_ID, LEFT_MARGIN, CONTENT_START_Y,
                      "Không tìm thấy sách");
    return;
  }

  const auto pageStartIndex = selectorIndex / pageItems * pageItems;

  // Draw selection highlight
  renderer.fillRect(
      0, CONTENT_START_Y + (selectorIndex % pageItems) * LINE_HEIGHT - 2,
      pageWidth - RIGHT_MARGIN, LINE_HEIGHT);

  // Draw items
  for (int i = pageStartIndex; i < fileCount && i < pageStartIndex + pageItems;
       i++) {
    auto item = renderer.truncatedText(UI_10_FONT_ID, files[i].c_str(),
                                       pageWidth - LEFT_MARGIN - RIGHT_MARGIN);
    renderer.drawText(UI_10_FONT_ID, LEFT_MARGIN,
                      CONTENT_START_Y + (i % pageItems) * LINE_HEIGHT,
                      item.c_str(), i != selectorIndex);
  }
}

void MyLibraryActivity::renderBooksGrid() const {
  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();
  const int pageItems = 4;
  const int bookCount = static_cast<int>(gridBooks.size());

  if (bookCount == 0) {
    renderer.drawText(UI_10_FONT_ID, LEFT_MARGIN, CONTENT_START_Y,
                      "Thư mục /books trống");
    return;
  }

  const auto pageStartIndex = selectorIndex / pageItems * pageItems;

  // Grid Dimensions
  const int bottomBarHeight = 60;
  const int gridAreaHeight = pageHeight - CONTENT_START_Y - bottomBarHeight;
  const int cellWidth = (pageWidth - LEFT_MARGIN - RIGHT_MARGIN) / 2;
  const int cellHeight = gridAreaHeight / 2;

  // Draw items
  for (int i = 0; i < pageItems; i++) {
    int index = pageStartIndex + i;
    if (index >= bookCount)
      break;

    const auto &book = gridBooks[index];
    int row = i / 2;
    int col = i % 2;

    int x = LEFT_MARGIN + col * cellWidth;
    int y = CONTENT_START_Y + row * cellHeight;
    int w = cellWidth - 10; // Padding
    int h = cellHeight - 10;

    // Selection highlight (Bolder)
    if (index == selectorIndex) {
      renderer.drawRect(x - 4, y - 4, w + 8, h + 8, true); // Outer border
      renderer.drawRect(x - 3, y - 3, w + 6, h + 6,
                        true); // Inner border (bold)
    }

    // Check if this book is currently generating a cover
    if (index == generatingBookIndex) {
      renderer.drawRect(x, y, w, h);                 // Border
      renderer.drawRect(x + 1, y + 1, w - 2, h - 2); // Bolder
      // Center the text roughly
      renderer.drawText(UI_10_FONT_ID, x + 20, y + h / 2 - 5, "Đang tải...");
      continue;
    }

    // Try to draw cover
    bool coverDrawn = false;
    if (book.hasCover) {
      FsFile file;
      if (SdMan.openFileForRead("LIB", book.coverPath, file)) {
        if (file.size() > 0) {
          Bitmap bitmap(file);
          if (bitmap.parseHeaders() == BmpReaderError::Ok) {
            // Calculate dimensions to fit/clip strictly within cell
            int bmpW = bitmap.getWidth();
            int bmpH = bitmap.getHeight();

            // Allow crop if larger, but ensure we don't draw outside [x, y, w,
            // h]
            int drawW = (bmpW > w) ? w : bmpW;
            int drawH = (bmpH > h) ? h : bmpH;

            // Center in cell
            int destX = x + (w - drawW) / 2;
            int destY = y + (h - drawH) / 2;

            // Draw with clipping (passing maxWidth/maxHeight to drawBitmap)
            renderer.drawBitmap(bitmap, destX, destY, drawW, drawH);
            coverDrawn = true;
          }
        }
        file.close();
      }
    }

    if (!coverDrawn) {
      // Fallback: Box with title (Bolder border)
      renderer.drawRect(x, y, w, h);
      renderer.drawRect(x + 1, y + 1, w - 2, h - 2); // Bold border

      // Center title in the cell
      auto title =
          renderer.truncatedText(UI_12_FONT_ID, book.title.c_str(), w - 20);
      int tW = renderer.getTextWidth(UI_12_FONT_ID, title.c_str());
      renderer.drawText(UI_12_FONT_ID, x + (w - tW) / 2, y + h / 2 - 5,
                        title.c_str());
    }
  }
}
