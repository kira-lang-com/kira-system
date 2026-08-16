#include "kira_system_dialogs.h"

#include <string.h>

/* One answer buffer per thread. The dialog is modal, so a call cannot be in
 * flight twice on the same thread, and the caller copies the path out before it
 * ever asks again. A returned pointer therefore never dangles and never needs a
 * free function on the Kira side. */
#if defined(_MSC_VER)
#define KIRA_SYSTEM_THREAD_LOCAL __declspec(thread)
#else
#define KIRA_SYSTEM_THREAD_LOCAL __thread
#endif

#define KIRA_SYSTEM_PATH_MAX 4096
static KIRA_SYSTEM_THREAD_LOCAL char g_answer[KIRA_SYSTEM_PATH_MAX];

static const char* kira_system_answer_empty(void) {
    g_answer[0] = '\0';
    return g_answer;
}

static int kira_system_has_text(const char* text) {
    return text != NULL && text[0] != '\0';
}

/* ---------------------------------------------------------------- Windows -- */
#if defined(_WIN32)

#define COBJMACROS
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shobjidl.h>

/* The dialog's class and interface ids, spelled out rather than taken from
 * `uuid.lib`: the toolchain compiles this shim with clang against the Windows
 * SDK headers, and an import library full of GUID constants is not something a
 * cross build can be assumed to have. */
static const CLSID kira_system_clsid_file_open =
    {0xDC1C5A9C, 0xE88A, 0x4DDE, {0xA5, 0xA1, 0x60, 0xF8, 0x2A, 0x20, 0xAE, 0xF7}};
static const CLSID kira_system_clsid_file_save =
    {0xC0B4E2F3, 0xBA21, 0x4773, {0x8D, 0xBA, 0x33, 0x5E, 0xC9, 0x46, 0xEB, 0x8B}};
static const IID kira_system_iid_file_open =
    {0xD57C7288, 0xD4AD, 0x4768, {0xBE, 0x02, 0x9D, 0x96, 0x93, 0x32, 0xA1, 0x06}};
static const IID kira_system_iid_file_save =
    {0x84BCCD23, 0x5FDE, 0x4CDB, {0xAE, 0xA4, 0xAF, 0x64, 0xB8, 0x3D, 0x78, 0xAB}};
static const IID kira_system_iid_shell_item =
    {0x43826D1E, 0xE718, 0x42EE, {0xBC, 0x55, 0xA1, 0xE2, 0x61, 0xC3, 0x7B, 0xFE}};

/* UTF-8 -> UTF-16 into a caller buffer. An empty or oversized input produces an
 * empty string, which every caller below reads as "no preference". */
static void kira_system_widen(const char* utf8, wchar_t* out, int outCount) {
    out[0] = L'\0';
    if (!kira_system_has_text(utf8)) {
        return;
    }
    if (MultiByteToWideChar(CP_UTF8, 0, utf8, -1, out, outCount) == 0) {
        out[0] = L'\0';
    }
}

/* The chosen item's filesystem path, as UTF-8, into the answer buffer. */
static void kira_system_take_result(IShellItem* item) {
    PWSTR wide = NULL;
    if (SUCCEEDED(IShellItem_GetDisplayName(item, SIGDN_FILESYSPATH, &wide)) && wide != NULL) {
        if (WideCharToMultiByte(CP_UTF8, 0, wide, -1, g_answer, KIRA_SYSTEM_PATH_MAX, NULL, NULL) == 0) {
            g_answer[0] = '\0';
        }
        CoTaskMemFree(wide);
    }
}

/* Point the dialog at `start`. Advisory: a path that no longer exists leaves the
 * dialog wherever it would have opened, which is better than refusing to open. */
static void kira_system_set_start(IFileDialog* dialog, const char* start) {
    wchar_t wideStart[KIRA_SYSTEM_PATH_MAX];
    kira_system_widen(start, wideStart, KIRA_SYSTEM_PATH_MAX);
    if (wideStart[0] == L'\0') {
        return;
    }
    IShellItem* folder = NULL;
    if (SUCCEEDED(SHCreateItemFromParsingName(wideStart, NULL, &kira_system_iid_shell_item, (void**)&folder))) {
        IFileDialog_SetFolder(dialog, folder);
        IShellItem_Release(folder);
    }
}

static void kira_system_set_title(IFileDialog* dialog, const char* title) {
    wchar_t wideTitle[512];
    kira_system_widen(title, wideTitle, 512);
    if (wideTitle[0] != L'\0') {
        IFileDialog_SetTitle(dialog, wideTitle);
    }
}

/* One run of the common item dialog. `save` picks the save panel over the open
 * panel; `folders` turns the open panel into a folder chooser.
 *
 * COM is initialized per call and torn down after: the dialog is the only thing
 * in this library that needs an apartment, and a program that opens one twice a
 * session should not be made to carry one for its whole run. Initializing an
 * apartment that already exists (the host may have made one) answers
 * RPC_E_CHANGED_MODE, which is not a failure — it just means this call does not
 * own it and must not uninitialize it. */
static const char* kira_system_run_dialog(int save, int folders, const char* title,
                                          const char* start, const char* suggested) {
    kira_system_answer_empty();

    HRESULT initialized = CoInitializeEx(NULL, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
    int ownsApartment = SUCCEEDED(initialized);

    IFileDialog* dialog = NULL;
    const CLSID* clsid = save ? &kira_system_clsid_file_save : &kira_system_clsid_file_open;
    const IID* iid = save ? &kira_system_iid_file_save : &kira_system_iid_file_open;
    if (SUCCEEDED(CoCreateInstance(clsid, NULL, CLSCTX_INPROC_SERVER, iid, (void**)&dialog))) {
        DWORD options = 0;
        IFileDialog_GetOptions(dialog, &options);
        options |= FOS_FORCEFILESYSTEM | FOS_NOCHANGEDIR;
        if (folders) {
            options |= FOS_PICKFOLDERS;
        }
        IFileDialog_SetOptions(dialog, options);

        kira_system_set_title(dialog, title);
        kira_system_set_start(dialog, start);
        if (kira_system_has_text(suggested)) {
            wchar_t wideName[512];
            kira_system_widen(suggested, wideName, 512);
            if (wideName[0] != L'\0') {
                IFileDialog_SetFileName(dialog, wideName);
            }
        }

        /* A cancel is an ordinary answer, and arrives as a failing HRESULT
         * (ERROR_CANCELLED); the empty result already says it. */
        if (SUCCEEDED(IFileDialog_Show(dialog, NULL))) {
            IShellItem* item = NULL;
            if (SUCCEEDED(IFileDialog_GetResult(dialog, &item)) && item != NULL) {
                kira_system_take_result(item);
                IShellItem_Release(item);
            }
        }
        IFileDialog_Release(dialog);
    }

    if (ownsApartment) {
        CoUninitialize();
    }
    return g_answer;
}

const char* kira_system_pick_folder(const char* title, const char* start) {
    return kira_system_run_dialog(0, 1, title, start, NULL);
}

const char* kira_system_pick_file(const char* title, const char* start) {
    return kira_system_run_dialog(0, 0, title, start, NULL);
}

const char* kira_system_pick_save_path(const char* title, const char* start, const char* suggested) {
    return kira_system_run_dialog(1, 0, title, start, suggested);
}

const char* kira_system_dialog_backend(void) {
    return "windows";
}

/* ------------------------------------------------------------------ macOS -- */
#elif defined(__APPLE__)

#include <TargetConditionals.h>

#if TARGET_OS_OSX

#include <objc/message.h>
#include <objc/runtime.h>

/* NSOpenPanel / NSSavePanel through the Objective-C runtime, so this file stays
 * C and the package needs no Objective-C compile step. `objc_msgSend` is cast
 * to the signature of the message being sent, which is how the runtime is
 * called from C — the unprototyped form passes floating-point and struct
 * arguments wrongly on arm64. */
typedef id (*kira_system_msg_id)(id, SEL);
typedef id (*kira_system_msg_id_id)(id, SEL, id);
typedef void (*kira_system_msg_void_bool)(id, SEL, BOOL);
typedef long (*kira_system_msg_long)(id, SEL);
typedef const char* (*kira_system_msg_cstr)(id, SEL);

static id kira_system_class(const char* name) {
    return (id)objc_getClass(name);
}

static id kira_system_send(id target, const char* selector) {
    return ((kira_system_msg_id)objc_msgSend)(target, sel_registerName(selector));
}

static void kira_system_send_bool(id target, const char* selector, BOOL value) {
    ((kira_system_msg_void_bool)objc_msgSend)(target, sel_registerName(selector), value);
}

static id kira_system_nsstring(const char* utf8) {
    if (!kira_system_has_text(utf8)) {
        return NULL;
    }
    return ((kira_system_msg_id_id)objc_msgSend)(kira_system_class("NSString"),
                                                 sel_registerName("stringWithUTF8String:"),
                                                 (id)(void*)utf8);
}

/* The panel's chosen path into the answer buffer. NSURL -> NSString -> UTF-8,
 * copied out before the autoreleased objects go anywhere. */
static void kira_system_take_url(id url) {
    if (url == NULL) {
        return;
    }
    id path = kira_system_send(url, "path");
    if (path == NULL) {
        return;
    }
    const char* utf8 = ((kira_system_msg_cstr)objc_msgSend)(path, sel_registerName("UTF8String"));
    if (utf8 != NULL) {
        strncpy(g_answer, utf8, KIRA_SYSTEM_PATH_MAX - 1);
        g_answer[KIRA_SYSTEM_PATH_MAX - 1] = '\0';
    }
}

static void kira_system_configure(id panel, const char* title, const char* start, const char* suggested) {
    id titleString = kira_system_nsstring(title);
    if (titleString != NULL) {
        ((kira_system_msg_id_id)objc_msgSend)(panel, sel_registerName("setMessage:"), titleString);
    }
    id startString = kira_system_nsstring(start);
    if (startString != NULL) {
        id url = ((kira_system_msg_id_id)objc_msgSend)(kira_system_class("NSURL"),
                                                       sel_registerName("fileURLWithPath:"),
                                                       startString);
        if (url != NULL) {
            ((kira_system_msg_id_id)objc_msgSend)(panel, sel_registerName("setDirectoryURL:"), url);
        }
    }
    id suggestedString = kira_system_nsstring(suggested);
    if (suggestedString != NULL) {
        ((kira_system_msg_id_id)objc_msgSend)(panel, sel_registerName("setNameFieldStringValue:"), suggestedString);
    }
}

/* NSModalResponseOK. */
#define KIRA_SYSTEM_MODAL_OK 1

static const char* kira_system_run_open(int folders, const char* title, const char* start) {
    kira_system_answer_empty();
    id panel = kira_system_send(kira_system_class("NSOpenPanel"), "openPanel");
    if (panel == NULL) {
        return g_answer;
    }
    kira_system_send_bool(panel, "setCanChooseDirectories:", folders ? YES : NO);
    kira_system_send_bool(panel, "setCanChooseFiles:", folders ? NO : YES);
    kira_system_send_bool(panel, "setAllowsMultipleSelection:", NO);
    kira_system_send_bool(panel, "setCanCreateDirectories:", YES);
    kira_system_configure(panel, title, start, NULL);
    if (((kira_system_msg_long)objc_msgSend)(panel, sel_registerName("runModal")) == KIRA_SYSTEM_MODAL_OK) {
        kira_system_take_url(kira_system_send(panel, "URL"));
    }
    return g_answer;
}

const char* kira_system_pick_folder(const char* title, const char* start) {
    return kira_system_run_open(1, title, start);
}

const char* kira_system_pick_file(const char* title, const char* start) {
    return kira_system_run_open(0, title, start);
}

const char* kira_system_pick_save_path(const char* title, const char* start, const char* suggested) {
    kira_system_answer_empty();
    id panel = kira_system_send(kira_system_class("NSSavePanel"), "savePanel");
    if (panel == NULL) {
        return g_answer;
    }
    kira_system_send_bool(panel, "setCanCreateDirectories:", YES);
    kira_system_configure(panel, title, start, suggested);
    if (((kira_system_msg_long)objc_msgSend)(panel, sel_registerName("runModal")) == KIRA_SYSTEM_MODAL_OK) {
        kira_system_take_url(kira_system_send(panel, "URL"));
    }
    return g_answer;
}

const char* kira_system_dialog_backend(void) {
    return "macos";
}

#else /* an Apple platform with no file panels (iOS, tvOS, watchOS) */

const char* kira_system_pick_folder(const char* title, const char* start) {
    (void)title; (void)start;
    return kira_system_answer_empty();
}

const char* kira_system_pick_file(const char* title, const char* start) {
    (void)title; (void)start;
    return kira_system_answer_empty();
}

const char* kira_system_pick_save_path(const char* title, const char* start, const char* suggested) {
    (void)title; (void)start; (void)suggested;
    return kira_system_answer_empty();
}

const char* kira_system_dialog_backend(void) {
    return "none";
}

#endif /* TARGET_OS_OSX */

/* ------------------------------------------------------------------ Linux -- */
#elif defined(__linux__)

#include <stdio.h>
#include <stdlib.h>

/* Linux has no system dialog library — the chooser belongs to the desktop, and
 * the portable way to reach it is to run the desktop's own helper and read the
 * path it prints. `zenity` (GNOME/portal) is tried first and `kdialog` (KDE)
 * second; a machine with neither answers an empty path, which callers already
 * read as a cancel. */
static void kira_system_run_helper(const char* command) {
    FILE* pipe = popen(command, "r");
    if (pipe == NULL) {
        return;
    }
    if (fgets(g_answer, KIRA_SYSTEM_PATH_MAX, pipe) == NULL) {
        g_answer[0] = '\0';
    }
    pclose(pipe);
    /* The helpers print one path and a newline. */
    size_t length = strlen(g_answer);
    while (length > 0 && (g_answer[length - 1] == '\n' || g_answer[length - 1] == '\r')) {
        g_answer[length - 1] = '\0';
        length = length - 1;
    }
}

static int kira_system_helper_exists(const char* name) {
    char probe[256];
    snprintf(probe, sizeof(probe), "command -v %s > /dev/null 2>&1", name);
    return system(probe) == 0;
}

/* Text that cannot end a shell single-quoted string: the arguments below are
 * pasted into one, and a title or path holding a quote would otherwise become
 * command text. Anything unquotable is dropped rather than escaped, because a
 * dialog title is decoration and a start directory is advisory. */
static int kira_system_shell_safe(const char* text) {
    if (!kira_system_has_text(text)) {
        return 0;
    }
    for (const char* c = text; *c != '\0'; c = c + 1) {
        if (*c == '\'' || *c == '\n') {
            return 0;
        }
    }
    return 1;
}

static const char* kira_system_run_linux(int folders, int save, const char* title,
                                         const char* start, const char* suggested) {
    kira_system_answer_empty();
    char command[KIRA_SYSTEM_PATH_MAX + 512];
    if (kira_system_helper_exists("zenity")) {
        snprintf(command, sizeof(command), "zenity --file-selection%s%s",
                 folders ? " --directory" : "", save ? " --save" : "");
        if (kira_system_shell_safe(title)) {
            strncat(command, " --title='", sizeof(command) - strlen(command) - 1);
            strncat(command, title, sizeof(command) - strlen(command) - 1);
            strncat(command, "'", sizeof(command) - strlen(command) - 1);
        }
        if (kira_system_shell_safe(start)) {
            strncat(command, " --filename='", sizeof(command) - strlen(command) - 1);
            strncat(command, start, sizeof(command) - strlen(command) - 1);
            strncat(command, "/", sizeof(command) - strlen(command) - 1);
            if (kira_system_shell_safe(suggested)) {
                strncat(command, suggested, sizeof(command) - strlen(command) - 1);
            }
            strncat(command, "'", sizeof(command) - strlen(command) - 1);
        }
        strncat(command, " 2>/dev/null", sizeof(command) - strlen(command) - 1);
        kira_system_run_helper(command);
        return g_answer;
    }
    if (kira_system_helper_exists("kdialog")) {
        const char* mode = folders ? "--getexistingdirectory" : (save ? "--getsavefilename" : "--getopenfilename");
        const char* where = kira_system_shell_safe(start) ? start : ".";
        snprintf(command, sizeof(command), "kdialog %s '%s' 2>/dev/null", mode, where);
        kira_system_run_helper(command);
        return g_answer;
    }
    return g_answer;
}

const char* kira_system_pick_folder(const char* title, const char* start) {
    return kira_system_run_linux(1, 0, title, start, NULL);
}

const char* kira_system_pick_file(const char* title, const char* start) {
    return kira_system_run_linux(0, 0, title, start, NULL);
}

const char* kira_system_pick_save_path(const char* title, const char* start, const char* suggested) {
    return kira_system_run_linux(0, 1, title, start, suggested);
}

const char* kira_system_dialog_backend(void) {
    if (kira_system_helper_exists("zenity") || kira_system_helper_exists("kdialog")) {
        return "portal";
    }
    return "none";
}

/* ------------------------------------------------------------ everywhere -- */
#else

const char* kira_system_pick_folder(const char* title, const char* start) {
    (void)title; (void)start;
    return kira_system_answer_empty();
}

const char* kira_system_pick_file(const char* title, const char* start) {
    (void)title; (void)start;
    return kira_system_answer_empty();
}

const char* kira_system_pick_save_path(const char* title, const char* start, const char* suggested) {
    (void)title; (void)start; (void)suggested;
    return kira_system_answer_empty();
}

const char* kira_system_dialog_backend(void) {
    return "none";
}

#endif
