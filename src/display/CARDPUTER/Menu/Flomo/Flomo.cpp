#include "Flomo.h"
#include "../Menu.h"
#include "app/app.h"
#include "display/display.h"

#include "service/Buffer/BufferService.h"
#include "service/Flomo/FlomoService.h"

// WiFi for connection check
#include <WiFi.h>

#include "display/CARDPUTER/display_CARDPUTER.h"

#define FLOMO_STATE_LIST    0
#define FLOMO_STATE_EDIT_EMAIL  1
#define FLOMO_STATE_EDIT_PASS   2
#define FLOMO_STATE_LOGIN       3
#define FLOMO_STATE_RESULT      4

static int flomo_state = FLOMO_STATE_LIST;
static String flomo_email;
static String flomo_password;
static String flomo_result;
static bool flomo_loading = false;

void Flomo_setup()
{
    buffer_clear();
    flomo_state = FLOMO_STATE_LIST;
    flomo_result = "";
    flomo_loading = false;

    // Load saved email/password from SD card
    flomo_email = "";
    flomo_password = "";
    File f = gfs()->open("/flomo.json", "r");
    if (f) {
        String data = f.readString();
        f.close();
        JsonDocument doc;
        if (!deserializeJson(doc, data)) {
            flomo_email = doc["email"].as<String>();
            flomo_password = doc["password"].as<String>();
        }
    }

    // Also load into _status
    JsonDocument &app = status();
    app["flomo"]["email"] = flomo_email;
    app["flomo"]["password"] = flomo_password;

    // Try loading saved token
    flomo_load_token();
}

void Flomo_render()
{
    JsonDocument &app = status();

    M5Cardputer.Display.setFont(&fonts::Font2);
    M5Cardputer.Display.setTextSize(1.0);

    if (flomo_state == FLOMO_STATE_LIST) {
        M5Cardputer.Display.setTextColor(TFT_WHITE, TFT_BLACK);
        M5Cardputer.Display.drawString("FLOMO SETTINGS", 0, 0);

        int y = 18;
        String haveToken = (flomo_get_token().length() > 0) ? " (logged in)" : "";
        M5Cardputer.Display.drawString(format("[1] Email: %s%s", flomo_email.c_str(), haveToken.c_str()), 0, y); y += 16;
        M5Cardputer.Display.drawString("[2] Password: ****", 0, y); y += 16;
        M5Cardputer.Display.drawString("[3] Login & Save", 0, y); y += 16;

        if (flomo_result.length() > 0) {
            M5Cardputer.Display.setTextColor(TFT_WHITE, flomo_result.startsWith("OK") ? TFT_GREEN : TFT_RED);
            M5Cardputer.Display.drawString(flomo_result, 0, y); y += 16;
        }
        M5Cardputer.Display.setTextColor(TFT_WHITE, TFT_BLACK);

        M5Cardputer.Display.drawString("[B] Back", 0, M5Cardputer.Display.height() - 18);
    }
    else if (flomo_state >= FLOMO_STATE_EDIT_EMAIL) {
        M5Cardputer.Display.setTextColor(TFT_WHITE, TFT_BLACK);
        if (flomo_state == FLOMO_STATE_EDIT_EMAIL) {
            M5Cardputer.Display.drawString("TYPE EMAIL:", 0, 0);
        } else {
            M5Cardputer.Display.drawString("TYPE PASSWORD:", 0, 0);
        }
        M5Cardputer.Display.drawString(format("  %s", buffer_get()), 0, 20);
        M5Cardputer.Display.drawString("[ENTER] Next  [B] Back", 0, M5Cardputer.Display.height() - 18);
    }
    else if (flomo_state == FLOMO_STATE_LOGIN) {
        M5Cardputer.Display.setTextColor(TFT_WHITE, TFT_BLACK);
        M5Cardputer.Display.drawString("LOGGING IN...", 0, 0);
    }
}

void Flomo_keyboard(char key)
{
    if (key == 0) return;

    if (flomo_state == FLOMO_STATE_LOGIN)
        return; // block input during login

    if (flomo_state >= FLOMO_STATE_EDIT_EMAIL) {
        // Editing mode
        if (key == '\n') {
            if (flomo_state == FLOMO_STATE_EDIT_EMAIL) {
                flomo_email = String(buffer_get());
                JsonDocument &app = status();
                app["flomo"]["email"] = flomo_email;
                buffer_clear();
                flomo_state = FLOMO_STATE_EDIT_PASS;
            } else if (flomo_state == FLOMO_STATE_EDIT_PASS) {
                flomo_password = String(buffer_get());
                JsonDocument &app = status();
                app["flomo"]["password"] = flomo_password;
                buffer_clear();
                flomo_state = FLOMO_STATE_LIST;
            }
            Menu_clear();
            return;
        }
        else if (key == '\b') {
            buffer_remove();
            Menu_clear();
            return;
        }
        else if (key == 'B' || key == 'b' || key == 27) {
            buffer_clear();
            flomo_state = FLOMO_STATE_LIST;
            Menu_clear();
            return;
        }
        else {
            buffer_add(key);
            Menu_clear();
            return;
        }
    }

    // List mode
    if (key == 'B' || key == 'b' || key == 27) {
        JsonDocument &app = status();
        app["menu"]["state"] = MENU_HOME;
        return;
    }
    else if (key == '1') {
        buffer_clear();
        flomo_state = FLOMO_STATE_EDIT_EMAIL;
        Menu_clear();
    }
    else if (key == '2') {
        buffer_clear();
        flomo_state = FLOMO_STATE_EDIT_PASS;
        Menu_clear();
    }
    else if (key == '3') {
        // Login to Flomo and save token
        if (flomo_email.length() == 0 || flomo_password.length() == 0) {
            flomo_result = "Set email + password first";
            Menu_clear();
            return;
        }

        // Check if WiFi is connected, if not, connect now
        if (WiFi.status() != WL_CONNECTED) {
            flomo_result = "Connecting WiFi...";
            Menu_clear();

            M5Cardputer.Display.setFont(&fonts::Font2);
            M5Cardputer.Display.setTextSize(1.0);
            M5Cardputer.Display.setTextColor(TFT_WHITE, TFT_BLACK);
            M5Cardputer.Display.drawString("Connecting WiFi...", 0, 48);

            // Load WiFi config and connect
            JsonDocument &app = status();
            JsonArray savedAccessPoints = app["wifi"]["access_points"].as<JsonArray>();

            if (savedAccessPoints.size() == 0) {
                flomo_result = "No WiFi config!";
                M5Cardputer.Display.setTextColor(TFT_WHITE, TFT_RED);
                M5Cardputer.Display.drawString("No WiFi saved!", 0, 64);
                M5Cardputer.Display.setTextColor(TFT_YELLOW, TFT_BLACK);
                M5Cardputer.Display.drawString("Use WiFi menu first", 0, 80);
                M5Cardputer.Display.setTextColor(TFT_WHITE, TFT_BLACK);
                delay(3000);
                Menu_clear();
                return;
            }

            WiFi.mode(WIFI_STA);
            delay(500);

            bool connected = false;
            for (int i = 0; i < (int)savedAccessPoints.size() && !connected; i++) {
                const char *ssid = savedAccessPoints[i]["ssid"].as<const char*>();
                const char *pass = savedAccessPoints[i]["password"].as<const char*>();

                if (!ssid || !ssid[0]) continue;

                M5Cardputer.Display.drawString(format("Try: %s", ssid), 0, 64);
                delay(500);

                WiFi.begin(ssid, pass ? pass : "");
                WiFi.setSleep(false);

                // Try to connect for up to 10 seconds
                for (int attempt = 0; attempt < 100; attempt++) {
                    delay(100);
                    if (WiFi.status() == WL_CONNECTED) {
                        connected = true;
                        break;
                    }
                }

                if (!connected) {
                    WiFi.disconnect();
                    delay(500);
                }
            }

            if (!connected) {
                WiFi.mode(WIFI_OFF);  // Clean up WiFi resources
                flomo_result = "WiFi failed!";
                M5Cardputer.Display.setTextColor(TFT_WHITE, TFT_RED);
                M5Cardputer.Display.drawString("WiFi connect failed!", 0, 80);
                M5Cardputer.Display.setTextColor(TFT_YELLOW, TFT_BLACK);
                M5Cardputer.Display.drawString("Check WiFi config", 0, 96);
                M5Cardputer.Display.setTextColor(TFT_WHITE, TFT_BLACK);
                delay(3000);
                Menu_clear();
                return;
            }

            // WiFi connected, sync time
            M5Cardputer.Display.setTextColor(TFT_GREEN, TFT_BLACK);
            M5Cardputer.Display.drawString("WiFi OK!", 0, 80);
            M5Cardputer.Display.setTextColor(TFT_WHITE, TFT_BLACK);
            delay(1000);

            configTime(8 * 3600, 0, "pool.ntp.org", "time.nist.gov");
            M5Cardputer.Display.drawString("Time synced", 0, 96);
            delay(500);
        }

        // Show login in progress
        flomo_result = "Logging in...";
        Menu_clear();

        // Save email/password to SD card
        JsonDocument &app = status();
        app["flomo"]["email"] = flomo_email;
        app["flomo"]["password"] = flomo_password;

        {
            JsonDocument sd;
            sd["email"] = flomo_email;
            sd["password"] = flomo_password;
            File f = gfs()->open("/flomo.json", FILE_WRITE);
            if (f) { serializeJson(sd, f); f.close(); }
        }

        // Try to login and get token (WiFi is now connected, NTP synced)
        flomo_state = FLOMO_STATE_LOGIN;
        Menu_clear();

        // Login now (blocking)
        String token = flomo_login(flomo_email, flomo_password);

        if (token.length() > 0) {
            flomo_result = "OK! Token saved.";
            _log("[Flomo] Login successful in menu\n");

            // Disconnect WiFi after login (save power)
            WiFi.mode(WIFI_OFF);
        } else {
            flomo_result = "Login failed!";
            _log("[Flomo] Login failed in menu\n");
            WiFi.mode(WIFI_OFF);
        }

        flomo_state = FLOMO_STATE_LIST;
        Menu_clear();
    }
}
