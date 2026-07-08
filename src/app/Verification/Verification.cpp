#include "Verification.h"
#include "app/app.h"
#include "display/display.h"

#if defined(BOARD_ESP32_S3) && defined(SD_CS)
#include <SD.h>
#endif

#if defined(CARDPUTER_ADV)
#include "service/WifiEntry/WifiEntry.h"
#endif

// check firmware update
bool firmware_check()
{
#ifdef BOARD_ESP32_S3
    // load app status
    JsonDocument &app = status();

    // Check if there are firmware.bin in the SD card
    // FIRMWARE is defiined in platformio.ini
    _log("Checking for firmware update: %s\n", FIRMWARE);
    if (gfs()->exists(FIRMWARE))
    {
        // move to firmware update screen
        app["screen"] = UPDATESCREEN;
        _log("New firmware found. Preparing Update.\n");
        return true;
    }
#endif

    _log("No new firmware found.\n");
    return false;
}

// check if file system is correctly loaded
bool filesystem_check()
{
    //
    JsonDocument &app = status();

    // Call File System for the first time to initialize
    gfs();

#if defined(SD_CS)
    // Check if SD card is inserted
    uint8_t cardType = SD.cardType();
    if (cardType == CARD_NONE)
    {
        //
        app["error"] = "SD CARD NOT INSERTED\n";
        app["screen"] = ERRORSCREEN;

        //
        _log(app["error"]);

        return false;
    }

    //
    _log("SD Card detected\n");
#endif

#if defined(DEBUG_FILE)
    // Delete debug.log
    const char *debugPath = "/debug.log";
    if (gfs()->exists(debugPath))
    {
        if (gfs()->remove(debugPath))
        {
            _log("Deleted %s\n", debugPath);
        }
        else
        {
            _log("Failed to delete %s\n", debugPath);
        }
    }
#endif

    // file system check pass

#if defined(CARDPUTER_ADV)
    // Pre-load WiFi config now, while SD card access is safe (no concurrent
    // display rendering). Loading it later inside menu render paths crashes.
    _log("[Verification] Loading WiFi config from SD card...\n");
    wifi_config_load();

    // Show WiFi config status on screen
    JsonArray aps = app["wifi"]["access_points"].as<JsonArray>();
    _log("[Verification] WiFi APs loaded: %d\n", aps.size());

    if (aps.size() == 0) {
        _log("[Verification] WARNING: No WiFi configured!\n");
        _log("[Verification] Create /wifi.json on SD card root\n");
    }
#endif

    return true;
}
