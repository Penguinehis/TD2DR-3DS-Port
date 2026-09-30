#include "loader.h"

#include <stdlib.h>
#include <string.h>

#define JOBS 24

// Job states. A texture job goes read -> NEED_BUF (the game thread allocates the texture
// memory) -> UNPACK (the worker decompresses into it) -> DONE.
enum { J_FREE, J_READ, J_BUSY, J_DONE, J_NEED_BUF, J_UNPACK };

typedef struct {
    char path[80];
    int state;
    int result;
    bool tex;
    void *data;      // file contents (heap)
    size_t size;
    LoaderTex t;     // texture jobs: header fields, destination buffer
    size_t head;     // texture jobs: offset of the compressed stream
    bool orphan;     // dropped while the worker had it: freed when it comes back
} Job;

static Job jobs[JOBS];
static LightLock lock;
static LightEvent wake;
static Thread thread;
static volatile bool quit;

static int read_file_now(const char *path, void **data, size_t *size)
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

// .t3x: u16 subtextures; u8 width_log2:3, height_log2:3, type:1; u8 format; u8 mipmaps;
// 12 bytes per subtexture; then a libctru compression stream of the texture data.
static bool t3x_header(const u8 *p, size_t size, LoaderTex *t, size_t *head)
{
    if (size < 9) return false;
    *head = 5 + 12 * (size_t)(p[0] | p[1] << 8);
    if (*head + 4 > size) return false;
    t->width = 1 << ((p[2] & 7) + 3);
    t->height = 1 << (((p[2] >> 3) & 7) + 3);
    t->format = p[3];
    decompressType type;
    size_t raw = 0;
    if (decompressHeader(&type, &raw, NULL, (void *)(p + *head), size - *head) < 0 || raw == 0) return false;
    t->size = raw;
    return true;
}

static bool t3x_unpack(const u8 *p, size_t size, size_t head, LoaderTex *t)
{
    if (!decompress(t->mem, t->size, NULL, (void *)(p + head), size - head)) return false;
    GSPGPU_FlushDataCache(t->mem, t->size);
    return true;
}

static void worker(void *arg)
{
    (void)arg;
    while (!quit) {
        LightEvent_Wait(&wake);
        for (;;) {
            Job *j = NULL;
            int was = 0;
            LightLock_Lock(&lock);
            for (int i = 0; i < JOBS && !j; i++)
                if (jobs[i].state == J_READ || jobs[i].state == J_UNPACK) j = &jobs[i];
            if (j) {
                was = j->state;
                j->state = J_BUSY;
            }
            LightLock_Unlock(&lock);
            if (!j || quit) break;

            int next = J_DONE, result = LOADER_FAILED;
            if (was == J_UNPACK) {
                result = t3x_unpack(j->data, j->size, j->head, &j->t) ? LOADER_READY : LOADER_FAILED;
                free(j->data);
                j->data = NULL;
            } else {
                void *data = NULL;
                size_t size = 0;
                result = read_file_now(j->path, &data, &size);
                j->data = data;
                j->size = size;
                if (result == LOADER_READY && j->tex) {
                    if (t3x_header(data, size, &j->t, &j->head)) {
                        next = J_NEED_BUF;
                    } else {
                        result = LOADER_FAILED;
                        free(j->data);
                        j->data = NULL;
                    }
                }
            }
            LightLock_Lock(&lock);
            j->result = result;
            j->state = next;
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

// Finds the job for path, or queues one. Lock held; NULL: pending (no free slot).
static Job *find_job(const char *path, bool tex, bool *queued)
{
    Job *free_slot = NULL;
    *queued = false;
    for (int i = 0; i < JOBS; i++) {
        Job *j = &jobs[i];
        if (j->state == J_FREE) {
            if (!free_slot) free_slot = j;
            continue;
        }
        if (!strcmp(j->path, path)) return j;
    }
    if (!free_slot) return NULL;
    memset(free_slot, 0, sizeof *free_slot);
    snprintf(free_slot->path, sizeof free_slot->path, "%s", path);
    free_slot->tex = tex;
    free_slot->state = J_READ;
    *queued = true;
    return free_slot;
}

int loader_fetch(const char *path, void **data, size_t *size)
{
    if (!thread) return read_file_now(path, data, size);
    bool queued;
    LightLock_Lock(&lock);
    Job *j = find_job(path, false, &queued);
    int r = LOADER_PENDING;
    if (j && j->state == J_DONE) {
        r = j->result;
        *data = j->data;
        *size = j->size;
        j->data = NULL;
        j->state = J_FREE;
    }
    LightLock_Unlock(&lock);
    if (queued) LightEvent_Signal(&wake);
    return r;
}

// Lock held: free the texture jobs nobody wants any more (game thread: linearFree)
static void sweep(bool all)
{
    for (int i = 0; i < JOBS; i++) {
        Job *j = &jobs[i];
        if (!j->tex || j->state == J_FREE) continue;
        bool worker_has = j->state == J_BUSY || j->state == J_UNPACK;
        if (worker_has) {
            if (all) j->orphan = true;
            continue;
        }
        if (!all && !j->orphan) continue;
        if (j->t.mem) linearFree(j->t.mem);
        free(j->data);
        memset(j, 0, sizeof *j);
    }
}

void loader_drop_textures(void)
{
    if (!thread) return;
    LightLock_Lock(&lock);
    sweep(true);
    LightLock_Unlock(&lock);
}

int loader_fetch_tex(const char *path, LoaderTex *out, LoaderAlloc alloc)
{
    if (!thread) return loader_load_tex_now(path, out, alloc);
    bool queued, wake_worker = false;
    LightLock_Lock(&lock);
    sweep(false);
    Job *j = find_job(path, true, &queued);
    int r = LOADER_PENDING;
    if (j && j->state == J_NEED_BUF) {
        j->t.mem = alloc(j->t.size);  // the only work on the game thread
        if (j->t.mem) {
            j->state = J_UNPACK;
            wake_worker = true;
        }
    } else if (j && j->state == J_DONE) {
        r = j->result;
        if (r == LOADER_READY) *out = j->t;
        else if (j->t.mem) linearFree(j->t.mem);
        free(j->data);
        j->data = NULL;
        j->state = J_FREE;
    }
    LightLock_Unlock(&lock);
    if (queued || wake_worker) LightEvent_Signal(&wake);
    return r;
}

int loader_load_tex_now(const char *path, LoaderTex *out, LoaderAlloc alloc)
{
    void *data;
    size_t size, head;
    int r = read_file_now(path, &data, &size);
    if (r != LOADER_READY) return r;
    LoaderTex t = { 0 };
    r = LOADER_FAILED;
    if (t3x_header(data, size, &t, &head) && (t.mem = alloc(t.size))) {
        if (t3x_unpack(data, size, head, &t)) {
            *out = t;
            r = LOADER_READY;
        } else {
            linearFree(t.mem);
        }
    }
    free(data);
    return r;
}
