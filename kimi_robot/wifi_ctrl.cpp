#include "config.h"
#include "wifi_ctrl.h"
#include <WiFi.h>

#include "wifi_secrets.h"
static const int wifiCount = sizeof(wifiList) / sizeof(wifiList[0]);

void wifiInit() {
  WiFi.mode(WIFI_STA);
  Serial.println("[WiFi] Scanning networks...");
  int n = WiFi.scanNetworks();
  Serial.printf("[WiFi] Found %d networks\n", n);

  const WifiCred* chosen = nullptr;
  if (n > 0) {
    for (int i = 0; i < n; i++) {
      Serial.printf("  - %s (%d dBm) %s\n",
                    WiFi.SSID(i).c_str(),
                    WiFi.RSSI(i),
                    (WiFi.encryptionType(i) == WIFI_AUTH_OPEN) ? "[open]" : "[secure]");
    }
    for (int k = 0; k < wifiCount && !chosen; k++) {
      for (int i = 0; i < n; i++) {
        if (WiFi.SSID(i) == wifiList[k].ssid) {
          chosen = &wifiList[k];
          Serial.printf("[WiFi] Matched '%s' (%d dBm)\n",
                        chosen->ssid, WiFi.RSSI(i));
          break;
        }
      }
    }
  }

  if (!chosen) {
    Serial.println("[WiFi] No known SSID found in scan, trying list in order...");
    for (int k = 0; k < wifiCount && !chosen; k++) {
      chosen = &wifiList[k];
    }
  }

  if (chosen) {
    Serial.printf("[WiFi] Connecting to '%s'...\n", chosen->ssid);
    WiFi.begin(chosen->ssid, chosen->pass);
    uint32_t t0 = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - t0 < 15000) {
      delay(400);
      Serial.print(".");
    }
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.printf("\n[WiFi] Connected! IP: %s (SSID: %s)\n",
                  WiFi.localIP().toString().c_str(),
                  WiFi.SSID().c_str());
  } else {
    Serial.println("\n[WiFi] All attempts failed, continuing without WiFi.");
  }
  WiFi.scanDelete();
}

bool wifiIsConnected() { return WiFi.status() == WL_CONNECTED; }
