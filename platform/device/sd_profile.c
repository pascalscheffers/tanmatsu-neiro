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
};

static int benchmark_case(FILE* stream, const uint8_t* source, size_t chunk_bytes, const char* mode) {
    const int64_t started_us = esp_timer_get_time();
    size_t        written    = 0;
    while (written < SD_PROFILE_BYTES) {
        if (fwrite(source, 1, chunk_bytes, stream) != chunk_bytes) {
            return errno != 0 ? errno : EIO;
        }
        written += chunk_bytes;
    }
    if (fflush(stream) != 0) {
        return errno != 0 ? errno : EIO;
    }
    const int64_t elapsed_us = esp_timer_get_time() - started_us;
    const int64_t elapsed_ms = (elapsed_us + 999) / 1000;
    const int64_t kib_per_s  = elapsed_us > 0 ? ((int64_t)SD_PROFILE_BYTES * 1000000) / (1024 * elapsed_us) : 0;
    ESP_LOGI(TAG, "mode=%s chunk=%u bytes=%u elapsed_ms=%lld rate=%lld KiB/s", mode, (unsigned)chunk_bytes,
             SD_PROFILE_BYTES, elapsed_ms, kib_per_s);
    return 0;
}

void sd_profile_run(const char* root) {
    static const size_t chunk_sizes[] = {4 * 1024, 8 * 1024, 16 * 1024, 32 * 1024};
    char                path[SD_PROFILE_PATH_BYTES];
    uint8_t*            source       = NULL;
    uint8_t*            stream_cache = NULL;
    FILE*               stream       = NULL;
    int                 first_error  = 0;
    const char*         failed_at    = NULL;

    if (root == NULL || snprintf(path, sizeof(path), "%s/.tanmatsu-sdbench.tmp", root) >= (int)sizeof(path)) {
        ESP_LOGE(TAG, "failed operation=path error=%d", EINVAL);
        return;
    }
    unlink(path);
    const esp_err_t prealloc_result = esp_vfs_fat_create_contiguous_file(root, path, SD_PROFILE_BYTES, true);
    if (prealloc_result != ESP_OK) {
        ESP_LOGE(TAG, "failed operation=preallocate code=%d (%s)", (int)prealloc_result,
                 esp_err_to_name(prealloc_result));
        unlink(path);
        return;
    }

    source       = heap_caps_malloc(SD_PROFILE_BUFFER_BYTES, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
    stream_cache = heap_caps_malloc(SD_PROFILE_BUFFER_BYTES, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
    if (source == NULL || stream_cache == NULL) {
        first_error = ENOMEM;
        failed_at   = "allocate";
        goto cleanup;
    }
    memset(source, 0xa5, SD_PROFILE_BUFFER_BYTES);
    ESP_LOGI(TAG, "buffers source_dma=%u cache_dma=%u bytes=%u", esp_ptr_dma_capable(source) ? 1u : 0u,
             esp_ptr_dma_capable(stream_cache) ? 1u : 0u, SD_PROFILE_BUFFER_BYTES);

    for (size_t mode_index = 0; mode_index < 2 && first_error == 0; ++mode_index) {
        const char* mode = mode_index == 0 ? "default" : "dma-cache";
        for (size_t chunk_index = 0; chunk_index < sizeof(chunk_sizes) / sizeof(chunk_sizes[0]); ++chunk_index) {
            stream = fopen(path, "r+b");
            if (stream == NULL) {
                first_error = errno != 0 ? errno : EIO;
                failed_at   = "open";
                break;
            }
            if (mode_index != 0 && setvbuf(stream, (char*)stream_cache, _IOFBF, SD_PROFILE_BUFFER_BYTES) != 0) {
                first_error = errno != 0 ? errno : EIO;
                failed_at   = "setvbuf";
            } else {
                first_error = benchmark_case(stream, source, chunk_sizes[chunk_index], mode);
                if (first_error != 0) failed_at = "write";
            }
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
    if (unlink(path) != 0 && first_error == 0) {
        first_error = errno != 0 ? errno : EIO;
        failed_at   = "remove";
    }
    heap_caps_free(stream_cache);
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
