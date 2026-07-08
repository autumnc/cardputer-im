#include "Battery.h"
#include "app/app.h"

//
void battery_setup()
{
    analogReadResolution(12);
    pinMode(BATTERY, INPUT);
}

// Battery monitoring constants
// rp2040 still works around 2.0 v
const int batteryInterval = 60000;      // Battery check interval: 60 seconds
const int batteryMinimum = 2.4;         // Minimum safe voltage: 2.4V (for LiPo/LiIon)
const int batteryMaximum = 4.0;         // Maximum voltage: 4.0V (fully charged)

void battery_loop()
{
    static long lastMeasure = -(batteryInterval - 5000);
    if (millis() - lastMeasure >= batteryInterval)
    {
        lastMeasure = millis();
        int raw = analogRead(BATTERY);

        // Convert raw ADC value to voltage
        // 12-bit ADC: 0-4095 represents 0-3.6V reference
        // Voltage divider factor: 2 (battery voltage is halved before ADC)
        float voltage = (raw / 4095.0) * 3.6 * 2;

        // app status
        JsonDocument &app = status();
        float percent = (voltage - batteryMinimum) / (batteryMaximum - batteryMinimum) * 100;
        if (percent > 100.0f)
            percent = 100.0f;
        if (percent < 0.0f)
            percent = 0.0f;

        //
        app["battery"] = percent;

        // Log
        _debug("Battery %f %% voltage: %.2f V raw: %d\n", percent, voltage, raw);
    }
}