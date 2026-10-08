#ifndef ASSETS_H
#define ASSETS_H

#ifdef __cplusplus
extern "C" {
#endif

/* Locate files shipped next to the executable.
 *
 * The app runs from a systemd unit with WorkingDirectory=/home/ricardo/ihm,
 * from a build tree during development, and from the PC simulator, so an
 * asset is looked up next to the binary first and then relative to the
 * current directory.
 *
 * Both functions return a pointer into a small rotating pool of static
 * buffers, so a handful of results can be held at once (two GIFs in one
 * screen, say).  Do not keep a returned pointer across many calls - copy it
 * if it has to outlive the next few lookups.  NULL means the file was not
 * found anywhere. */

/** Plain filesystem path, for fopen() and lv_lottie_set_src_file(). */
const char *asset_path(const char *rel);

/** Same lookup, prefixed with LVGL's "A:" drive letter, for image sources. */
const char *asset_lv_path(const char *rel);

#ifdef __cplusplus
}
#endif

#endif /* ASSETS_H */
