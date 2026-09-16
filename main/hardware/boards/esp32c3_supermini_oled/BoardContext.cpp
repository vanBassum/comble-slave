#include "BoardContext.h"
#include "esp_log.h"

void BoardContext::Init()
{
    auto init = initState_.TryBeginInit();
    if (!init)
    {
        ESP_LOGW(TAG, "Already initialized or initializing");
        return;
    }

    led_.Init();

    // A missing or dead panel is a warning, not a failure. Display::Init()
    // probes the address before it writes, so this says "no OLED" rather than
    // spraying write timeouts from inside the driver.
    displayOk_ = display_.Init();
    if (!displayOk_)
        ESP_LOGW(TAG, "No display — the adapter runs blind");

    init.SetReady();
    ESP_LOGI(TAG, "Initialized");
}
