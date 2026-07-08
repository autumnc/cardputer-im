#include "Wifi.h"
#include "../Menu.h"
#include "app/app.h"
#include "display/display.h"
#include "../../WordProcessor/WordProcessor.h"

//
#include "service/WifiEntry/WifiEntry.h"
#include "service/Buffer/BufferService.h"

//
#include "display/CARDPUTER/display_CARDPUTER.h"
extern const uint8_t u8g2_font_terminus28_tf[];

//

void Wifi_setup()
{
    WifiEntry_setup();
}

//

void Wifi_render()
{
    // Use small built-in font so all 7 lines fit on the 135px display
    M5Cardputer.Display.setFont(&fonts::Font2);
    M5Cardputer.Display.setTextSize(1.0);
    M5Cardputer.Display.setTextColor(TFT_WHITE, TFT_BLACK);

    // RENDER based on the screen index
    //
    JsonDocument &app = status();
    int wifi_config_status = app["wifi_config_status"].as<int>();
    if (wifi_config_status == WIFI_CONFIG_LIST)
    {
        Wifi_render_list();
    }
    else if (wifi_config_status >= WIFI_CONFIG_EDIT_SSID)
    {
        Wifi_render_edit();
    }
}

//

void Wifi_render_list()
{
    M5Cardputer.Display.drawString(" CHOOSE THE ENTRY TO EDIT: ", 0, 0);

    //
    JsonDocument &app = status();

    // initialize the config
    if (!app["wifi"].is<JsonObject>())
    {
        JsonObject wifi = app["wifi"].to<JsonObject>();
        app["wifi"] = wifi;
    }

    if (!app["wifi"]["access_points"].is<JsonArray>())
    {
        JsonArray access_points = app["wifi"]["access_points"].to<JsonArray>();
        app["wifi"]["access_points"] = access_points;
    }

    // Load saved WiFi connection information from the app["config"]["access_points"] array
    JsonArray savedAccessPoints = app["wifi"]["access_points"].as<JsonArray>();

    // Iterate through each available network
    int y = M5Cardputer.Display.fontHeight() + 4;
    for (int i = 0; i < 5; i++)
    {
        if (savedAccessPoints.size() > i)
        {
            // available wifi network
            const char *savedSsid = savedAccessPoints[i]["ssid"].as<const char *>();
            M5Cardputer.Display.drawString(format(" [%d] %s", i + 1, savedSsid ? savedSsid : ""), 0, y);
        }
        else
        {
            M5Cardputer.Display.drawString(format(" [%d]", i + 1), 0, y);
        }

        y += M5Cardputer.Display.fontHeight() + 2;
    }

    M5Cardputer.Display.drawString(" [B] BACK ", 0, y);
}

void Wifi_render_edit()
{
    //
    JsonDocument &app = status();
    int wifi_config_index = app["wifi_config_index"].as<int>();
    int wifi_config_status = app["wifi_config_status"].as<int>();

    // Load saved WiFi connection information from the app["config"]["access_points"] array
    JsonArray savedAccessPoints = app["wifi"]["access_points"].as<JsonArray>();

    const char *savedSsid = savedAccessPoints[wifi_config_index]["ssid"];
    const char *savedPassword = savedAccessPoints[wifi_config_index]["password"];

    int y = 0;
    M5Cardputer.Display.drawString(format(" EDIT [%d] WIFI CONFIG", wifi_config_index + 1), 0, y);

    if (wifi_config_status == WIFI_CONFIG_EDIT_SSID)
    {
        y += M5Cardputer.Display.fontHeight() + 4;
        M5Cardputer.Display.drawString(" TYPE SSID:", 0, y);

        y += M5Cardputer.Display.fontHeight() + 4;
        M5Cardputer.Display.drawString(format("      %s", buffer_get()), 0, y);

        y += M5Cardputer.Display.fontHeight() * 3;
        M5Cardputer.Display.drawString(" [ENTER] NEXT ", 0, y);

    }
    else if (wifi_config_status == WIFI_CONFIG_EDIT_KEY)
    {
        y += M5Cardputer.Display.fontHeight() + 4;
        M5Cardputer.Display.drawString(" TYPE WIFI KEY:", 0, y);

        y += M5Cardputer.Display.fontHeight() + 4;
        M5Cardputer.Display.drawString(format("      %s", buffer_get()), 0, y);

        y += M5Cardputer.Display.fontHeight() * 3;
        M5Cardputer.Display.drawString(" [ENTER] SAVE ", 0, y);
    }
}

//

void Wifi_keyboard(char key)
{
    //
    WifiEntry_keyboard(key);

    //
    Menu_clear();
}
