//
#include "app.h"
#include "display/display.h"

//
#include "service/WordCounter/WordCounter.h"
#include "service/Send/Send.h"

#ifdef SD_CS
#include "app/FileSystem/FileSystemSD.h"
#endif

#ifdef USE_FAT
#include "app/FileSystem/FileSystemFAT.h"
#endif

#ifdef BOARD_PICO
#include "app/FileSystem/FileSystemRP2040.h"
#endif

#ifdef USE_MSC
#include "service/MassStorage/MassStorage.h"
#endif

#ifdef BOARD_ESP32_S3
#include "service/Sync/Sync.h"
#endif

#ifdef BATTERY
#include "service/Battery/Battery.h"
#endif

#ifdef USE_BLESERVER
#include "service/BLEServer/BLEServer.h"
#endif

#ifdef CARDPUTER
#include "M5Cardputer.h"
#endif

#if defined(USE_USB_DRIVE) && defined(BOARD_ESP32_S3)
#include "service/UsbDrive/UsbDrive.h"
#endif

// When app is not ready. Such as file system is not initialized
// then no more operation should occur.
// this is a flag to check if the app is ready
bool _ready = false;
bool app_ready()
{
    return _ready;
}

// USB-drive (export) mode flag, latched at boot (see app_setup).
bool _usbDrive = false;

// Pre-allocated DMA buffer for WiFi scan (see app_setup).
void *wifi_scan_reserve = nullptr;
bool usbdrive_mode()
{
    return _usbDrive;
}

// Main object for the setup is to initialize the device
// set up the serial communication
// file system will be initialized here
void app_setup()
{
    // Initialize status mutex for ESP32-S3 multi-core safety
#ifdef BOARD_ESP32_S3
    status_mutex_init();
#endif

    // Setup Serial Communication
    Serial.begin(115200);
#ifdef DEBUG
    delay(5000);
#endif

#ifdef CARDPUTER
    auto cfg = M5.config();
    M5Cardputer.begin(cfg, true); // enableKeyboard
#endif

#if defined(USE_USB_DRIVE) && defined(BOARD_ESP32_S3) && defined(CARDPUTER)
    // Hold 'e' (export) at power-on to expose the SD/TF card to a host PC as a
    // USB drive. In this mode the editor and USB-host keyboard are NOT started
    // (see main.cpp / usbdrive_mode), so only the USB host owns the SD.
    M5Cardputer.Display.setRotation(1);
    // Let the key matrix settle, then poll a few times so a held 'e' is seen
    // reliably at cold boot.
    bool exportHeld = false;
    for (int i = 0; i < 25 && !exportHeld; i++)
    {
        M5Cardputer.update();
        if (M5Cardputer.Keyboard.isKeyPressed('e'))
            exportHeld = true;
        delay(20);
    }
    if (exportHeld)
    {
        _usbDrive = true;
        JsonDocument &app = status();

        if (usbdrive_begin())
        {
            app["screen"] = USBDRIVESCREEN;
        }
        else
        {
            // no card / unreadable - report on screen
            app["error"] = "NO SD CARD.\nInsert a TF card, then reboot.\n";
            app["screen"] = ERRORSCREEN;
        }

        // App is "ready" enough to run the display loop for the status/error
        // screen; skip the rest of normal setup (config, editor, MSC, etc.).
        _ready = true;
        _log("USB drive (export) mode\n");
        return;
    }
#endif

#ifdef CARDPUTER
    // Reserve 32KB DMA memory at boot (while heap is contiguous) for WiFi TX.
    // Released in sync_start() before WiFi.begin().
    // Only allocate in normal mode (not in USB drive mode).
    extern void *wifi_scan_reserve;
    wifi_scan_reserve = heap_caps_malloc(32768, MALLOC_CAP_DMA);
    if (!wifi_scan_reserve) {
        _log("Failed to allocate DMA buffer for WiFi\n");
        JsonDocument &app = status();
        app["error"] = "Memory allocation failed.\nCannot use WiFi features.";
        app["screen"] = ERRORSCREEN;
        // Continue without WiFi capability - device still usable for basic editing
    }
#endif

#if defined(REV7)
    const int RX_PIN = 45;
    const int TX_PIN = 48;
    Serial1.begin(115200, SERIAL_8N1, RX_PIN, TX_PIN);
#endif
    _log("Setting Baud Rate: %d\n", 115200);

#if defined(BOARD_HAS_PSRAM)
    // Check if PSRAM is detected and enabled
    if (psramFound())
    {
        Serial.println("PSRAM is detected and enabled!");
        Serial.printf("Total heap: %d bytes\n", ESP.getHeapSize());
        Serial.printf("Total PSRAM: %d bytes\n", ESP.getPsramSize());
    }
    else
    {
        Serial.println("PSRAM not found or not initialized!");
    }

    // allocate memory is PSRAM
    heap_caps_malloc_extmem_enable(64);

    //
    Serial.println("Flash Chip Information:");
    Serial.printf("  Size: %u bytes (%.2f MB)\n", ESP.getFlashChipSize(), ESP.getFlashChipSize() / 1024.0 / 1024.0);
    Serial.printf("  Speed: %u Hz\n", ESP.getFlashChipSpeed());
    Serial.printf("  Mode: %u\n", ESP.getFlashChipMode());
#endif

    // File System Check
    if (filesystem_check() == false)
    {
        // if file system check fails then do not proceed further
        _log("File system check failed. Exiting setup.\n");
        _ready = true;  // Mark as ready so display_loop can show ERRORSCREEN
        return;
    }

    // Firmware Update Check
    if (firmware_check() == true)
    {
        // new firmare is found do not proceed further
        _log("Firmware update found. Exiting setup.\n");
        _ready = true;  // Mark as ready so display_loop can show UPDATESCREEN
        return;
    }

    // Load Configuration for the first time
    if (config_load() == false)
    {
        _log("Configuration load failed. Exiting setup.\n");
        _ready = true;  // Mark as ready so display_loop can show ERRORSCREEN
        return;
    }

#ifdef USE_MSC
    // Mass Storage Setup
    ms_setup();
#endif

#ifdef BATTERY
    battery_setup();
#endif

#if defined(REV5) || defined(CARDPUTER)
    // BLE Server Background Task
    BLEServer_init();
#endif

    // app ready
    _ready = true;
    _log("App is ready\n");
}

// This is a loop where background tasks will be placed
void app_loop()
{
    // wait until the app is ready
    if (app_ready() == false)
    {
        // wait for a while
        delay(100);
        return;
    }

#if defined(USE_USB_DRIVE) && defined(BOARD_ESP32_S3)
    // Export mode: only service the USB drive; the normal editor/services are
    // not running (and the SD belongs to the USB host).
    if (usbdrive_mode())
    {
        usbdrive_loop();
        return;
    }
#endif

    // word count
    wordcounter_service();

    // SEND feature
    send_loop();

#ifdef USE_BLESERVER
    // Pairing with BLE Keyboard Enabled
    BLEServer_loop();
#endif

#ifdef BATTERY
    battery_loop();
#endif

#ifdef USE_MSC
    // Mass Storage Control Loop
    ms_loop();
#endif

#ifdef BOARD_ESP32_S3
    sync_loop();    
#endif

}

// status storage
JsonDocument _status;
#ifdef BOARD_ESP32_S3
#include "freertos/semphr.h"
static SemaphoreHandle_t _status_mutex = nullptr;

// Initialize mutex once
void status_mutex_init() {
    if (!_status_mutex) {
        _status_mutex = xSemaphoreCreateRecursiveMutex();
    }
}

// Lock/Unlock helpers
void status_lock() {
    if (_status_mutex) {
        xSemaphoreTakeRecursive(_status_mutex, portMAX_DELAY);
    }
}

void status_unlock() {
    if (_status_mutex) {
        xSemaphoreGiveRecursive(_status_mutex);
    }
}
#endif
JsonDocument &status()
{
    return _status;
}

// file system instance
FileSystem *fileSystem = nullptr;
FileSystem *gfs()
{
    if (fileSystem == nullptr)
    {
// Initialize the file system here
#ifdef BOARD_PICO
        fileSystem = new FileSystemRP2040();
#endif

// ESP32-S3 boards with SD configured use SD; otherwise use internal SPIFFS.
#ifdef SD_CS

        // Use SD Card
        fileSystem = new FileSystemSD();

#endif

#ifdef USE_MSC

        // file system in FAT
        fileSystem = new FileSystemFAT("storage");

#endif

        if (!fileSystem->begin())
        {
            //
            JsonDocument &app = status();
            app["error"] = "File System Failed\n";
            app["screen"] = ERRORSCREEN; // ERROR SCREEN IS 0
            _log(app["error"]);
        }
        else
        {
            _log("File System Initialized\n");
        }
    }

    return fileSystem;
}
