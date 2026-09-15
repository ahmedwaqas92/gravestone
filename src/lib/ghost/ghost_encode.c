/* ghost_encode.c
 *
 * Turning the script into one base64 word keeps every quote, bracket and
 * dollar sign it contains away from the shell that starts the Windows
 * program. The alternative is writing the script to a file and handing
 * over its path, which needs the path translated into Windows form and
 * quoted through two shells.
 */
#include "ghost.h"
#include "ghost_internal.h"
#include "gravestone.h"
#include "str.h"

#include <stdio.h>
#include <string.h>

/* The walk collects its matches into a list held outside the callback,
 * because a value written from inside one is thrown away rather than
 * printed. The two counts are printed once the walk has finished. */
int gs_ghost_script(const char *pattern, char *out, size_t cap)
{
    static const char body[] =
        "Add-Type @\"\n"
        "using System;\n"
        "using System.Text;\n"
        "using System.Runtime.InteropServices;\n"
        "public class G {\n"
        "  public delegate bool Cb(IntPtr h, IntPtr l);\n"
        "  [DllImport(\"user32.dll\")] public static extern bool "
        "EnumWindows(Cb cb, IntPtr l);\n"
        "  [DllImport(\"user32.dll\")] public static extern int "
        "GetWindowText(IntPtr h, StringBuilder s, int n);\n"
        "  [DllImport(\"user32.dll\")] public static extern bool "
        "IsWindowVisible(IntPtr h);\n"
        "  [DllImport(\"user32.dll\")] public static extern bool "
        "ShowWindowAsync(IntPtr h, int c);\n"
        "}\n"
        "\"@\n"
        "$seen = New-Object System.Collections.ArrayList\n"
        "$cb = [G+Cb]{ param($h, $l)\n"
        "  $sb = New-Object System.Text.StringBuilder 256\n"
        "  [void][G]::GetWindowText($h, $sb, 256)\n"
        "  $t = $sb.ToString()\n"
        "  if ($t -like '%s') {\n"
        "    $vis = [G]::IsWindowVisible($h)\n"
        "    if ($vis) { [void][G]::ShowWindowAsync($h, 0) }\n"
        "    [void]$seen.Add($vis)\n"
        "  }\n"
        "  return $true\n"
        "}\n"
        "[void][G]::EnumWindows($cb, [IntPtr]::Zero)\n"
        "$hid = @($seen | Where-Object { $_ }).Count\n"
        "Write-Output \"hidden $hid already $($seen.Count - $hid)\"\n";

    int written;

    if (pattern == NULL || out == NULL || cap == 0)
        return GS_ERR_ARG;
    if (strchr(pattern, '\'') != NULL)
        return GS_ERR_ARG;          /* a quote would end the string early */

    written = snprintf(out, cap, body, pattern);
    if (written < 0 || (size_t)written >= cap)
        return GS_ERR_ARG;
    return GS_OK;
}
