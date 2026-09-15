#!/bin/sh
# display.sh
#
# Makes sure a screen exists before the window opens, and repairs the one
# case where the system says a screen exists while nothing can be seen.
#
# WSLg carries window pixels to Windows through a shared memory file both
# sides map. When that file fails to open, frames and taskbar buttons
# still cross while every pixel is dropped, so the X server reports a
# healthy window and the desktop shows nothing. The failure is written
# into the compositor's own log at boot, which is what this reads.
#
# The repair is a plain X server running on Windows, which draws windows
# itself and needs no shared memory. Nothing in gravestone changes.

set -e

say() { printf 'display: %s\n' "$1" >&2; }

# A Windows binary of gravestone draws through Windows itself, so this
# whole file concerns the Linux and WSL development loop alone.
case "$(uname -s)" in
    Linux) ;;
    *) exit 0 ;;
esac

WESTON_LOG=/mnt/wslg/weston.log
WSLG_BROKEN=0
if [ -r "$WESTON_LOG" ] &&
   grep -q "rdp_allocate_shared_memory: Failed" "$WESTON_LOG"; then
    WSLG_BROKEN=1
fi

# The address Windows answers on, which is the default route under WSL2.
host_address() {
    ip route show default 2>/dev/null | awk '{print $3; exit}'
}

x_server_reachable() {
    addr="$1"
    [ -n "$addr" ] || return 1
    timeout 2 sh -c "true > /dev/tcp/$addr/6000" 2>/dev/null
}

# A screen that works is left alone.
if [ "$WSLG_BROKEN" -eq 0 ] && [ -n "$DISPLAY" ]; then
    say "using $DISPLAY"
    exit 0
fi

if [ "$WSLG_BROKEN" -eq 1 ]; then
    say "WSLg cannot carry pixels on this boot, from $WESTON_LOG"
fi

ADDR=$(host_address)
if x_server_reachable "$ADDR"; then
    say "an X server is already answering at $ADDR"
    printf 'DISPLAY=%s:0\n' "$ADDR"
    exit 0
fi

if ! command -v powershell.exe >/dev/null 2>&1; then
    say "no Windows side reachable, leaving $DISPLAY alone"
    exit 0
fi

# VcXsrv draws X11 windows as ordinary Windows windows. Installing it
# writes to the Windows machine, so it happens only when asked for.
VCXSRV='C:\Program Files\VcXsrv\vcxsrv.exe'
HAVE=$(powershell.exe -NoProfile -Command \
    "if (Test-Path '$VCXSRV') { 'yes' } else { 'no' }" 2>/dev/null |
    tr -d '\r')

if [ "$HAVE" != "yes" ]; then
    if [ "$GS_DISPLAY_INSTALL" != "1" ]; then
        say "no X server on Windows. Run 'make display-install' once, or"
        say "set GS_DISPLAY_INSTALL=1 to let this install VcXsrv."
        exit 1
    fi
    say "installing VcXsrv on the Windows side"
    powershell.exe -NoProfile -Command \
        "winget install --id marha.VcXsrv -e --silent \
         --accept-package-agreements --accept-source-agreements" \
        >/dev/null 2>&1 || true
fi

# Started with one window per application, no client of its own, and no
# access control, since the connection comes from another machine as far
# as Windows is concerned.
say "starting the X server on Windows"
powershell.exe -NoProfile -Command \
    "if (-not (Get-Process vcxsrv -EA SilentlyContinue)) {
         Start-Process '$VCXSRV' -ArgumentList '-multiwindow','-clipboard','-wgl','-ac'
     }" >/dev/null 2>&1 || true

i=0
while [ "$i" -lt 20 ]; do
    if x_server_reachable "$ADDR"; then
        say "X server answering at $ADDR"
        printf 'DISPLAY=%s:0\n' "$ADDR"
        exit 0
    fi
    sleep 1
    i=$((i + 1))
done

say "the X server did not answer at $ADDR within twenty seconds"
exit 1
