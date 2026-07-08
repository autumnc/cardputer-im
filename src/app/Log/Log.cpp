#include "Log.h"
#include "app/app.h"
#include <Arduino.h>

#ifdef BOARD_ESP32_S3
// #define COREID xPortGetCoreID()
#define COREID xPortGetCoreID()
#endif

#ifdef BOARD_PICO
#define COREID get_core_num()
#endif

//
// APP LOG
//
void _log(const char *format, ...)
{
    char message[256];
    va_list args;
    va_start(args, format);
    vsnprintf(message, sizeof(message), format, args);
    va_end(args);

    // Non-blocking: skip Serial when TX buffer is full so _log never hangs
    // the caller (e.g. wifi_config_load→WDT reset when no monitor attached).
    if (Serial.availableForWrite() >= 40)
        Serial.printf("[%d][%d] %s", COREID, millis(), message);

#if defined(REV7)
    if (Serial1.availableForWrite() >= 40)
        Serial1.printf("[%d][%d] %s", COREID, millis(), message);
#endif

#if defined(DEBUG_FILE)
    File f = gfs()->open("/debug.log", FILE_APPEND);
    if (f)
    {
        f.printf("[%d][%d] %s", COREID, millis(), message);
        f.close();
    }
#endif
}

//
// DEBUG LOG
//
void _debug(const char *format, ...)
{
#if defined(DEBUG)
    char message[256];
    va_list args;
    va_start(args, format);
    vsnprintf(message, sizeof(message), format, args);
    va_end(args);

    if (Serial.availableForWrite() >= 40)
        Serial.printf("[%d][%d] %s", COREID, millis(), message);
#endif
}
