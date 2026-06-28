// main.c — device entry shim.
//
// All app logic lives in the portable app_run() (app/app.c), expressed purely
// through the platform HAL (platform.h). This file exists only because ESP-IDF
// requires an app_main() in the `main` component; it hands off immediately.
#include "app.h"

void app_main(void) {
    app_run();
}
