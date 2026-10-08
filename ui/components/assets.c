#include "assets.h"

#include <libgen.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#define ASSET_PATH_MAX   512
#define ASSET_POOL_SLOTS 4

/* Rotating result buffers - see the note in assets.h. */
static char s_pool[ASSET_POOL_SLOTS][ASSET_PATH_MAX];
static unsigned s_pool_next = 0;

static char *pool_take(void)
{
    char *slot = s_pool[s_pool_next];
    s_pool_next = (s_pool_next + 1u) % ASSET_POOL_SLOTS;
    return slot;
}

static bool file_readable(const char *path)
{
    FILE *f = fopen(path, "rb");
    if (!f) return false;
    fclose(f);
    return true;
}

/* Write the first existing candidate for @p rel into @p out.
 * Order: next to the executable, then the current directory, then one level
 * up (the usual spot when running straight out of a build tree). */
static bool asset_locate(const char *rel, char *out, size_t out_len)
{
    if (!rel || !out || out_len == 0) return false;

    char exe[ASSET_PATH_MAX];
    ssize_t len = readlink("/proc/self/exe", exe, sizeof(exe) - 1);
    if (len > 0) {
        exe[len] = '\0';
        /* dirname() may modify its argument, so it gets the throwaway copy. */
        const char *dir = dirname(exe);
        if (snprintf(out, out_len, "%s/%s", dir, rel) < (int)out_len
                && file_readable(out)) {
            return true;
        }
    }

    static const char *const prefixes[] = { "", "../" };
    for (unsigned i = 0; i < sizeof(prefixes) / sizeof(prefixes[0]); i++) {
        if (snprintf(out, out_len, "%s%s", prefixes[i], rel) < (int)out_len
                && file_readable(out)) {
            return true;
        }
    }

    out[0] = '\0';
    return false;
}

const char *asset_path(const char *rel)
{
    char *slot = pool_take();
    return asset_locate(rel, slot, ASSET_PATH_MAX) ? slot : NULL;
}

const char *asset_lv_path(const char *rel)
{
    char found[ASSET_PATH_MAX];
    if (!asset_locate(rel, found, sizeof(found))) return NULL;

    char *slot = pool_take();
    if (snprintf(slot, ASSET_PATH_MAX, "A:%s", found) >= ASSET_PATH_MAX)
        return NULL;
    return slot;
}
