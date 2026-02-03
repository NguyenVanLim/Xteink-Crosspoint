#pragma once

class WifiService {
public:
  static WifiService &getInstance();

  // Initialize WiFi service
  void begin();

  // Attempt to connect to saved networks
  void autoConnect();

  // Check if WiFi is connected
  bool isConnected() const;

private:
  WifiService() = default;
  static WifiService instance;
  bool autoConnectAttempted = false;
};
