// app.h — the portable application entry point.
//
// Both targets' thin entry shims call app_run(): device app_main() and the host
// main(). Everything app_run() does is expressed through platform.h, so the
// init + main-loop logic lives in exactly one place.
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

// Initialize the platform, start the engine + audio sink, then run the UI/input
// loop until a quit event. Returns when the app should exit.
void app_run(void);

#ifdef __cplusplus
}
#endif
