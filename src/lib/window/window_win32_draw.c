/* window_win32_draw.c
 *
 * Drawing for the Windows window. Every mark lands on a frame held off
 * screen, and the frame reaches the window only when it is presented,
 * so nothing half finished is ever seen.
 *
 * The frame is a device independent bitmap, which is a picture Windows
 * keeps whose pixels this program can write to directly. Colours arrive
 * as 0x00RRGGBB and sit in memory that way, so a whole canvas is one
 * copy rather than a loop.
 */
#include "window.h"
#include "window_internal.h"
#include "gravestone.h"
#include "log.h"

#include <stdlib.h>
#include <string.h>

/* A fixed width face, so every glyph costs the same and placing text is
 * arithmetic rather than measurement. */
int gs_win_font_open(struct gs_window *w)
{
    HDC screen = GetDC(w->hwnd);
    TEXTMETRIC metrics;
    HFONT previous;

    if (screen == NULL)
        return GS_ERR;

    w->font = CreateFont(14, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                         ANSI_CHARSET, OUT_DEFAULT_PRECIS,
                         CLIP_DEFAULT_PRECIS, ANTIALIASED_QUALITY,
                         FIXED_PITCH | FF_MODERN, "Consolas");
    if (w->font == NULL)
        w->font = (HFONT)GetStockObject(ANSI_FIXED_FONT);
    if (w->font == NULL) {
        ReleaseDC(w->hwnd, screen);
        return GS_ERR;
    }

    previous = (HFONT)SelectObject(screen, w->font);
    GetTextMetrics(screen, &metrics);
    SelectObject(screen, previous);
    ReleaseDC(w->hwnd, screen);

    w->font_width = (int)metrics.tmAveCharWidth;
    w->font_ascent = (int)metrics.tmAscent;
    w->font_descent = (int)metrics.tmDescent;
    if (w->font_width <= 0)
        w->font_width = 8;

    gs_log_debug("window: font %dx%d, %d up, %d down", w->font_width,
                 w->font_ascent + w->font_descent, w->font_ascent,
                 w->font_descent);
    return GS_OK;
}

void gs_win_font_close(struct gs_window *w)
{
    if (w != NULL && w->font != NULL) {
        DeleteObject(w->font);
        w->font = NULL;
    }
}

void gs_win_frame_release(struct gs_window *w)
{
    if (w == NULL)
        return;
    if (w->frame_dc != NULL) {
        if (w->frame_old != NULL)
            SelectObject(w->frame_dc, w->frame_old);
        DeleteDC(w->frame_dc);
        w->frame_dc = NULL;
        w->frame_old = NULL;
    }
    if (w->frame_bitmap != NULL) {
        DeleteObject(w->frame_bitmap);
        w->frame_bitmap = NULL;
    }
    w->frame_pixels = NULL;
    w->frame_w = 0;
    w->frame_h = 0;
}

/* Makes sure a frame of this size exists, keeping the one already there
 * when it fits. */
static int ensure_frame(struct gs_window *w, int width, int height)
{
    BITMAPINFO info;
    HDC screen;

    if (w->frame_dc != NULL && w->frame_w == width && w->frame_h == height)
        return GS_OK;

    gs_win_frame_release(w);

    screen = GetDC(w->hwnd);
    if (screen == NULL)
        return GS_ERR;

    memset(&info, 0, sizeof info);
    info.bmiHeader.biSize = sizeof info.bmiHeader;
    info.bmiHeader.biWidth = width;
    /* A negative height lays the rows out top down, matching the order
     * every caller already uses. */
    info.bmiHeader.biHeight = -height;
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    info.bmiHeader.biCompression = BI_RGB;

    w->frame_dc = CreateCompatibleDC(screen);
    if (w->frame_dc == NULL) {
        ReleaseDC(w->hwnd, screen);
        return GS_ERR;
    }
    w->frame_bitmap = CreateDIBSection(screen, &info, DIB_RGB_COLORS,
                                       (void **)&w->frame_pixels, NULL, 0);
    ReleaseDC(w->hwnd, screen);

    if (w->frame_bitmap == NULL || w->frame_pixels == NULL) {
        gs_win_frame_release(w);
        return GS_ERR_MEM;
    }

    w->frame_old = (HBITMAP)SelectObject(w->frame_dc, w->frame_bitmap);
    SelectObject(w->frame_dc, w->font);
    SetBkMode(w->frame_dc, TRANSPARENT);
    w->frame_w = width;
    w->frame_h = height;
    return GS_OK;
}

/* The frame a mark should land on. Marks go on the frame that is already
 * there, whatever size it is, because throwing it away would take the
 * picture with it. Only when there is no frame at all is one made, and
 * then it is made the size of the window. */
static int frame_to_draw_on(struct gs_window *w)
{
    if (w->frame_dc != NULL && w->frame_pixels != NULL)
        return GS_OK;
    if (w->width <= 0 || w->height <= 0)
        return GS_ERR;
    return ensure_frame(w, w->width, w->height);
}

int gs_window_has_frame(const gs_window_t *window)
{
    return window != NULL && window->frame_bitmap != NULL;
}

int gs_window_present(gs_window_t *window)
{
    HDC screen;

    if (window == NULL)
        return GS_ERR_ARG;
    if (window->frame_dc == NULL)
        return GS_OK;              /* nothing buffered, nothing to show */

    screen = GetDC(window->hwnd);
    if (screen == NULL)
        return GS_ERR;
    BitBlt(screen, 0, 0, window->frame_w, window->frame_h,
           window->frame_dc, 0, 0, SRCCOPY);
    ReleaseDC(window->hwnd, screen);
    return GS_OK;
}

int gs_window_blit(gs_window_t *window, const unsigned int *pixels,
                   int w, int h)
{
    if (window == NULL || pixels == NULL || w <= 0 || h <= 0)
        return GS_ERR_ARG;
    if (ensure_frame(window, w, h) != GS_OK)
        return GS_ERR;

    /* The frame holds 0x00RRGGBB in the same order the caller uses, so
     * the whole canvas moves in one copy. */
    memcpy(window->frame_pixels, pixels,
           (size_t)w * (size_t)h * sizeof *pixels);
    return GS_OK;
}

int gs_window_blit_rect(gs_window_t *window, const unsigned int *pixels,
                        int w, int h, int y, int rows_high)
{
    int last;

    if (window == NULL || pixels == NULL || w <= 0 || h <= 0)
        return GS_ERR_ARG;

    if (window->frame_pixels == NULL || window->frame_w != w ||
        window->frame_h != h)
        return gs_window_blit(window, pixels, w, h);

    if (y < 0) {
        rows_high += y;
        y = 0;
    }
    last = y + rows_high - 1;
    if (last > h - 1)
        last = h - 1;
    if (rows_high <= 0 || y > last)
        return GS_OK;

    memcpy(window->frame_pixels + (size_t)y * (size_t)w,
           pixels + (size_t)y * (size_t)w,
           (size_t)(last - y + 1) * (size_t)w * sizeof *pixels);
    return GS_OK;
}

void gs_window_fill(gs_window_t *window, int x, int y, int w, int h,
                    unsigned int colour)
{
    RECT area;
    HBRUSH brush;

    if (window == NULL || w <= 0 || h <= 0)
        return;
    if (frame_to_draw_on(window) != GS_OK)
        return;

    area.left = x;
    area.top = y;
    area.right = x + w;
    area.bottom = y + h;

    /* Windows names a colour blue first, so the two ends swap places. */
    brush = CreateSolidBrush(RGB((colour >> 16) & 0xff,
                                 (colour >> 8) & 0xff, colour & 0xff));
    if (brush == NULL)
        return;
    FillRect(window->frame_dc, &area, brush);
    DeleteObject(brush);
}

void gs_window_text(gs_window_t *window, int x, int y, const char *text,
                    unsigned int colour)
{
    size_t len;

    if (window == NULL || text == NULL)
        return;
    len = strlen(text);
    if (len == 0 || len > 255)
        return;
    if (frame_to_draw_on(window) != GS_OK)
        return;

    SetTextColor(window->frame_dc, RGB((colour >> 16) & 0xff,
                                       (colour >> 8) & 0xff, colour & 0xff));
    /* The caller gives the baseline, and Windows takes the top, so the
     * ascent is taken off before the text goes down. */
    TextOut(window->frame_dc, x, y - window->font_ascent, text, (int)len);
}

void gs_window_flush(gs_window_t *window)
{
    if (window != NULL && window->frame_dc != NULL)
        GdiFlush();
}

int gs_window_read_pixel(gs_window_t *window, int x, int y,
                         unsigned int *out)
{
    COLORREF value;

    if (window == NULL || out == NULL)
        return GS_ERR_ARG;
    if (window->frame_pixels == NULL)
        return GS_ERR;
    if (x < 0 || y < 0 || x >= window->frame_w || y >= window->frame_h)
        return GS_ERR_ARG;

    /* Read from the frame rather than the screen, since the frame is
     * what the program drew and the screen may be covered. */
    value = window->frame_pixels[(size_t)y * (size_t)window->frame_w +
                                 (size_t)x];
    *out = (unsigned int)value & 0x00ffffffu;
    return GS_OK;
}

int gs_window_text_width(const gs_window_t *window, const char *text)
{
    if (window == NULL || text == NULL)
        return 0;
    return (int)strlen(text) * window->font_width;
}

int gs_window_font_height(const gs_window_t *window)
{
    return window != NULL ? window->font_ascent + window->font_descent : 0;
}

int gs_window_font_ascent(const gs_window_t *window)
{
    return window != NULL ? window->font_ascent : 0;
}
