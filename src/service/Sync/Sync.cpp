#include "Sync.h"
#include "app/app.h"
#include "display/display.h"
#include "service/Editor/Editor.h"
#include "service/Flomo/FlomoService.h"

//
#include <HTTPClient.h>
#include <base64.h>
#include <esp_task_wdt.h>
#include <esp_wifi.h>
#include <FS.h>

#if defined(CARDPUTER_ADV)
#include "display/CARDPUTER/display_CARDPUTER.h"
#endif

// Named constants for better code readability
constexpr size_t WIFI_HEAP_MINIMUM = 65536;       // Minimum heap for WiFi stack (64KB)
constexpr int WIFI_CONNECT_TIMEOUT_MS = 1000;   // Milliseconds between connection status checks
constexpr int WIFI_CONNECT_MAX_ATTEMPTS = 100;  // Max attempts (10 seconds total)
constexpr int WIFI_STABILIZE_DELAY_MS = 2000;  // Wait for WiFi to stabilize after connection
constexpr int WIFI_AP_DISPLAY_DELAY_MS = 1000; // Delay to show AP attempt on screen
constexpr int WIFI_FAILURE_DISPLAY_MS = 2000;  // Show failure message duration
constexpr int NTP_TIMEZONE_OFFSET = 8 * 3600;   // UTC+8 timezone offset in seconds
constexpr size_t SYNC_HEAP_MARGIN = 20480;     // Heap margin for HTTP operations (20KB)

// Reset all the sync related flags
void sync_init()
{
    // reset the sync state
    // update app sync state
    JsonDocument &app = status();
    app["sync_state"] = SYNC_START;
    app["sync_error"] = "";
    app["sync_message"] = "";

    _log("[sync_init] sync_init\n");
}

// request background service to pick up the request
void sync_start_request()
{
    JsonDocument &app = status();
    String task = app["task"].as<String>();
    //
    if (task != "sync_start")
    {
        app["task"] = "sync_start";
        _log("[sync_start_request] Sync Start Requested\n");
    }
}

//
void sync_loop()
{
    // Sync is now handled by Sync_process_in_loop() on Core 0.
    // Reading _status here on Core 1 races with display rendering on Core 0.
    return;
}

// Start Sync Process
// Search for WIFI ACCESS POINTS
void sync_start()
{
    //
    JsonDocument &app = status();

    //
    _log("[sync_start] Sync Start\n");

    // Check if WiFi is available (DMA buffer was allocated at boot)
    bool wifiAvailable = app["wifi_available"].as<bool>();
    if (!wifiAvailable) {
        app["sync_error"] = "WiFi not available.\nMemory allocation failed at boot.\nTry rebooting device.";
        app["sync_state"] = SYNC_ERROR;
        app["clear"] = true;
        _log("[sync_start] WiFi unavailable - DMA buffer allocation failed\n");
        return;
    }

    //
    app["sync_state"] = SYNC_STARTED;
    app["sync_message"] = "Connecting to WiFi";
    app["clear"] = true;

    // Check free heap before initializing WiFi stack (needs ~60 KB)
    size_t freeHeap = ESP.getFreeHeap();
    _log("[sync_start] free heap: %u bytes\n", (unsigned)freeHeap);
    if (freeHeap < WIFI_HEAP_MINIMUM)
    {
        app["sync_error"] = format("Low memory (%u B). Try reboot.", (unsigned)freeHeap);
        app["sync_state"] = SYNC_ERROR;
        app["clear"] = true;
        return;
    }

    // Release DMA reserve held since boot.
    extern void *wifi_scan_reserve;
    if (wifi_scan_reserve) { free(wifi_scan_reserve); wifi_scan_reserve = nullptr; }

    // Use standard Arduino WiFi API — same as official M5Cardputer demo.
    JsonArray savedAccessPoints = app["wifi"]["access_points"].as<JsonArray>();

    _log("[sync_start] Number of saved WiFi APs: %d\n", savedAccessPoints.size());

    // Update screen immediately with AP count
    app["sync_message"] = format("WiFi APs: %d", savedAccessPoints.size());
    app["clear"] = true;
    delay(WIFI_AP_DISPLAY_DELAY_MS); // Give time to display

    if (savedAccessPoints.size() == 0) {
        app["sync_error"] = "No saved WiFi. Set in WiFi menu.";
        app["sync_state"] = SYNC_ERROR;
        app["clear"] = true;
        _log("[sync_start] No WiFi APs saved!\n");
        delay(WIFI_FAILURE_DISPLAY_MS); // Show error for 2 seconds
        return;
    }

    WiFi.mode(WIFI_STA);
    delay(1000);

    bool connected = false;
    for (int i = 0; i < (int)savedAccessPoints.size(); i++) {
        const char *ssid = savedAccessPoints[i]["ssid"].as<const char*>();
        const char *pass = savedAccessPoints[i]["password"].as<const char*>();

        _log("[sync_start] AP[%d]: SSID='%s', has_password=%d\n", i, ssid ? ssid : "(null)", pass ? 1 : 0);

        if (!ssid || !ssid[0]) {
            _log("[sync_start] Skipping empty SSID at index %d\n", i);
            continue;
        }

        app["sync_message"] = format("Try %d/%d: %s", i+1, savedAccessPoints.size(), ssid);
        app["clear"] = true;
        delay(WIFI_AP_DISPLAY_DELAY_MS); // Show which AP we're trying

        _log("[sync_start] Connecting to '%s'...\n", ssid);

        // Start WiFi connection
        WiFi.begin(ssid, pass ? pass : "");
        WiFi.setSleep(false); // Disable WiFi sleep for better stability

        int attempt = 0;
        wl_status_t status;
        while (attempt < WIFI_CONNECT_MAX_ATTEMPTS) {
            delay(WIFI_CONNECT_TIMEOUT_MS);
            status = WiFi.status();

            if (attempt % 10 == 0) {  // Update screen every second
                app["sync_message"] = format("Try %s... %d/10s", ssid, attempt/10);
                app["clear"] = true;
            }

            _log("[sync_start] Attempt %d: status=%d\n", attempt, status);

            if (status == WL_CONNECTED) {
                connected = true;
                app["sync_message"] = format("Connected: %s", ssid);
                app["clear"] = true;
                _log("[sync_start] WiFi connected, stabilizing...\n");
                delay(WIFI_STABILIZE_DELAY_MS); // Wait for WiFi to stabilize
                configTime(NTP_TIMEZONE_OFFSET, 0, "pool.ntp.org", "time.nist.gov");
                _log("[sync_start] Time synced via NTP\n");
                sync_send();
                return;
            }
            attempt++;
        }

        _log("[sync_start] Failed to connect to '%s' (status=%d)\n", ssid, status);

        // Show failure reason on screen for 2 seconds
        app["sync_message"] = format("Failed: %s (err %d)", ssid, status);
        app["clear"] = true;

        // Show error explanation
        String errorDetail = "";
        if (status == 1) errorDetail = "WiFi not found";
        else if (status == 4) errorDetail = "Wrong password";
        else if (status == 6) errorDetail = "Disconnected";
        else errorDetail = format("Error code %d", status);

        M5Cardputer.Display.setFont(&fonts::Font2);
        M5Cardputer.Display.setTextSize(1.0);
        M5Cardputer.Display.setTextColor(TFT_YELLOW, TFT_BLACK);
        M5Cardputer.Display.drawString(errorDetail, 0, 32);
        M5Cardputer.Display.setTextColor(TFT_WHITE, TFT_BLACK);

        delay(2000); // Show failure reason for 2 seconds
        WiFi.disconnect();
        delay(500);
    }

    if (!connected) {
        sync_stop();
        app["sync_error"] = "NOT ABLE TO CONNECT TO WIFI";
        app["sync_state"] = SYNC_ERROR;
        app["clear"] = true;
        _log("[sync_start] All WiFi APs failed\n");

        // Show detailed failure info on screen
        M5Cardputer.Display.setFont(&fonts::Font2);
        M5Cardputer.Display.setTextSize(1.0);
        M5Cardputer.Display.setTextColor(TFT_WHITE, TFT_BLACK);
        M5Cardputer.Display.drawString("All 5 WiFi failed", 0, 48);
        M5Cardputer.Display.drawString("Check:", 0, 64);
        M5Cardputer.Display.drawString("1. WiFi password", 0, 80);
        M5Cardputer.Display.drawString("2. Router ON", 0, 96);
        M5Cardputer.Display.drawString("3. 2.4GHz (not 5GHz)", 0, 112);
    }
}

void sync_stop()
{
    WiFi.mode(WIFI_OFF);
    // Re-allocate DMA reserve for next WiFi use (no-PSRAM devices need a
    // contiguous DMA block, and WiFi deinit might not fully defragment).
    extern void *wifi_scan_reserve;
    if (!wifi_scan_reserve)
        wifi_scan_reserve = heap_caps_malloc(32768, MALLOC_CAP_DMA);
}

bool sync_connect_wifi(JsonDocument &app, const char *ssid, const char *password)
{
    // (unused, logic is inline in sync_start now)
    return false;
}

void sync_send()
{
    JsonDocument &app = status();

    // Load token from SPIFFS
    flomo_load_token();
    String token = flomo_get_token();

    if (token.length() == 0) {
        sync_stop();
        app["sync_error"] = "No Flomo token.";
        app["sync_state"] = SYNC_ERROR;
        app["clear"] = true;

        M5Cardputer.Display.setFont(&fonts::Font2);
        M5Cardputer.Display.setTextSize(1.0);
        M5Cardputer.Display.setTextColor(TFT_WHITE, TFT_RED);
        M5Cardputer.Display.drawString("No Token!", 0, 48);
        M5Cardputer.Display.setTextColor(TFT_YELLOW, TFT_BLACK);
        M5Cardputer.Display.drawString("Go to Flomo menu:", 0, 64);
        M5Cardputer.Display.drawString("Press [3] to login", 0, 80);
        M5Cardputer.Display.setTextColor(TFT_WHITE, TFT_BLACK);

        _log("[sync_send] No Flomo token\n");
        return;
    }

    // Save any unsaved edits before reading the complete file
    Editor &ed = Editor::getInstance();
    if (!ed.saved) {
        ed.saveFile();
    }

    // Read the complete file from disk
    String content;
    File file = gfs()->open(ed.fileName.c_str(), "r");
    if (file) {
        size_t fileSize = file.size();

        // Check heap availability (leave margin for HTTP operations)
        // Avoid integer overflow by checking separately
        size_t freeHeap = ESP.getFreeHeap();
        bool heapOk = false;

        if (fileSize > freeHeap) {
            // File is larger than available heap
            heapOk = false;
        } else if (freeHeap - fileSize < SYNC_HEAP_MARGIN) {
            // Not enough margin after loading file
            heapOk = false;
        } else {
            heapOk = true;
        }

        if (!heapOk) {
            sync_stop();
            app["sync_error"] = format("File too large (%u bytes). Free heap: %u bytes.",
                                       (unsigned)fileSize, (unsigned)freeHeap);
            app["sync_state"] = SYNC_ERROR;
            app["clear"] = true;
            _log("[sync_send] File too large: %u bytes, free heap: %u\n",
                 (unsigned)fileSize, (unsigned)freeHeap);
            file.close();
            return;
        }

        content = file.readString();
        file.close();
    } else {
        sync_stop();
        app["sync_error"] = "Cannot open file.";
        app["sync_state"] = SYNC_ERROR;
        app["clear"] = true;
        _log("[sync_send] Cannot open file: %s\n", ed.fileName.c_str());
        return;
    }

    if (content.length() == 0) {
        sync_stop();
        app["sync_error"] = "Empty document.";
        app["sync_state"] = SYNC_ERROR;
        app["clear"] = true;
        return;
    }

    app["sync_message"] = "Sending to Flomo...";
    app["clear"] = true;

    bool ok = flomo_send(content);

    sync_stop();
    if (ok) {
        app["sync_message"] = "Sent to Flomo!";
        app["sync_state"] = SYNC_COMPLETED;
        app["clear"] = true;
    } else {
        app["sync_error"] = "Flomo send failed.";
        app["sync_state"] = SYNC_ERROR;
        app["clear"] = true;
    }
}
