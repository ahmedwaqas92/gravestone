/* picker.c
 *
 * Three ways to reach a file box, tried in the order that gives the best
 * answer on the machine actually running.
 */
#include "picker.h"
#include "picker_internal.h"
#include "gravestone.h"
#include "log.h"
#include "str.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* The extensions the chat panel accepts, in the two forms the boxes want.
 * Windows takes one string of patterns. The Linux helpers take one
 * pattern per switch. */
#define KINDS_TEXT "Spreadsheets, documents, pictures, sound and video"

static const char *const extensions[] = {
    "xlsx", "xls", "pdf", "doc", "docx", "png", "jpg", "jpeg", "webp",
    "ppt", "pptx", "mov", "mp3", "mp4"
};
#define EXTENSION_COUNT ((int)(sizeof extensions / sizeof extensions[0]))

const char *gs_picker_kinds(void)
{
    return KINDS_TEXT;
}

/* A title reaches a shell, so anything that could end a quoted string or
 * start a command of its own is refused rather than trimmed. */
static int title_is_plain(const char *title)
{
    size_t i;

    if (title == NULL)
        return 0;
    for (i = 0; title[i] != '\0'; i++) {
        char c = title[i];
        int plain = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                    (c >= '0' && c <= '9') || c == ' ' || c == '.' ||
                    c == ',' || c == '-' || c == '_';

        if (!plain)
            return 0;
    }
    return i > 0 && i < 120;
}

/* Takes the trailing newline off a line read back from a program. */
static void trim(char *text)
{
    size_t len = strlen(text);

    while (len > 0 && (text[len - 1] == '\n' || text[len - 1] == '\r'))
        text[--len] = '\0';
}

#ifdef _WIN32

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <commdlg.h>

int gs_picker_available(void)
{
    return 1;               /* the box is part of the system itself */
}

int gs_picker_open(const char *title, char *out, size_t cap)
{
    OPENFILENAMEA ask;
    char chosen[1024];
    char filter[512];
    size_t at = 0;
    int i;

    if (out == NULL || cap == 0)
        return GS_ERR_ARG;
    out[0] = '\0';
    if (!title_is_plain(title))
        return GS_ERR_ARG;

    /* Windows wants a name, then the patterns, then two zero bytes to
     * say the list has ended. */
    at += (size_t)snprintf(filter + at, sizeof filter - at, "%s", KINDS_TEXT);
    filter[at++] = '\0';
    for (i = 0; i < EXTENSION_COUNT; i++)
        at += (size_t)snprintf(filter + at, sizeof filter - at, "%s*.%s",
                               i == 0 ? "" : ";", extensions[i]);
    filter[at++] = '\0';
    filter[at++] = '\0';

    memset(chosen, 0, sizeof chosen);
    memset(&ask, 0, sizeof ask);
    ask.lStructSize = sizeof ask;
    ask.lpstrFilter = filter;
    ask.lpstrFile = chosen;
    ask.nMaxFile = sizeof chosen;
    ask.lpstrTitle = title;
    /* A box with no owner can open behind the window that asked for it.
     * The window in front is this program's own, since a person just
     * pressed a button on it, so that is the one the box belongs to. */
    ask.hwndOwner = GetForegroundWindow();
    ask.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;

    if (!GetOpenFileNameA(&ask))
        return GS_PICKER_NONE;       /* closed without choosing */
    if (strlen(chosen) + 1 > cap)
        return GS_ERR_MEM;
    memcpy(out, chosen, strlen(chosen) + 1);
    return GS_OK;
}

#else /* every other system */

#include <sys/stat.h>
#include <unistd.h>

/* Runs a program and keeps its first line. Written here rather than taken
 * from src/lib/proc/ because one wrapper may not lean on another. */
static int first_line(const char *command, char *out, size_t cap)
{
    FILE *pipe_in = popen(command, "r");
    char *got;

    if (pipe_in == NULL)
        return GS_ERR;
    out[0] = '\0';
    got = fgets(out, (int)cap, pipe_in);
    if (pclose(pipe_in) != 0 || got == NULL || out[0] == '\0')
        return GS_PICKER_NONE;
    trim(out);
    return out[0] == '\0' ? GS_PICKER_NONE : GS_OK;
}

static int have(const char *program)
{
    char command[128];
    char answer[8];

    snprintf(command, sizeof command,
             "command -v %s >/dev/null 2>&1 && echo y", program);
    return first_line(command, answer, sizeof answer) == GS_OK;
}

/* A Linux window mirrored onto a Windows desktop can borrow the box that
 * desktop already has. WSL mounts its own directory here whenever it is
 * carrying windows across. */
static int under_wsl(void)
{
    struct stat where;

    return stat("/mnt/wslg", &where) == 0 && S_ISDIR(where.st_mode);
}

int gs_picker_available(void)
{
    return under_wsl() || have("zenity") || have("kdialog");
}

/* The Windows box, reached over the bridge WSL provides, with the path it
 * gives back turned into the mounted form this program opens files by. */
int gs_picker_script(const char *title, char *out, size_t cap)
{
    char distro[80];
    char start_at[160];
    size_t at = 0;
    int i;

    if (out == NULL || cap == 0)
        return GS_ERR_ARG;
    out[0] = '\0';
    if (!title_is_plain(title))
        return GS_ERR_ARG;

    /* Windows reaches the files of a distribution over the network name
     * the system publishes for it, so the box starts at the root of this
     * one rather than in a Windows folder. An unnamed distribution leaves
     * the box to open wherever it last was. */
    if (gs_str_wsl_distro(distro, sizeof distro) == GS_OK)
        snprintf(start_at, sizeof start_at,
                 "$d.InitialDirectory = '\\\\wsl.localhost\\%s\\'\n",
                 distro);
    else
        start_at[0] = '\0';

    at += (size_t)snprintf(out + at, cap - at,
        "Add-Type -AssemblyName System.Windows.Forms\n"
        /* A box with no owner opens behind the window that asked for it,
         * because that window belongs to another program as far as
         * Windows is concerned. An owner that is always on top, and that
         * takes the foreground first, puts the box in front and gives it
         * the keyboard. */
        "$owner = New-Object System.Windows.Forms.Form\n"
        "$owner.TopMost = $true\n"
        "$owner.ShowInTaskbar = $false\n"
        "$owner.FormBorderStyle = 'None'\n"
        "$owner.Size = New-Object System.Drawing.Size(1,1)\n"
        "$owner.StartPosition = 'Manual'\n"
        "$owner.Location = New-Object System.Drawing.Point(-4000,-4000)\n"
        "$owner.Show()\n"
        "$owner.Activate()\n"
        "$d = New-Object System.Windows.Forms.OpenFileDialog\n"
        "$d.Title = '%s'\n"
        "%s"
        "$d.Filter = '%s|", title, start_at, KINDS_TEXT);
    for (i = 0; i < EXTENSION_COUNT; i++)
        at += (size_t)snprintf(out + at, cap - at, "%s*.%s",
                               i == 0 ? "" : ";", extensions[i]);
    at += (size_t)snprintf(out + at, cap - at,
                           "|Every file|*.*'\n");
    if (at >= cap)
        return GS_ERR;

    /* A window filling the screen sits above ordinary always on top
     * windows, so the box has to be lifted above that as well. The
     * lifting runs on a timer, because the box does not exist until
     * ShowDialog is already running and ShowDialog does not come back
     * until the person has answered. */
    at += (size_t)snprintf(out + at, cap - at,
        "Add-Type @\"\n"
        "using System;\n"
        "using System.Runtime.InteropServices;\n"
        "public class L {\n"
        "  [DllImport(\"\"user32.dll\"\", CharSet=CharSet.Unicode)]\n"
        "  public static extern IntPtr FindWindow(string c, string n);\n"
        "  [DllImport(\"\"user32.dll\"\")]\n"
        "  public static extern bool SetWindowPos(IntPtr h, IntPtr a,\n"
        "      int x, int y, int w, int t, uint f);\n"
        "  [DllImport(\"\"user32.dll\"\")]\n"
        "  public static extern bool SetForegroundWindow(IntPtr h);\n"
        "}\n"
        "\"@\n"
        "$tick = New-Object System.Windows.Forms.Timer\n"
        "$tick.Interval = 200\n"
        "$tick.Add_Tick({\n"
        "  $h = [L]::FindWindow($null, '%s')\n"
        "  if ($h -ne [IntPtr]::Zero) {\n"
        /* Minus one asks for the place above every other always on top
         * window, and three leaves the size and the position alone. */
        "    [void][L]::SetWindowPos($h, [IntPtr](-1), 0, 0, 0, 0, 3)\n"
        "    [void][L]::SetForegroundWindow($h)\n"
        "    $tick.Stop()\n"
        "  }\n"
        "})\n"
        "$tick.Start()\n"
        "$answer = $d.ShowDialog($owner)\n"
        "$tick.Stop()\n"
        "$owner.Close()\n"
        "if ($answer -eq 'OK') { Write-Output $d.FileName }\n", title);
    if (at >= cap)
        return GS_ERR;
    return GS_OK;
}

static int ask_windows(const char *title, char *out, size_t cap)
{
    char script[4096];
    char encoded[12288];
    char command[12800];
    char windows_path[1024];
    char translate[2048];
    int rc;

    if (gs_picker_script(title, script, sizeof script) != GS_OK)
        return GS_ERR;
    if (gs_str_windows_command(script, encoded, sizeof encoded) < 0)
        return GS_ERR;

    /* STA is the mode Windows insists on before it will put a file box on
     * the screen. */
    if ((size_t)snprintf(command, sizeof command,
                         "powershell.exe -NoProfile -STA -EncodedCommand %s "
                         "2>/dev/null", encoded) >= sizeof command)
        return GS_ERR;

    rc = first_line(command, windows_path, sizeof windows_path);
    if (rc != GS_OK)
        return rc;

    /* The path goes into a quoted argument, so a quote inside it would
     * end that argument and the rest would run as a command of its own.
     * Checked before the command is built rather than after. */
    if (strchr(windows_path, '\'') != NULL)
        return GS_ERR;

    /* wslpath turns a Windows path into the mounted one. */
    snprintf(translate, sizeof translate,
             "wslpath -u -- '%s' 2>/dev/null", windows_path);
    rc = first_line(translate, out, cap);
    if (rc != GS_OK) {
        /* A file was chosen, so this is a drive that is not mounted
         * rather than a person closing the box. Saying so is better than
         * reporting no choice at all. */
        gs_log_warn("picker: %s is on a drive this system cannot reach",
                    windows_path);
        out[0] = '\0';
        return GS_ERR;
    }
    return GS_OK;
}

static int ask_helper(const char *program, const char *title,
                      char *out, size_t cap)
{
    char command[2048];
    size_t at = 0;
    int i;

    if (strcmp(program, "kdialog") == 0) {
        at += (size_t)snprintf(command + at, sizeof command - at,
                               "kdialog --title '%s' --getopenfilename . '",
                               title);
        for (i = 0; i < EXTENSION_COUNT; i++)
            at += (size_t)snprintf(command + at, sizeof command - at,
                                   "%s*.%s", i == 0 ? "" : " ",
                                   extensions[i]);
        at += (size_t)snprintf(command + at, sizeof command - at,
                               "' 2>/dev/null");
    } else {
        at += (size_t)snprintf(command + at, sizeof command - at,
                               "zenity --file-selection --title='%s'", title);
        for (i = 0; i < EXTENSION_COUNT; i++)
            at += (size_t)snprintf(command + at, sizeof command - at,
                                   " --file-filter='*.%s'", extensions[i]);
        at += (size_t)snprintf(command + at, sizeof command - at,
                               " 2>/dev/null");
    }
    if (at >= sizeof command)
        return GS_ERR;
    return first_line(command, out, cap);
}

int gs_picker_open(const char *title, char *out, size_t cap)
{
    if (out == NULL || cap < 2)
        return GS_ERR_ARG;
    out[0] = '\0';
    if (!title_is_plain(title))
        return GS_ERR_ARG;

    if (under_wsl())
        return ask_windows(title, out, cap);
    if (have("zenity"))
        return ask_helper("zenity", title, out, cap);
    if (have("kdialog"))
        return ask_helper("kdialog", title, out, cap);

    gs_log_warn("picker: no file box on this machine. Install zenity.");
    return GS_ERR;
}

#endif
