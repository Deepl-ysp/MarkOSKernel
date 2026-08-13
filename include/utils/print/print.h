#ifndef PRINT_H
#define PRINT_H

#include <utils/print/font_data.h>

struct GOPInfo
{
    unsigned long long FrameBufferBase;
    unsigned int HorizontalResolution;
    unsigned int VerticalResolution;
    unsigned int PixelsPerScanLine;
};

static volatile struct GOPInfo *gop = (void *)0x1000;

static unsigned int cursor_x = 0;
static unsigned int cursor_y = 0;

static inline void outb(unsigned char val)
{
    __asm__ volatile(
        "mov $0x3F8, %%dx\n\t"
        "outb %0, %%dx"
        :
        : "a"(val)
        : "dx");
}

static void putchar(unsigned int *x, unsigned int *y, char c)
{
    if (gop->FrameBufferBase == 0)
        return;
    unsigned int *fb = (unsigned int *)(unsigned long)gop->FrameBufferBase;
    unsigned int pitch = gop->PixelsPerScanLine;
    unsigned int fg = 0x00FFFFFF;

    const unsigned char *glyph = font[(unsigned char)c];
    int char_w = (int)font_width[(unsigned char)c];

    for (int ty = 0; ty < CHAR_HEIGHT; ty++)
    {
        for (int tx = 0; tx < char_w; tx++)
        {
            unsigned int gray = glyph[ty * CHAR_WIDTH + tx];
            unsigned int r = ((fg >> 16) & 0xFF) * gray / 255;
            unsigned int g = ((fg >> 8) & 0xFF) * gray / 255;
            unsigned int b = (fg & 0xFF) * gray / 255;
            unsigned int color = (r << 16) | (g << 8) | b;
            fb[(*y + ty) * pitch + (*x + tx)] = color;
        }
    }

    *x += char_w;
    if (*x + CHAR_WIDTH > gop->HorizontalResolution)
    {
        *x = 0;
        *y += CHAR_HEIGHT;
    }
}

static void puts(const char *str)
{
    while (*str)
    {
        if (*str == '\n')
        {
            cursor_x = 0;
            cursor_y += CHAR_HEIGHT;
        }
        else if (*str == '\r')
        {
            cursor_x = 0;
        }
        else
        {
            putchar(&cursor_x, &cursor_y, *str);
        }
        str++;
    }
}

static void print_number(unsigned long long num, unsigned int base, int is_signed)
{
    if (is_signed && (long long)num < 0)
    {
        putchar(&cursor_x, &cursor_y, '-');
        num = -(long long)num;
    }

    if (num == 0)
    {
        putchar(&cursor_x, &cursor_y, '0');
        return;
    }

    char buf[32];
    int i = 0;
    while (num > 0)
    {
        int digit = num % base;
        buf[i++] = (digit < 10) ? ('0' + digit) : ('a' + (digit - 10));
        num /= base;
    }

    while (i-- > 0)
    {
        putchar(&cursor_x, &cursor_y, buf[i]);
    }
}

static void printf(const char *str, ...)
{
    __builtin_va_list args;

    __builtin_va_start(args, str);
    const char *p;
    for (p = str; *p != '\0'; p++)
    {
        if (*p == '%')
        {
            p++;
            if (*p == '%')
            {
                putchar(&cursor_x, &cursor_y, '%');
            }
            else if (*p == 's')
            {
                const char *s = __builtin_va_arg(args, const char *);
                while (*s)
                {
                    putchar(&cursor_x, &cursor_y, *s++);
                }
            }
            else if (*p == 'd')
            {
                int val = __builtin_va_arg(args, int);
                print_number((unsigned long long)val, 10, 1);
            }
            else if (*p == 'x')
            {
                unsigned int val = __builtin_va_arg(args, unsigned int);
                print_number((unsigned long long)val, 16, 0);
            }
            else if (*p == 'c')
            {
                char ch = (char)__builtin_va_arg(args, int);
                putchar(&cursor_x, &cursor_y, ch);
            }
            else
            {
                putchar(&cursor_x, &cursor_y, '%');
                putchar(&cursor_x, &cursor_y, *p);
            }
        }
        else
        {
            putchar(&cursor_x, &cursor_y, *p);
        }
    }

    __builtin_va_end(args);
}

static void clear_screen(void)
{
    if (gop->FrameBufferBase == 0)
        return;
    unsigned int *fb = (unsigned int *)(unsigned long)gop->FrameBufferBase;
    unsigned int pitch = gop->PixelsPerScanLine;
    unsigned int width = gop->HorizontalResolution;
    unsigned int height = gop->VerticalResolution;

    for (unsigned int y = 0; y < height; y++)
    {
        for (unsigned int x = 0; x < width; x++)
        {
            fb[y * pitch + x] = 0x00000000;
        }
    }
}

static void reset_cursor(void)
{
    cursor_x = 0;
    cursor_y = 0;
}

#endif // PRINT_H