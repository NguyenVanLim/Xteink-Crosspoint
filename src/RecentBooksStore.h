#pragma once
#include <string>
#include <vector>

#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

class RecentBooksStore {
  // Static instance
  static RecentBooksStore instance;

public:
  struct RecentBook {
    std::string path;
    std::string title;
    std::string author;
    int progress = 0; // 0-100
  };

private:
  std::vector<RecentBook> recentBooks;

public:
  ~RecentBooksStore() = default;

  // Get singleton instance
  static RecentBooksStore &getInstance() { return instance; }

  // Add a book path to the recent list (moves to front if already exists)
  void addBook(const std::string &path, const std::string &title = "",
               const std::string &author = "");

  // Update progress for a book
  void updateProgress(const std::string &path, int progress,
                      const std::string &title = "",
                      const std::string &author = "");

  // Get the list of recent book objects (most recent first)
  const std::vector<RecentBook> &getBooks() const { return recentBooks; }

  // Get the count of recent books
  int getCount() const { return static_cast<int>(recentBooks.size()); }

  // Clear all recent books
  void clear() { recentBooks.clear(); }

  bool saveToFile() const;

  bool loadFromFile();
};

// Helper macro to access recent books store
#define RECENT_BOOKS RecentBooksStore::getInstance()
