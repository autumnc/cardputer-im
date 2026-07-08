#pragma once

#include <Arduino.h>

// Returns cached token (empty if not logged in).
String flomo_get_token();

// Load token from SPIFFS. Returns true if a saved token exists.
bool flomo_load_token();

// Save token to SPIFFS.
void flomo_save_token(const String &token);

// Login to Flomo with email + password. Returns access_token on success,
// empty string on failure.
String flomo_login(const String &email, const String &password);

// Send text content to Flomo as a new memo. Returns true on success.
bool flomo_send(const String &content);

// Clear saved token (logout).
void flomo_clear_token();
