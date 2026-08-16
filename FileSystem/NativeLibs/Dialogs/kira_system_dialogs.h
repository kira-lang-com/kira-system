#ifndef KIRA_SYSTEM_DIALOGS_H
#define KIRA_SYSTEM_DIALOGS_H

/* The platform's own file chooser.
 *
 * Each call runs the system dialog MODALLY and returns when the user has
 * answered it: a UTF-8 filesystem path, or an empty string when they cancelled.
 * The returned pointer is owned by this library and stays valid until the next
 * call on the same thread — copy it if you need to keep it.
 *
 * `title` labels the dialog and `start` is the directory it opens in; either
 * may be empty (or NULL) for the platform's default. Both are UTF-8.
 *
 * These must be called from the thread that owns the application's windows,
 * which on every platform here is the main thread. */

/* Choose one existing directory. */
const char* kira_system_pick_folder(const char* title, const char* start);

/* Choose one existing file. */
const char* kira_system_pick_file(const char* title, const char* start);

/* Name a file that does not have to exist yet (a save panel). `suggested` is
 * the file name the dialog opens with; it may be empty. */
const char* kira_system_pick_save_path(const char* title, const char* start, const char* suggested);

/* Which chooser this build talks to: "windows", "macos", "portal" (a Linux
 * desktop helper), or "none" where the platform has no dialog at all. A caller
 * that must not block on a chooser it cannot get can ask first; everything else
 * can read an empty result as a cancel and never call this. */
const char* kira_system_dialog_backend(void);

#endif
