#include "Sync.h"
#include "../Menu.h"
#include "app/app.h"
#include "display/display.h"
//
#include "service/WifiEntry/WifiEntry.h"
#include "service/Sync/Sync.h"
#include "service/Editor/Editor.h"
#include "service/Flomo/FlomoService.h"

//
#include "display/CARDPUTER/display_CARDPUTER.h"
#include <esp_task_wdt.h>

//
static bool sync_triggered = false;

void Sync_setup()
{
    _log("Sync_setup\n");
    Menu_clear();
    sync_triggered = false;
    sync_init();
}

void Sync_render()
{
    JsonDocument &app = status();

    // Use small built-in font to avoid U8g2 text-overflow crashes
    M5Cardputer.Display.setFont(&fonts::Font2);
    M5Cardputer.Display.setTextSize(1.0);
    M5Cardputer.Display.setTextColor(TFT_WHITE, TFT_BLACK);

    M5Cardputer.Display.drawString("SYNC & BACKUP", 0, 0);

    int sync_state = app["sync_state"].as<int>();

    _log("[Sync_render] state=%d\n", sync_state);

    if (sync_state == SYNC_START)
    {
        M5Cardputer.Display.drawString("Preparing...", 0, 16);

        // Check WiFi config immediately
        JsonArray aps = app["wifi"]["access_points"].as<JsonArray>();
        int count = aps.size();
        M5Cardputer.Display.drawString(format("WiFi APs: %d", count), 0, 32);

        if (count == 0) {
            M5Cardputer.Display.setTextColor(TFT_YELLOW, TFT_BLACK);
            M5Cardputer.Display.drawString("NO WIFI CONFIG!", 0, 48);
            M5Cardputer.Display.drawString("Check SD:/wifi.json", 0, 64);
        }

        sync_triggered = true;
    }
    else if (sync_state == SYNC_STARTED || sync_state == SYNC_PROGRESS)
    {
        const char *msg = app["sync_message"].as<const char*>();
        if (msg && msg[0])
            M5Cardputer.Display.drawString(msg, 0, 16);
        else
            M5Cardputer.Display.drawString("Working...", 0, 16);
    }
    else if (sync_state == SYNC_ERROR)
    {
        const char *error = app["sync_error"].as<const char*>();
        if (error && error[0])
        {
            M5Cardputer.Display.setTextColor(TFT_WHITE, TFT_RED);
            M5Cardputer.Display.drawString(error, 0, 16);
            M5Cardputer.Display.setTextColor(TFT_WHITE, TFT_BLACK);

            // Additional context
            if (strcmp(error, "NOT ABLE TO CONNECT TO WIFI") == 0) {
                M5Cardputer.Display.drawString("Check wifi.json on SD", 0, 32);
            }
            else if (strstr(error, "Flomo") != NULL) {
                M5Cardputer.Display.drawString("Check email/password", 0, 32);
                M5Cardputer.Display.drawString("Try login in Flomo menu", 0, 48);
            }

            M5Cardputer.Display.drawString("Waiting 5s...", 0, 64);
            delay(5000); // Wait 5 seconds so user can see error details
        }
    }
    else if (sync_state == SYNC_COMPLETED)
    {
        const char *msg = app["sync_message"].as<const char*>();
        if (msg && msg[0])
            M5Cardputer.Display.drawString(msg, 0, 16);
        else
            M5Cardputer.Display.drawString("Done.", 0, 16);

        // Show additional details for 5 seconds if successful
        if (msg && strstr(msg, "Sent") != NULL) {
            M5Cardputer.Display.setTextColor(TFT_WHITE, TFT_GREEN);
            M5Cardputer.Display.drawString("Success!", 0, 32);
            M5Cardputer.Display.setTextColor(TFT_WHITE, TFT_BLACK);
            M5Cardputer.Display.drawString("Check Flomo app", 0, 48);
            M5Cardputer.Display.drawString("Waiting 5s...", 0, 64);
            delay(5000); // Wait 5 seconds so user can see
        }
    }
}

void Sync_keyboard(char key)
{
    JsonDocument &app = status();
    int sync_state = app["sync_state"].as<int>();
    if (sync_state == SYNC_COMPLETED || sync_state == SYNC_ERROR)
    {
        app["screen"] = WORDPROCESSOR;
    }
}

void Sync_process_in_loop()
{
    if (!sync_triggered)
        return;
    sync_triggered = false;

    JsonDocument &app = status();
    int sync_state = app["sync_state"].as<int>();
    if (sync_state != SYNC_START)
        return;

    sync_start();
}
