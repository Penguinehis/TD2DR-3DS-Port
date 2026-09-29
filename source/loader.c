#include "loader.h"

#include <stdlib.h>
#include <string.h>

#define JOBS 24

typedef struct {
    char path[80];
    void *data;
    size_t size;
    int state;  // 0 free, 1 queued, 2 reading, 3 done (result in `result`)
    int result;
} Job;

static Job jobs[JOBS];
static LightLock lock;
static LightEvent wake;
static Thread thread;
static volatile bool quit;

// A .t3x is a 5-byte header, 12 bytes per subtexture, then a libctru compression stream.
// Decompress that here and hand over the same file with an uncompressed ("dummy", type 0)
// stream: citro3d then only copies it on the game thread.
static void t3x_unpack(void **data, size_t *size)
{
    const u8 *p = *data;
    if (*size < 10) return;
    size_t head = 5 + 12 * (size_t)(p[0] | p[1] << 8);
    if (head + 4 > *size || p[head] == 0x00) return;  // already uncompressed
    decompressType type;
    size_t raw = 0;
    if (decompressHeader(&type, &raw, NULL, (void *)(p + head), *size - head) < 0 || raw == 0 || raw >= (1u << 24))
        return;
    u8 *out = malloc(head + 4 + raw);
    if (!out) return;
    memcpy(out, p, head);
    u32 dummy = (u32)raw << 8;  // type 0x00, then the size
    memcpy(out + head, &dummy, 4);
    if (!decompress(out + head + 4, raw, NULL, (void *)(p + head), *size - head)) {
        free(out);
        return;
    }
    free(*data);
    *data = out;
    *size = head + 4 + raw;
}

static void worker(void *arg)
{
    (void)arg;
    while (!quit) {
        LightEvent_Wait(&wake);
        for (;;) {
            Job *j = NULL;
            LightLock_Lock(&lock);
            for (int i = 0; i < JOBS && !j; i++)
                if (jobs[i].state == 1) j = &jobs[i];
            if (j) j->state = 2;
            LightLock_Unlock(&lock);
            if (!j || quit) break;

            void *data = NULL;
            size_t size = 0;
            int result = LOADER_FAILED;
            FILE *f = fopen(j->path, "rb");
            if (!f) {
                result = LOADER_MISSING;
            } else {
                fseek(f, 0, SEEK_END);
                long n = ftell(f);
                fseek(f, 0, SEEK_SET);
                data = n > 0 ? malloc(n) : NULL;
                if (data && fread(data, 1, n, f) == (size_t)n) {
                    size = n;
                    result = LOADER_READY;
                    size_t len = strlen(j->path);
                    if (len > 4 && !strcmp(j->path + len - 4, ".t3x")) t3x_unpack(&data, &size);
                } else {
                    free(data);
                    data = NULL;
                }
                fclose(f);
            }
            LightLock_Lock(&lock);
            j->data = data;
            j->size = size;
            j->result = result;
            j->state = 3;
            LightLock_Unlock(&lock);
        }
    }
}

void loader_init(void)
{
    LightLock_Init(&lock);
    LightEvent_Init(&wake, RESET_ONESHOT);
    s32 prio = 0x30;
    svcGetThreadPriority(&prio, CUR_THREAD_HANDLE);
    // Mostly waiting on the card: the system core is fine; else the app core below the game
    // (it then runs while the game waits for the screen refresh).
    thread = threadCreate(worker, NULL, 16 * 1024, prio - 1, 1, false);
    if (!thread) thread = threadCreate(worker, NULL, 16 * 1024, prio + 1, -2, false);
}

void loader_exit(void)
{
    if (!thread) return;
    quit = true;
    LightEvent_Signal(&wake);
    threadJoin(thread, U64_MAX);
    threadFree(thread);
    thread = NULL;
    for (int i = 0; i < JOBS; i++) free(jobs[i].data);
}

// Synchronous read (no worker thread)
static int read_now(const char *path, void **data, size_t *size)
{
    FILE *f = fopen(path, "rb");
    if (!f) return LOADER_MISSING;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    void *buf = n > 0 ? malloc(n) : NULL;
    int r = LOADER_FAILED;
    if (buf && fread(buf, 1, n, f) == (size_t)n) {
        *data = buf;
        *size = n;
        r = LOADER_READY;
    } else {
        free(buf);
    }
    fclose(f);
    return r;
}

int loader_fetch(const char *path, void **data, size_t *size)
{
    if (!thread) return read_now(path, data, size);
    LightLock_Lock(&lock);
    Job *free_slot = NULL;
    for (int i = 0; i < JOBS; i++) {
        Job *j = &jobs[i];
        if (j->state == 0) {
            if (!free_slot) free_slot = j;
            continue;
        }
        if (strcmp(j->path, path)) continue;
        if (j->state != 3) {
            LightLock_Unlock(&lock);
            return LOADER_PENDING;
        }
        int r = j->result;
        *data = j->data;
        *size = j->size;
        j->data = NULL;
        j->state = 0;
        LightLock_Unlock(&lock);
        return r;
    }
    if (free_slot) {
        snprintf(free_slot->path, sizeof free_slot->path, "%s", path);
        free_slot->state = 1;
    }
    LightLock_Unlock(&lock);
    if (free_slot) LightEvent_Signal(&wake);
    return LOADER_PENDING;
}
