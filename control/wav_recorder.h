// control/wav_recorder.h — control-thread SD WAV writer (ADR 0024).
#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef uint8_t wav_recorder_state_t;
enum {
    WAV_RECORDER_IDLE = 0,
    WAV_RECORDER_RECORDING,
    WAV_RECORDER_ERROR,
};

typedef uint8_t wav_recorder_error_t;
enum {
    WAV_RECORDER_ERROR_NONE = 0,
    WAV_RECORDER_ERROR_SD_UNAVAILABLE,
    WAV_RECORDER_ERROR_DIRECTORY,
    WAV_RECORDER_ERROR_NO_FILENAME,
    WAV_RECORDER_ERROR_OPEN,
    WAV_RECORDER_ERROR_WRITE,
    WAV_RECORDER_ERROR_RING_OVERFLOW,
    WAV_RECORDER_ERROR_SIZE_LIMIT,
};

// Call once per control-loop iteration. A latched error remains visible while
// want_record is true; releasing the request returns the recorder to idle.
void wav_recorder_service(bool want_record);

wav_recorder_state_t wav_recorder_state(void);
wav_recorder_error_t wav_recorder_error(void);

#ifdef __cplusplus
}
#endif
