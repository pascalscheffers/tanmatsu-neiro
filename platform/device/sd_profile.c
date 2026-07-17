// sd_profile.c — PROFILE-only SD filesystem throughput diagnostic.
#include "sd_profile.h"

#ifdef SYNTH_PROFILE

#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include "esp_err.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_memory_utils.h"
#include "esp_timer.h"
#include "esp_vfs_fat.h"

static const char TAG[] = "sdbench";

enum {
    SD_PROFILE_BYTES        = 128 * 1024,
    SD_PROFILE_BUFFER_BYTES = 32 * 1024,
    SD_PROFILE_PATH_BYTES   = 64,
    SD_PROFILE_WAV_OFFSET   = 44,
    SD_PROFILE_PAD_OFFSET   = 4096,
};

static int benchmark_case(FILE* stream, const uint8_t* source, size_t offset, const char* phase) {
    if (fseek(stream, (long)offset, SEEK_SET) != 0) {
        return errno != 0 ? errno : EIO;
    }
    const int64_t started_us = esp_timer_get_time();
    size_t        written    = 0;
    while (written < SD_PROFILE_BYTES) {
        if (fwrite(source, 1, SD_PROFILE_BUFFER_BYTES, stream) != SD_PROFILE_BUFFER_BYTES) {
            return errno != 0 ? errno : EIO;
        }
        written += SD_PROFILE_BUFFER_BYTES;
    }
    if (fflush(stream) != 0) {
        return errno != 0 ? errno : EIO;
    }
    const int64_t elapsed_us = esp_timer_get_time() - started_us;
    const int64_t elapsed_ms = (elapsed_us + 999) / 1000;
    const int64_t kib_per_s  = elapsed_us > 0 ? ((int64_t)SD_PROFILE_BYTES * 1000000) / (1024 * elapsed_us) : 0;
    ESP_LOGI(TAG, "phase=%s offset=%u chunk=%u bytes=%u elapsed_ms=%lld rate=%lld KiB/s", phase, (unsigned)offset,
             SD_PROFILE_BUFFER_BYTES, SD_PROFILE_BYTES, elapsed_ms, kib_per_s);
    return 0;
}

void sd_profile_run(const char* root) {
    static const size_t offsets[] = {SD_PROFILE_WAV_OFFSET, SD_PROFILE_PAD_OFFSET};
    char                paths[2][SD_PROFILE_PATH_BYTES];
    uint8_t*            source      = NULL;
    FILE*               stream      = NULL;
    int                 first_error = 0;
    const char*         failed_at   = NULL;

    if (root == NULL ||
        snprintf(paths[0], sizeof(paths[0]), "%s/.tanmatsu-sdbench-44.tmp", root) >= (int)sizeof(paths[0]) ||
        snprintf(paths[1], sizeof(paths[1]), "%s/.tanmatsu-sdbench-4096.tmp", root) >= (int)sizeof(paths[1])) {
        ESP_LOGE(TAG, "failed operation=path error=%d", EINVAL);
        return;
    }
    for (size_t case_index = 0; case_index < 2; ++case_index) {
        unlink(paths[case_index]);
        const esp_err_t result =
            esp_vfs_fat_create_contiguous_file(root, paths[case_index], SD_PROFILE_BYTES + offsets[case_index], true);
        if (result != ESP_OK) {
            first_error = (int)result;
            failed_at   = "preallocate";
            ESP_LOGE(TAG, "failed operation=preallocate offset=%u code=%d (%s)", (unsigned)offsets[case_index],
                     (int)result, esp_err_to_name(result));
            goto cleanup;
        }
    }

    source = heap_caps_malloc(SD_PROFILE_BUFFER_BYTES, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
    if (source == NULL) {
        first_error = ENOMEM;
        failed_at   = "allocate";
        goto cleanup;
    }
    memset(source, 0xa5, SD_PROFILE_BUFFER_BYTES);
    ESP_LOGI(TAG, "buffer source_dma=%u bytes=%u", esp_ptr_dma_capable(source) ? 1u : 0u, SD_PROFILE_BUFFER_BYTES);

    for (size_t phase_index = 0; phase_index < 2 && first_error == 0; ++phase_index) {
        const char* phase = phase_index == 0 ? "fresh" : "overwrite";
        for (size_t case_index = 0; case_index < 2; ++case_index) {
            stream = fopen(paths[case_index], "r+b");
            if (stream == NULL) {
                first_error = errno != 0 ? errno : EIO;
                failed_at   = "open";
                break;
            }
            first_error = benchmark_case(stream, source, offsets[case_index], phase);
            if (first_error != 0) failed_at = "write";
            if (fclose(stream) != 0 && first_error == 0) {
                first_error = errno != 0 ? errno : EIO;
                failed_at   = "close";
            }
            stream = NULL;
            if (first_error != 0) break;
        }
    }

cleanup:
    if (stream != NULL && fclose(stream) != 0 && first_error == 0) {
        first_error = errno != 0 ? errno : EIO;
        failed_at   = "close";
    }
    for (size_t case_index = 0; case_index < 2; ++case_index) {
        if (unlink(paths[case_index]) != 0 && errno != ENOENT && first_error == 0) {
            first_error = errno != 0 ? errno : EIO;
            failed_at   = "remove";
        }
    }
    heap_caps_free(source);
    if (first_error != 0) {
        ESP_LOGE(TAG, "failed operation=%s error=%d", failed_at, first_error);
    }
}

#else

void sd_profile_run(const char* root) {
    (void)root;
}

#endif
