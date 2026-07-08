#include "FlomoService.h"
#include <SPIFFS.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <ArduinoJson.h>
#include <mbedtls/md5.h>
#include <algorithm>
#include <vector>
#include <time.h>
#include "app/app.h"

static const char *API_BASE = "https://flomoapp.com/api/v1";
static const char *API_KEY = "flomo_web";
static const char *APP_VERSION = "4.0";
static const char *PLATFORM = "web";
static const char *SIGN_SECRET = "dbbc3dd73364b4084c3a69346e0ce2b2";
static const char *TOKEN_PATH = "/flomo_token.txt";

static String _token;

// ── MD5 ────────────────────────────────────────────────────────────────
static String md5(const String &input)
{
    uint8_t digest[16];
    mbedtls_md5_ret((const uint8_t *)input.c_str(), input.length(), digest);

    char buf[33];
    for (int i = 0; i < 16; i++)
        sprintf(buf + i * 2, "%02x", digest[i]);
    buf[32] = '\0';
    return String(buf);
}

// ── Sign ────────────────────────────────────────────────────────────────
// Generate the MD5 sign for a set of key-value parameters.
// Keys must be sorted alphabetically. Empty values are skipped.
// The secret is appended to the raw query string before hashing.
static String generate_sign(std::vector<std::pair<String, String>> &params)
{
    // Sort by key
    std::sort(params.begin(), params.end(),
              [](const std::pair<String,String> &a, const std::pair<String,String> &b) { return a.first < b.first; });

    String raw;
    for (size_t i = 0; i < params.size(); i++) {
        const auto &p = params[i];
        if (p.second.length() == 0) continue;
        if (raw.length() > 0) raw += '&';
        raw += p.first + "=" + p.second;
    }
    raw += SIGN_SECRET;
    return md5(raw);
}

// ── Unix Timestamp ─────────────────────────────────────────────────────
static String get_timestamp()
{
    time_t now;
    time(&now);
    return String((unsigned long)now);
}

// ── Token I/O ───────────────────────────────────────────────────────────
bool flomo_load_token()
{
    if (_token.length() > 0) return true;
    if (!SPIFFS.begin(false)) return false;
    File f = SPIFFS.open(TOKEN_PATH, "r");
    if (!f) return false;
    _token = f.readStringUntil('\n');
    _token.trim();
    f.close();
    return _token.length() > 0;
}

void flomo_save_token(const String &token)
{
    _token = token;
    if (!SPIFFS.begin(false) && !SPIFFS.begin(true)) return;
    File f = SPIFFS.open(TOKEN_PATH, "w");
    if (!f) return;
    f.print(token);
    f.close();
}

void flomo_clear_token()
{
    _token = "";
    SPIFFS.begin(false);
    SPIFFS.remove(TOKEN_PATH);
}

String flomo_get_token()
{
    return _token;
}

// ── Secure HTTP helper ──────────────────────────────────────────────────
static bool http_post(const char *url, const String &body, String &resp)
{
    WiFiClientSecure client;
    client.setInsecure(); // skip cert verification
    client.setTimeout(30000); // 30 second timeout

    HTTPClient http;
    http.setTimeout(30000);
    http.begin(client, url);
    http.addHeader("Content-Type", "application/json");

    _log("[Flomo] POST %s\n", url);
    _log("[Flomo] Body: %s\n", body.c_str());

    int code = http.POST(body);

    _log("[Flomo] HTTP code: %d\n", code);

    if (code <= 0) {
        _log("[Flomo] POST failed: %s\n", http.errorToString(code).c_str());
        http.end();
        return false;
    }

    resp = http.getString();
    _log("[Flomo] Response: %s\n", resp.c_str());
    http.end();

    if (code != 200) {
        _log("[Flomo] HTTP error %d\n", code);
    }

    return code == 200;
}

static bool http_put(const char *url, const String &token, const String &body, String &resp)
{
    WiFiClientSecure client;
    client.setInsecure();
    client.setTimeout(30000);

    HTTPClient http;
    http.setTimeout(30000);
    http.begin(client, url);
    http.addHeader("Content-Type", "application/json");
    http.addHeader("Authorization", "Bearer " + token);

    _log("[Flomo] PUT %s\n", url);
    _log("[Flomo] Body: %s\n", body.c_str());

    int code = http.sendRequest("PUT", (uint8_t *)body.c_str(), body.length());

    _log("[Flomo] HTTP code: %d\n", code);

    if (code <= 0) {
        _log("[Flomo] PUT failed: %s\n", http.errorToString(code).c_str());
        http.end();
        return false;
    }

    resp = http.getString();
    _log("[Flomo] Response: %s\n", resp.c_str());
    http.end();

    if (code != 200) {
        _log("[Flomo] HTTP error %d\n", code);
    }

    return code == 200;
}

// ── Login ───────────────────────────────────────────────────────────────
String flomo_login(const String &email, const String &password)
{
    std::vector<std::pair<String, String>> params;
    params.push_back({"api_key", API_KEY});
    params.push_back({"app_version", APP_VERSION});
    params.push_back({"email", email});
    params.push_back({"password", password});
    params.push_back({"platform", PLATFORM});
    params.push_back({"timestamp", get_timestamp()});
    params.push_back({"wechat_oa_open_id", ""});
    params.push_back({"wechat_union_id", ""});
    params.push_back({"webp", "1"});
    String sign = generate_sign(params);
    params.push_back({"sign", sign});

    // Build JSON body
    JsonDocument doc;
    for (size_t i = 0; i < params.size(); i++) {
        const auto &p = params[i];
        if (p.second.length() == 0) continue;
        doc[p.first] = p.second;
    }
    String body;
    serializeJson(doc, body);

    String resp;
    if (!http_post((String(API_BASE) + "/user/login_by_email").c_str(), body, resp)) {
        _log("[Flomo] Login HTTP request failed\n");
        return "";
    }

    JsonDocument rdoc;
    DeserializationError err = deserializeJson(rdoc, resp);
    if (err) {
        _log("[Flomo] Login JSON parse error: %s\n", err.c_str());
        return "";
    }

    int apiCode = rdoc["code"].as<int>();
    if (apiCode != 0) {
        String msg = rdoc["message"].as<String>();
        _log("[Flomo] Login API error %d: %s\n", apiCode, msg.c_str());
        return "";
    }

    String token = rdoc["data"]["access_token"].as<String>();
    if (token.length() > 0) {
        flomo_save_token(token);
        _log("[Flomo] Login successful, token saved\n");
    }
    return token;
}

// ── Send ────────────────────────────────────────────────────────────────
bool flomo_send(const String &content)
{
    if (_token.length() == 0) {
        _log("[Flomo] Send failed: no token\n");
        return false;
    }

    _log("[Flomo] Sending %d bytes\n", content.length());

    // Convert plain text to simple HTML
    String html;
    int start = 0;
    while (start < content.length()) {
        int end = content.indexOf('\n', start);
        if (end < 0) end = content.length();
        String line = content.substring(start, end);
        line.trim();
        if (line.length() == 0)
            html += "<p><br></p>";
        else
            html += "<p>" + line + "</p>";
        start = end + 1;
    }

    std::vector<std::pair<String, String>> params;
    params.push_back({"api_key", API_KEY});
    params.push_back({"app_version", APP_VERSION});
    params.push_back({"content", html});
    params.push_back({"platform", PLATFORM});
    params.push_back({"source", "web"});
    params.push_back({"timestamp", get_timestamp()});
    params.push_back({"tz", "8:0"});
    params.push_back({"webp", "1"});
    String sign = generate_sign(params);
    params.push_back({"sign", sign});

    JsonDocument doc;
    for (size_t i = 0; i < params.size(); i++) {
        const auto &p = params[i];
        if (p.second.length() == 0) continue;
        doc[p.first] = p.second;
    }
    String body;
    serializeJson(doc, body);

    String resp;
    String url = String(API_BASE) + "/memo";
    if (!http_put(url.c_str(), _token, body, resp)) {
        _log("[Flomo] Send HTTP request failed\n");
        return false;
    }

    JsonDocument rdoc;
    if (deserializeJson(rdoc, resp)) {
        _log("[Flomo] Send JSON parse error\n");
        return false;
    }

    int apiCode = rdoc["code"].as<int>();
    if (apiCode == 0) {
        _log("[Flomo] Send successful\n");
        return true;
    } else {
        String msg = rdoc["message"].as<String>();
        _log("[Flomo] Send API error %d: %s\n", apiCode, msg.c_str());
        return false;
    }
}
