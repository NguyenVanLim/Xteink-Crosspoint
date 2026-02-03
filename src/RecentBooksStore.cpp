#include "RecentBooksStore.h"

#include <HardwareSerial.h>
#include <SDCardManager.h>
#include <Serialization.h>

#include <algorithm>

namespace {
constexpr uint8_t RECENT_BOOKS_FILE_VERSION = 2; // Incremented from 1
constexpr char RECENT_BOOKS_FILE[] = "/.crosspoint/recent_v2.bin";
constexpr int MAX_RECENT_BOOKS = 10;
} // namespace

RecentBooksStore RecentBooksStore::instance;

void RecentBooksStore::addBook(const std::string &path,
                               const std::string &title,
                               const std::string &author) {
  // Remove existing entry if present
  auto it = std::find_if(recentBooks.begin(), recentBooks.end(),
                         [&](const RecentBook &b) { return b.path == path; });

  RecentBook book;
  if (it != recentBooks.end()) {
    book = *it;
    recentBooks.erase(it);
  } else {
    book.path = path;
    book.title = title;
    book.author = author;
  }

  // Add to front
  recentBooks.insert(recentBooks.begin(), book);

  // Trim to max size
  if (recentBooks.size() > MAX_RECENT_BOOKS) {
    recentBooks.resize(MAX_RECENT_BOOKS);
  }

  saveToFile();
}

void RecentBooksStore::updateProgress(const std::string &path, int progress,
                                      const std::string &title,
                                      const std::string &author) {
  auto it = std::find_if(recentBooks.begin(), recentBooks.end(),
                         [&](const RecentBook &b) { return b.path == path; });

  if (it != recentBooks.end()) {
    it->progress = progress;
    if (!title.empty())
      it->title = title;
    if (!author.empty())
      it->author = author;
  } else {
    // If not in list, add it
    RecentBook book;
    book.path = path;
    book.title = title;
    book.author = author;
    book.progress = progress;
    recentBooks.insert(recentBooks.begin(), book);
    if (recentBooks.size() > MAX_RECENT_BOOKS) {
      recentBooks.resize(MAX_RECENT_BOOKS);
    }
  }

  saveToFile();
}

bool RecentBooksStore::saveToFile() const {
  // Make sure the directory exists
  SdMan.mkdir("/.crosspoint");

  FsFile outputFile;
  if (!SdMan.openFileForWrite("RBS", RECENT_BOOKS_FILE, outputFile)) {
    return false;
  }

  serialization::writePod(outputFile, RECENT_BOOKS_FILE_VERSION);
  const uint8_t count = static_cast<uint8_t>(recentBooks.size());
  serialization::writePod(outputFile, count);

  for (const auto &book : recentBooks) {
    serialization::writeString(outputFile, book.path);
    serialization::writeString(outputFile, book.title);
    serialization::writeString(outputFile, book.author);
    serialization::writePod(outputFile, book.progress);
  }

  outputFile.close();
  Serial.printf("[%lu] [RBS] Recent books saved to file (%d entries)\n",
                millis(), count);
  return true;
}

bool RecentBooksStore::loadFromFile() {
  FsFile inputFile;
  if (!SdMan.openFileForRead("RBS", RECENT_BOOKS_FILE, inputFile)) {
    // Try legacy version if v2 doesn't exist
    if (SdMan.openFileForRead("RBS", "/.crosspoint/recent.bin", inputFile)) {
      uint8_t v;
      serialization::readPod(inputFile, v);
      if (v == 1) {
        uint8_t count;
        serialization::readPod(inputFile, count);
        recentBooks.clear();
        for (uint8_t i = 0; i < count; i++) {
          RecentBook b;
          serialization::readString(inputFile, b.path);
          recentBooks.push_back(b);
        }
        inputFile.close();
        return true;
      }
      inputFile.close();
    }
    return false;
  }

  uint8_t version;
  serialization::readPod(inputFile, version);
  if (version != RECENT_BOOKS_FILE_VERSION) {
    Serial.printf("[%lu] [RBS] Deserialization failed: Unknown version %u\n",
                  millis(), version);
    inputFile.close();
    return false;
  }

  uint8_t count;
  serialization::readPod(inputFile, count);

  recentBooks.clear();
  recentBooks.reserve(count);

  for (uint8_t i = 0; i < count; i++) {
    RecentBook book;
    serialization::readString(inputFile, book.path);
    serialization::readString(inputFile, book.title);
    serialization::readString(inputFile, book.author);
    serialization::readPod(inputFile, book.progress);
    recentBooks.push_back(book);
  }

  inputFile.close();
  Serial.printf("[%lu] [RBS] Recent books loaded from file (%d entries)\n",
                millis(), count);
  return true;
}
