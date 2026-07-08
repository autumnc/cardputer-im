#include "WifiEntry.h"
#include "app/app.h"
#include "display/display.h"

#if defined(BOARD_ESP32_S3)
#include <WiFi.h>
#endif

// services
#include "service/Buffer/BufferService.h"

//
int wifi_config_status = 0;
int wifi_config_index = 0;

void WifiEntry_setup()
{
    buffer_clear();

    // wifi.json is pre-loaded at boot (filesystem_check). No SD access here
    // so we don't crash inside the display render path.

    wifi_config_status = WIFI_CONFIG_LIST;
}

//
void WifiEntry_keyboard(char key)
{
    // non printable keys are not going to be going through the buffer
    if(key == 0) return;

    //
    _debug("WifiEntry_keyboard key: [%d] %c, wifi_config_status: %d\n", key, key, wifi_config_status);

    //
    JsonDocument &app = status();

    //
    if (wifi_config_status >= WIFI_CONFIG_EDIT_SSID)
    {
        // SAVE or NEXT
        if (key == '\n')
        {
            // NEXT step
            if (wifi_config_status == WIFI_CONFIG_EDIT_SSID)
            {
                // save ssid
                JsonArray savedAccessPoints = app["wifi"]["access_points"].as<JsonArray>();
                savedAccessPoints[wifi_config_index]["ssid"] = String(buffer_get());

                // clear buffer
                buffer_clear();

                // move to password enter screen
                wifi_config_status = WIFI_CONFIG_EDIT_KEY;
                app["wifi_config_status"] = wifi_config_status;
            }
            else if (wifi_config_status == WIFI_CONFIG_EDIT_KEY)
            {
                // save ssid
                JsonArray savedAccessPoints = app["wifi"]["access_points"].as<JsonArray>();
                savedAccessPoints[wifi_config_index]["password"] = String(buffer_get());

                // save the configuration
                wifi_config_save();

                //
                buffer_clear();

                // go back to the configuration list
                wifi_config_status = WIFI_CONFIG_LIST;
                app["wifi_config_status"] = wifi_config_status;
            }
        }
        // BACK SPACE
        else if (key == '\b')
        {
            // backspace
            buffer_remove();
        }
        // ADD KEYS
        else
        {
            // edit mode
            buffer_add(key);

            //
            _debug("WifiEntry_keyboard buffer_add: %c, %s\n", key, buffer_get());
        }
    }
    else
    {
        // back to home
        if (key == 'B' || key == 'b')
        {
            //
            // go back to home menu
            app["menu"]["state"] = MENU_HOME;

            return;
        }

        else if (key >= '1' && key <= '5')
        {
            //
            // wifi entry chose to edit
            //

            // clear buffer
            buffer_clear();

            // determine the index to edit
            wifi_config_index = key - '1';
            _log("Wifi Entry chosen %d\n", wifi_config_index);
            app["wifi_config_index"] = wifi_config_index;

            // move the screen
            wifi_config_status = WIFI_CONFIG_EDIT_SSID;
            app["wifi_config_status"] = wifi_config_status;

            return;
        }
    }
}

// For safety concerns saving wifi information sd card can easily expose security information
// Wifi Configuration is saved internal storage of ESP32
void wifi_config_load()
{
    //
    JsonDocument &app = status();

    // load config.json
    _log("Opening wifi.json file from SD card root\n");
    File file = gfs()->open("/wifi.json", "r");
    if (file)
    {
        // read the file
        _log("Reading wifi.json file\n");
        String wifiString = file.readString();
        _log("wifi.json size: %d bytes\n", wifiString.length());
        _log("Closing wifi.json file\n");
        file.close();
        delay(100);

        // check if configString is empty
        if (wifiString.isEmpty())
        {
            // to avoid deserialization failure whem empty
            _log("wifi.json is empty!\n");
            wifiString = "{}";
        }

        // Prepare a JsonDocument for the configuration
        // The size should be adjusted according to your configuration's needs
        JsonDocument configDoc;

        // convert to JsonObject
        DeserializationError error = deserializeJson(configDoc, wifiString);
        _log("Deserializing wifi.json file\n");
        if (error)
        {
            //
            _log("wifi.json deserializeJson() failed: %s\n", error.c_str());

            //
            app["error"] = "Wrong format wifi.json";
            app["screen"] = ERRORSCREEN;

            // delete wifi.json from SD
            gfs()->remove("/wifi.json");

            return;
        }

        // Assign the loaded configuration to "config" property of app
        _log("Loading wifi config\n");
        app["wifi"] = configDoc.as<JsonObject>();

        // Debug: print loaded APs
        JsonArray aps = app["wifi"]["access_points"].as<JsonArray>();
        _log("Loaded %d WiFi APs\n", aps.size());
        for (int i = 0; i < aps.size(); i++) {
            const char* ssid = aps[i]["ssid"].as<const char*>();
            _log("  AP[%d]: %s\n", i, ssid ? ssid : "(null)");
        }

        // print out the configuration
        _log("Wifi config loaded successfully!\n");
    }
    else
    {
        // file doesn't exist
        _log("wifi.json NOT FOUND on SD card!\n");
        _log("Please create /wifi.json on SD card root\n");
        _log("Example format:\n");
        _log("{\"access_points\":[{\"ssid\":\"MyWiFi\",\"password\":\"pass123\"}]}\n");

        // Initialize empty wifi config
        app["wifi"]["access_points"] = JsonArray();

        return;
    }
}

// save current wifi configuration
void wifi_config_save()
{
    // load app status
    JsonDocument &app = status();

    // save config
    // Open the file for writing
    File file = gfs()->open("/wifi.json", FILE_WRITE);
    if (!file)
    {
        _log("Failed to open wifi.json file for writing.\n");
        return;
    }

    // Serialize the "config" property of the app Document directly to the file
    if (app["wifi"].is<JsonObject>())
    {
        String jsonOutput;
        serializeJsonPretty(app["wifi"], jsonOutput);
        file.println(jsonOutput);

        // debug
        _debug("wifi_config_save\n%s\n", jsonOutput.c_str());

        //
        _log("Wifi config updated successfully.\n");
    }
    else
    {
        _log("No 'wifi' property found in app Document.\n");
    }

    // close config.json
    file.close();
    delay(100);
}

// Connect to saved WiFi APs, returns true on success
// Automatically tries all saved APs with timeout
// Releases DMA reserve before connection (for ESP32-S3 no-PSRAM devices)
bool wifi_connect_saved()
{
    JsonDocument &app = status();

    // Check free heap before initializing WiFi stack (needs ~60 KB)
    size_t freeHeap = ESP.getFreeHeap();
    if (freeHeap < 65536) {
        _log("[wifi_connect] Insufficient heap: %u bytes\n", (unsigned)freeHeap);
        return false;
    }

    // Release DMA reserve held since boot (for ESP32-S3 no-PSRAM devices)
    extern void *wifi_scan_reserve;
    if (wifi_scan_reserve) {
        free(wifi_scan_reserve);
        wifi_scan_reserve = nullptr;
    }

    JsonArray savedAccessPoints = app["wifi"]["access_points"].as<JsonArray>();

    if (savedAccessPoints.size() == 0) {
        _log("[wifi_connect] No WiFi APs saved\n");
        return false;
    }

    WiFi.mode(WIFI_STA);
    delay(500);

    bool connected = false;
    for (int i = 0; i < (int)savedAccessPoints.size() && !connected; i++) {
        const char *ssid = savedAccessPoints[i]["ssid"].as<const char*>();
        const char *pass = savedAccessPoints[i]["password"].as<const char*>();

        if (!ssid || !ssid[0]) {
            continue;
        }

        _log("[wifi_connect] Trying AP[%d]: %s\n", i, ssid);

        WiFi.begin(ssid, pass ? pass : "");
        WiFi.setSleep(false);

        // Try to connect for up to 10 seconds
        for (int attempt = 0; attempt < 100; attempt++) {
            delay(100);
            if (WiFi.status() == WL_CONNECTED) {
                connected = true;
                _log("[wifi_connect] Connected to %s\n", ssid);
                break;
            }
        }

        if (!connected) {
            WiFi.disconnect();
            delay(500);
        }
    }

    if (connected) {
        // Sync NTP time after connection
        configTime(8 * 3600, 0, "pool.ntp.org", "time.nist.gov");
        _log("[wifi_connect] NTP time synced\n");
    } else {
        WiFi.mode(WIFI_OFF);
        _log("[wifi_connect] All WiFi APs failed\n");
    }

    return connected;
}
