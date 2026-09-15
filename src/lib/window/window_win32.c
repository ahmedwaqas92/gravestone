/* window_win32.c
 *
 * The same window as window_x11.c, drawn by Windows itself. Opening it,
 * closing it, sizing it, and turning what Windows reports into the
 * events the rest of the program already understands.
 *
 * Windows delivers input by calling a function of ours whenever it
 * pleases, so events land in a queue here and the waiting call reads
 * from that queue rather than from the system directly.
 *
 * Drawing lives in window_win32_draw.c.
 */
#include "window.h"
#include "window_internal.h"
#include "gravestone.h"
#include "log.h"
#include "str.h"

#include <stdlib.h>
#include <string.h>

#define CLASS_NAME "GravestoneWindow"

static ATOM registered;

static void queue_push(struct gs_window *w, const gs_window_event_t *e)
{
    if (w->queue_len >= GS_WIN_QUEUE_LEN)
        return;
    w->queue[(w->queue_head + w->queue_len) % GS_WIN_QUEUE_LEN] = *e;
    w->queue_len++;
}

int gs_win_queue_take(struct gs_window *w, gs_window_event_t *out)
{
    if (w == NULL || w->queue_len == 0)
        return 0;
    *out = w->queue[w->queue_head];
    w->queue_head = (w->queue_head + 1) % GS_WIN_QUEUE_LEN;
    w->queue_len--;
    return 1;
}

/* Windows knows a window by its handle, and the rest of this file knows
 * it by the struct, so the struct is stored against the handle when the
 * window is made. */
static struct gs_window *window_of(HWND hwnd)
{
    return (struct gs_window *)(uintptr_t)GetWindowLongPtr(hwnd,
                                                           GWLP_USERDATA);
}

/* The plain character a key carries, with backspace as 8 and escape as
 * 27, matching what the X11 side reports. */
/* Which holding keys are down right now. The high bit of the answer says
 * held, and Windows is asked at the moment the key press is read. */
static int mods_now(void)
{
    int mods = 0;

    if (GetKeyState(VK_SHIFT) < 0)
        mods |= GS_WINDOW_MOD_SHIFT;
    if (GetKeyState(VK_CONTROL) < 0)
        mods |= GS_WINDOW_MOD_CONTROL;
    if (GetKeyState(VK_MENU) < 0)
        mods |= GS_WINDOW_MOD_ALT;
    return mods;
}

static int character_of(WPARAM key)
{
    BYTE keyboard[256];
    WCHAR out[4];
    UINT scan;
    int made;

    if (key == VK_ESCAPE)
        return 27;
    if (key == VK_BACK)
        return 8;
    if (key == VK_RETURN)
        return 10;
    if (key == VK_TAB)
        return 9;

    if (!GetKeyboardState(keyboard))
        return 0;
    scan = MapVirtualKey((UINT)key, MAPVK_VK_TO_VSC);
    made = ToUnicode((UINT)key, scan, keyboard, out,
                     (int)(sizeof out / sizeof out[0]), 0);
    if (made == 1 && out[0] >= 0x20 && out[0] <= 0x7e)
        return (int)out[0];
    return 0;
}

static LRESULT CALLBACK window_proc(HWND hwnd, UINT message, WPARAM w_param,
                                    LPARAM l_param)
{
    struct gs_window *w = window_of(hwnd);
    gs_window_event_t e;

    if (w == NULL)
        return DefWindowProc(hwnd, message, w_param, l_param);

    memset(&e, 0, sizeof e);

    switch (message) {
    case WM_PAINT: {
        PAINTSTRUCT paint;

        BeginPaint(hwnd, &paint);
        EndPaint(hwnd, &paint);
        w->exposed = 1;
        e.kind = GS_WINDOW_EVENT_EXPOSE;
        queue_push(w, &e);
        return 0;
    }

    case WM_SIZE: {
        int nw = LOWORD(l_param);
        int nh = HIWORD(l_param);

        if (nw > 0 && nh > 0 && (nw != w->width || nh != w->height)) {
            w->width = nw;
            w->height = nh;
            e.kind = GS_WINDOW_EVENT_RESIZE;
            e.width = nw;
            e.height = nh;
            queue_push(w, &e);
        }
        return 0;
    }

    case WM_KEYDOWN:
    case WM_SYSKEYDOWN:
        e.kind = GS_WINDOW_EVENT_KEY;
        e.key = (int)w_param;
        e.ch = character_of(w_param);
        e.mods = mods_now();
        queue_push(w, &e);
        return 0;

    case WM_LBUTTONDOWN:
    case WM_RBUTTONDOWN:
    case WM_MBUTTONDOWN:
        e.kind = GS_WINDOW_EVENT_CLICK;
        e.x = GET_X_LPARAM(l_param);
        e.y = GET_Y_LPARAM(l_param);
        e.button = message == WM_LBUTTONDOWN ? 1
                 : (message == WM_MBUTTONDOWN ? 2 : 3);
        queue_push(w, &e);
        return 0;

    case WM_MOUSEWHEEL:
        /* The wheel arrives as buttons four and five everywhere else, so
         * it arrives as those here too. */
        e.kind = GS_WINDOW_EVENT_CLICK;
        e.button = GET_WHEEL_DELTA_WPARAM(w_param) > 0 ? 4 : 5;
        {
            POINT at;

            at.x = GET_X_LPARAM(l_param);
            at.y = GET_Y_LPARAM(l_param);
            ScreenToClient(hwnd, &at);
            e.x = at.x;
            e.y = at.y;
        }
        queue_push(w, &e);
        return 0;

    case WM_MOUSEMOVE:
        e.kind = GS_WINDOW_EVENT_MOVE;
        e.x = GET_X_LPARAM(l_param);
        e.y = GET_Y_LPARAM(l_param);
        queue_push(w, &e);
        return 0;

    case WM_SETCURSOR:
        /* Windows asks every time the pointer moves over the window, and
         * only the part inside the frame is ours to answer for. */
        if (LOWORD(l_param) == HTCLIENT && w->cursor != NULL) {
            SetCursor(w->cursor);
            return TRUE;
        }
        break;

    case WM_GETMINMAXINFO: {
        MINMAXINFO *limits = (MINMAXINFO *)l_param;

        if (w->min_w > 0)
            limits->ptMinTrackSize.x = w->min_w;
        if (w->min_h > 0)
            limits->ptMinTrackSize.y = w->min_h;
        return 0;
    }

    case WM_CLOSE:
        e.kind = GS_WINDOW_EVENT_CLOSE;
        queue_push(w, &e);
        return 0;                  /* the caller decides when to go */

    default:
        break;
    }
    return DefWindowProc(hwnd, message, w_param, l_param);
}

int gs_window_display_available(void)
{
    if (getenv("GS_TEST_NO_WINDOW") != NULL)
        return 0;
    /* Windows always has a desktop for a program started from one. */
    return GetSystemMetrics(SM_CXSCREEN) > 0;
}

/* The size asked for is the drawing area, so the frame is added on top
 * before the window is made. */
static void outer_size(int want_w, int want_h, int *out_w, int *out_h)
{
    RECT r;

    r.left = 0;
    r.top = 0;
    r.right = want_w;
    r.bottom = want_h;
    AdjustWindowRect(&r, WS_OVERLAPPEDWINDOW, FALSE);
    *out_w = r.right - r.left;
    *out_h = r.bottom - r.top;
}

gs_window_t *gs_window_open(const char *title, int width, int height)
{
    /* Nothing opens while windows are turned off, which is how a whole
     * test run goes past without taking the screen from anybody. */
    if (getenv("GS_TEST_NO_WINDOW") != NULL)
        return NULL;

    struct gs_window *w;
    HINSTANCE instance = GetModuleHandle(NULL);
    int outer_w, outer_h;

    if (title == NULL || width <= 0 || height <= 0) {
        gs_log_error("window: bad arguments to gs_window_open");
        return NULL;
    }

    if (registered == 0) {
        WNDCLASSEX cls;

        memset(&cls, 0, sizeof cls);
        cls.cbSize = sizeof cls;
        cls.style = CS_HREDRAW | CS_VREDRAW | CS_OWNDC;
        cls.lpfnWndProc = window_proc;
        cls.hInstance = instance;
        cls.hCursor = LoadCursor(NULL, IDC_ARROW);
        /* No background brush, since every pixel comes from the frame
         * and letting Windows paint one first shows as a flash. */
        cls.hbrBackground = NULL;
        cls.lpszClassName = CLASS_NAME;
        registered = RegisterClassEx(&cls);
        if (registered == 0) {
            gs_log_error("window: the window class would not register");
            return NULL;
        }
    }

    w = calloc(1, sizeof *w);
    if (w == NULL)
        return NULL;

    w->width = width;
    w->height = height;
    outer_size(width, height, &outer_w, &outer_h);

    w->hwnd = CreateWindowEx(0, CLASS_NAME, title, WS_OVERLAPPEDWINDOW,
                             CW_USEDEFAULT, CW_USEDEFAULT, outer_w, outer_h,
                             NULL, NULL, instance, NULL);
    if (w->hwnd == NULL) {
        gs_log_error("window: the window would not open");
        free(w);
        return NULL;
    }
    SetWindowLongPtr(w->hwnd, GWLP_USERDATA, (LONG_PTR)(uintptr_t)w);

    if (gs_win_font_open(w) != GS_OK) {
        DestroyWindow(w->hwnd);
        free(w);
        return NULL;
    }

    ShowWindow(w->hwnd, SW_SHOWNORMAL);
    SetForegroundWindow(w->hwnd);
    UpdateWindow(w->hwnd);

    gs_log_info("window: opened %dx%d as \"%s\"", width, height, title);
    return w;
}

void gs_window_close(gs_window_t *window)
{
    if (window == NULL)
        return;

    gs_win_frame_release(window);
    gs_win_font_close(window);
    if (window->hwnd != NULL) {
        SetWindowLongPtr(window->hwnd, GWLP_USERDATA, 0);
        DestroyWindow(window->hwnd);
        window->hwnd = NULL;
    }
    free(window);
}

/* Reads whatever Windows has waiting and turns it into queued events. */
static void pump(void)
{
    MSG message;

    while (PeekMessage(&message, NULL, 0, 0, PM_REMOVE)) {
        TranslateMessage(&message);
        DispatchMessage(&message);
    }
}

int gs_window_wait_any(gs_window_t **windows, int count, int *which,
                       gs_window_event_t *out, int timeout_ms)
{
    DWORD deadline;
    int i;

    if (windows == NULL || out == NULL || which == NULL || count <= 0)
        return GS_ERR_ARG;

    memset(out, 0, sizeof *out);
    deadline = GetTickCount() + (timeout_ms < 0 ? 0 : (DWORD)timeout_ms);

    for (;;) {
        int alive = 0;

        pump();
        for (i = 0; i < count; i++) {
            if (windows[i] == NULL || windows[i]->hwnd == NULL)
                continue;
            alive++;
            if (gs_win_queue_take(windows[i], out)) {
                *which = i;
                return 1;
            }
        }
        if (alive == 0)
            return GS_ERR_IO;

        if (timeout_ms == 0)
            return 0;
        if (timeout_ms > 0 && GetTickCount() >= deadline)
            return 0;

        /* Sleeping until a message arrives keeps the processor idle
         * while the window sits still. */
        MsgWaitForMultipleObjects(0, NULL, FALSE,
                                  timeout_ms < 0 ? INFINITE : 10,
                                  QS_ALLINPUT);
    }
}

/* Windows delivers everything through its own message queue, so there is
 * no second descriptor to watch and nothing for this to do. */
void gs_window_set_wake_fd(int fd)
{
    (void)fd;
}

int gs_window_wait_event(gs_window_t *window, gs_window_event_t *out,
                         int timeout_ms)
{
    int which = 0;

    if (window == NULL)
        return GS_ERR_ARG;
    return gs_window_wait_any(&window, 1, &which, out, timeout_ms);
}

int gs_window_set_hints(gs_window_t *window, int x, int y,
                        int want_w, int want_h, int min_w, int min_h)
{
    int outer_w, outer_h;

    if (window == NULL || want_w <= 0 || want_h <= 0)
        return GS_ERR_ARG;

    window->min_w = min_w;
    window->min_h = min_h;
    outer_size(want_w, want_h, &outer_w, &outer_h);

    if (x >= 0 && y >= 0)
        MoveWindow(window->hwnd, x, y, outer_w, outer_h, TRUE);
    else
        SetWindowPos(window->hwnd, NULL, 0, 0, outer_w, outer_h,
                     SWP_NOMOVE | SWP_NOZORDER);
    return GS_OK;
}

int gs_window_position(gs_window_t *window, int *x, int *y)
{
    POINT origin;

    if (window == NULL || x == NULL || y == NULL)
        return GS_ERR_ARG;
    origin.x = 0;
    origin.y = 0;
    if (!ClientToScreen(window->hwnd, &origin))
        return GS_ERR;
    *x = origin.x;
    *y = origin.y;
    return GS_OK;
}

int gs_window_place(gs_window_t *window, int x, int y, int w, int h)
{
    int outer_w, outer_h;

    if (window == NULL || w <= 0 || h <= 0)
        return GS_ERR_ARG;
    if (w > 32767 || h > 32767 || x < -32768 || x > 32767 ||
        y < -32768 || y > 32767)
        return GS_ERR_ARG;

    outer_size(w, h, &outer_w, &outer_h);
    return MoveWindow(window->hwnd, x, y, outer_w, outer_h, TRUE)
        ? GS_OK : GS_ERR;
}

int gs_window_resize(gs_window_t *window, int w, int h)
{
    int outer_w, outer_h;

    if (window == NULL || w <= 0 || h <= 0)
        return GS_ERR_ARG;
    outer_size(w, h, &outer_w, &outer_h);
    return SetWindowPos(window->hwnd, NULL, 0, 0, outer_w, outer_h,
                        SWP_NOMOVE | SWP_NOZORDER) ? GS_OK : GS_ERR;
}

int gs_window_width(const gs_window_t *window)
{
    return window != NULL ? window->width : 0;
}

int gs_window_height(const gs_window_t *window)
{
    return window != NULL ? window->height : 0;
}

/* A window drawn by the system itself has no connection to lose, so it is
 * usable for as long as it exists. */
/* The shapes Windows carries built in, in the same order as the list the
 * interface asks by. */
static LPCSTR cursor_name(gs_window_cursor_t shape)
{
    switch (shape) {
    case GS_WINDOW_CURSOR_TEXT: return IDC_IBEAM;
    case GS_WINDOW_CURSOR_HAND: return IDC_HAND;
    default:                    return IDC_ARROW;
    }
}

int gs_window_set_cursor(gs_window_t *window, gs_window_cursor_t shape)
{
    HCURSOR picked;

    if (window == NULL || shape < 0 || shape >= GS_WINDOW_CURSOR_COUNT)
        return GS_ERR_ARG;
    if (window->cursor_now == (int)shape)
        return GS_OK;

    picked = LoadCursor(NULL, cursor_name(shape));
    if (picked == NULL)
        return GS_ERR;
    window->cursor = picked;
    window->cursor_now = (int)shape;
    /* Windows asks again the next time the pointer moves, and the answer
     * below hands back whatever was chosen here. Setting it now as well
     * changes the shape without waiting for that movement. */
    SetCursor(picked);
    return GS_OK;
}

int gs_window_maximise(gs_window_t *window, int on)
{
    if (window == NULL || window->hwnd == NULL)
        return GS_ERR_ARG;

    /* Windows grows a window to the working area by itself, and the
     * frame with its three buttons stays where it is. */
    ShowWindow(window->hwnd, on ? SW_MAXIMIZE : SW_RESTORE);
    return GS_OK;
}

int gs_window_connected(const gs_window_t *window)
{
    return window != NULL && window->hwnd != NULL;
}

int gs_window_error_count(const gs_window_t *window)
{
    /* Windows reports a failure through the call that made it rather
     * than on a stream, so nothing accumulates here. */
    (void)window;
    return 0;
}
