#include "libc.h"
#include <stdint.h>

#define FILE_BUF_SIZE 4096
#define O_RDONLY 0x000
#define O_WRONLY 0x001
#define O_RDWR   0x002
#define O_CREAT  0x040
#define O_TRUNC  0x200
#define O_APPEND 0x400

static unsigned char stdin_buf[FILE_BUF_SIZE];
static unsigned char stdout_buf[FILE_BUF_SIZE];

static FILE _stdin = { 0, O_RDONLY, 0, 0, 2, FILE_BUF_SIZE, 0, 0, 0, stdin_buf, -1 };
static FILE _stdout = { 1, O_WRONLY, 0, 0, 1, FILE_BUF_SIZE, 0, 0, 0, stdout_buf, -1 };
static FILE _stderr = { 2, O_WRONLY, 0, 0, 0, 0, 0, 0, 0, NULL, -1 };

FILE *stdin = &_stdin;
FILE *stdout = &_stdout;
FILE *stderr = &_stderr;

FILE *fopen(const char *path, const char *mode) {
    int flags = 0;
    if (strcmp(mode, "r") == 0) flags = O_RDONLY;
    else if (strcmp(mode, "w") == 0) flags = O_WRONLY | O_CREAT | O_TRUNC;
    else if (strcmp(mode, "a") == 0) flags = O_WRONLY | O_CREAT | O_APPEND;
    else if (strcmp(mode, "r+") == 0) flags = O_RDWR;
    else if (strcmp(mode, "w+") == 0) flags = O_RDWR | O_CREAT | O_TRUNC;
    else if (strcmp(mode, "a+") == 0) flags = O_RDWR | O_CREAT | O_APPEND;
    else { errno = 22; return NULL; }

    int fd = open(path, flags, 0644);
    if (fd < 0) return NULL;

    FILE *f = (FILE *)malloc(sizeof(FILE));
    if (!f) { close(fd); return NULL; }

    f->fd = fd;
    f->flags = flags;
    f->eof = 0;
    f->error = 0;
    f->buf_mode = 2;
    f->buf_size = FILE_BUF_SIZE;
    f->buf_pos = 0;
    f->buf_len = 0;
    f->buf_end = 0;
    f->buffer = (unsigned char *)malloc(FILE_BUF_SIZE);
    if (!f->buffer) { free(f); close(fd); return NULL; }
    f->unget_char = -1;
    return f;
}

int fclose(FILE *stream) {
    if (!stream) return EOF;
    fflush(stream);
    int res = close(stream->fd);
    if (stream->buffer) free(stream->buffer);
    free(stream);
    return res;
}

int fflush(FILE *stream) {
    if (!stream) return 0;
    if (stream->flags & O_WRONLY || stream->flags & O_RDWR) {
        if (stream->buf_end > 0) {
            ssize_t w = write(stream->fd, stream->buffer, stream->buf_end);
            if (w < 0) { stream->error = 1; return EOF; }
            stream->buf_end = 0;
        }
    } else {
        stream->buf_pos = 0;
        stream->buf_len = 0;
    }
    return 0;
}

size_t fread(void *ptr, size_t size, size_t nmemb, FILE *stream) {
    if (!stream || size == 0 || nmemb == 0) return 0;
    size_t total = size * nmemb;
    unsigned char *dest = (unsigned char *)ptr;
    size_t read_count = 0;

    if (stream->unget_char != -1) {
        *dest++ = (unsigned char)stream->unget_char;
        stream->unget_char = -1;
        read_count++;
        total--;
    }

    while (total > 0) {
        if (stream->buf_pos >= stream->buf_len) {
            ssize_t r = read(stream->fd, stream->buffer, stream->buf_size);
            if (r <= 0) { stream->eof = 1; break; }
            stream->buf_len = r;
            stream->buf_pos = 0;
        }
        size_t avail = stream->buf_len - stream->buf_pos;
        size_t to_copy = (avail < total) ? avail : total;
        memcpy(dest, stream->buffer + stream->buf_pos, to_copy);
        stream->buf_pos += to_copy;
        dest += to_copy;
        read_count += to_copy;
        total -= to_copy;
    }
    return read_count / size;
}

size_t fwrite(const void *ptr, size_t size, size_t nmemb, FILE *stream) {
    if (!stream || size == 0 || nmemb == 0) return 0;
    size_t total = size * nmemb;
    const unsigned char *src = (const unsigned char *)ptr;
    size_t written = 0;

    if (stream->buf_mode == 0) {
        ssize_t w = write(stream->fd, src, total);
        if (w < 0) { stream->error = 1; return 0; }
        return w / size;
    }

    while (total > 0) {
        size_t space = stream->buf_size - stream->buf_end;
        size_t to_copy = (space < total) ? space : total;
        memcpy(stream->buffer + stream->buf_end, src, to_copy);
        stream->buf_end += to_copy;
        src += to_copy;
        written += to_copy;
        total -= to_copy;

        if (stream->buf_end == stream->buf_size || (stream->buf_mode == 1 && memchr(src - to_copy, '\n', to_copy))) {
            if (fflush(stream) == EOF) break;
        }
    }
    return written / size;
}

int fgetc(FILE *stream) {
    unsigned char c;
    if (fread(&c, 1, 1, stream) == 1) return c;
    return EOF;
}

int fputc(int c, FILE *stream) {
    unsigned char ch = (unsigned char)c;
    if (fwrite(&ch, 1, 1, stream) == 1) return c;
    return EOF;
}

char *fgets(char *s, int size, FILE *stream) {
    if (size <= 0) return NULL;
    int i = 0;
    while (i < size - 1) {
        int c = fgetc(stream);
        if (c == EOF) break;
        s[i++] = (char)c;
        if (c == '\n') break;
    }
    if (i == 0 && feof(stream)) return NULL;
    s[i] = '\0';
    return s;
}

int fputs(const char *s, FILE *stream) {
    size_t len = strlen(s);
    if (fwrite(s, 1, len, stream) == len) return 0;
    return EOF;
}

int getchar(void) { return fgetc(stdin); }
int putchar(int c) { return fputc(c, stdout); }

int puts(const char *s) {
    if (fputs(s, stdout) == EOF) return EOF;
    return fputc('\n', stdout);
}

int fseek(FILE *stream, long offset, int whence) {
    fflush(stream);
    off_t res = lseek(stream->fd, offset, whence);
    if (res == -1) { stream->error = 1; return -1; }
    stream->eof = 0;
    return 0;
}

long ftell(FILE *stream) {
    fflush(stream);
    return (long)lseek(stream->fd, 0, SEEK_CUR);
}

void rewind(FILE *stream) { fseek(stream, 0, SEEK_SET); }
int feof(FILE *stream) { return stream ? stream->eof : 0; }
int ferror(FILE *stream) { return stream ? stream->error : 0; }
void clearerr(FILE *stream) { if (stream) { stream->eof = 0; stream->error = 0; } }

int ungetc(int c, FILE *stream) {
    if (!stream || c == EOF) return EOF;
    stream->unget_char = c;
    stream->eof = 0;
    return c;
}

typedef struct {
    FILE *f;
    char *str;
    size_t pos;
    size_t max;
    int count;
} printf_ctx_t;

static void printf_out_char(printf_ctx_t *ctx, char c) {
    if (ctx->f) { fputc(c, ctx->f); }
    else if (ctx->str && ctx->pos < ctx->max - 1) { ctx->str[ctx->pos++] = c; }
    ctx->count++;
}

static void printf_out_str(printf_ctx_t *ctx, const char *s, size_t len) {
    for (size_t i = 0; i < len; i++) printf_out_char(ctx, s[i]);
}

static void print_int(printf_ctx_t *ctx, unsigned long long val, int base, int is_neg, int width, int precision, int flags) {
    char buf[64];
    int i = 0;
    if (val == 0) buf[i++] = '0';
    while (val > 0) {
        int d = val % base;
        buf[i++] = (d < 10) ? ('0' + d) : ((flags & 0x10) ? ('A' + d - 10) : ('a' + d - 10));
        val /= base;
    }
    int len = i;
    int pad_len = (precision > len) ? precision : len;
    if (is_neg) pad_len++;

    char pad_char = (flags & 0x08) ? '0' : ' ';
    if (!(flags & 0x01) && pad_char == ' ') {
        for (int p = pad_len; p < width; p++) printf_out_char(ctx, ' ');
    }
    if (is_neg) printf_out_char(ctx, '-');
    for (int p = len; p < precision; p++) printf_out_char(ctx, '0');
    while (i > 0) printf_out_char(ctx, buf[--i]);
    if (flags & 0x01) {
        for (int p = pad_len; p < width; p++) printf_out_char(ctx, ' ');
    }
}

static void print_float(printf_ctx_t *ctx, double val, int precision, int width, int flags) {
    if (val < 0) { printf_out_char(ctx, '-'); val = -val; }
    unsigned long long int_part = (unsigned long long)val;
    double frac_part = val - (double)int_part;

    int round_digit = 0;
    double round_val = frac_part;
    for (int i = 0; i <= precision; i++) round_val *= 10.0;
    round_digit = (int)round_val % 10;
    int carry = (round_digit >= 5) ? 1 : 0;

    char frac_buf[64];
    for (int i = precision - 1; i >= 0; i--) {
        frac_part *= 10.0;
        int d = (int)frac_part;
        if (i == precision - 1) d += carry;
        if (d >= 10) { d -= 10; carry = 1; } else { carry = 0; }
        frac_buf[i] = '0' + d;
        frac_part -= (int)frac_part;
    }
    if (carry) int_part++;

    char int_buf[32];
    int i_len = 0;
    if (int_part == 0) int_buf[i_len++] = '0';
    while (int_part > 0) { int_buf[i_len++] = '0' + (int_part % 10); int_part /= 10; }

    int total_len = i_len + 1 + precision;
    if (!(flags & 0x01)) {
        for (int p = total_len; p < width; p++) printf_out_char(ctx, ' ');
    }
    while (i_len > 0) printf_out_char(ctx, int_buf[--i_len]);
    printf_out_char(ctx, '.');
    for (int i = 0; i < precision; i++) printf_out_char(ctx, frac_buf[i]);
    if (flags & 0x01) {
        for (int p = total_len; p < width; p++) printf_out_char(ctx, ' ');
    }
}

static int format_parser(printf_ctx_t *ctx, const char *fmt, va_list args) {
    while (*fmt) {
        if (*fmt != '%') { printf_out_char(ctx, *fmt++); continue; }
        fmt++;
        int flags = 0;
        while (*fmt == '-' || *fmt == '+' || *fmt == ' ' || *fmt == '#' || *fmt == '0') {
            if (*fmt == '-') flags |= 0x01;
            else if (*fmt == '0') flags |= 0x08;
            else if (*fmt == '#') flags |= 0x20;
            fmt++;
        }
        int width = 0;
        if (*fmt == '*') { width = va_arg(args, int); fmt++; }
        else { while (*fmt >= '0' && *fmt <= '9') { width = width * 10 + (*fmt - '0'); fmt++; } }
        int precision = -1;
        if (*fmt == '.') {
            fmt++;
            precision = 0;
            if (*fmt == '*') { precision = va_arg(args, int); fmt++; }
            else { while (*fmt >= '0' && *fmt <= '9') { precision = precision * 10 + (*fmt - '0'); fmt++; } }
        }
        int length = 0;
        if (*fmt == 'h') { length = 1; fmt++; if (*fmt == 'h') { length = 2; fmt++; } }
        else if (*fmt == 'l') { length = 3; fmt++; if (*fmt == 'l') { length = 4; fmt++; } }
        else if (*fmt == 'z' || *fmt == 'j' || *fmt == 't') { length = 4; fmt++; }

        switch (*fmt) {
            case 'd': case 'i': {
                long long val = (length == 4) ? va_arg(args, long long) : va_arg(args, int);
                int is_neg = (val < 0) ? 1 : 0;
                unsigned long long uval = is_neg ? (unsigned long long)(-val) : (unsigned long long)val;
                print_int(ctx, uval, 10, is_neg, width, precision, flags);
                break;
            }
            case 'u': case 'o': case 'x': case 'X': {
                unsigned long long val = (length == 4) ? va_arg(args, unsigned long long) : va_arg(args, unsigned int);
                int base = (*fmt == 'o') ? 8 : ((*fmt == 'u') ? 10 : 16);
                if (*fmt == 'X') flags |= 0x10;
                print_int(ctx, val, base, 0, width, precision, flags);
                break;
            }
            case 'c': printf_out_char(ctx, (char)va_arg(args, int)); break;
            case 's': {
                char *s = va_arg(args, char *);
                if (!s) s = "(null)";
                size_t len = strlen(s);
                if (precision >= 0 && (size_t)precision < len) len = precision;
                if (!(flags & 0x01)) { for (size_t p = len; p < (size_t)width; p++) printf_out_char(ctx, ' '); }
                printf_out_str(ctx, s, len);
                if (flags & 0x01) { for (size_t p = len; p < (size_t)width; p++) printf_out_char(ctx, ' '); }
                break;
            }
            case 'p': {
                unsigned long long val = (unsigned long long)va_arg(args, void *);
                printf_out_str(ctx, "0x", 2);
                print_int(ctx, val, 16, 0, width, precision, flags);
                break;
            }
            case 'f': {
                double val = va_arg(args, double);
                if (precision < 0) precision = 6;
                print_float(ctx, val, precision, width, flags);
                break;
            }
            case '%': printf_out_char(ctx, '%'); break;
            default: printf_out_char(ctx, *fmt); break;
        }
        fmt++;
    }
    return ctx->count;
}

int vfprintf(FILE *stream, const char *fmt, va_list args) {
    printf_ctx_t ctx = { stream, NULL, 0, 0, 0 };
    return format_parser(&ctx, fmt, args);
}

int fprintf(FILE *stream, const char *fmt, ...) {
    va_list args; va_start(args, fmt);
    int res = vfprintf(stream, fmt, args);
    va_end(args); return res;
}

int vprintf(const char *fmt, va_list args) { return vfprintf(stdout, fmt, args); }
int printf(const char *fmt, ...) {
    va_list args; va_start(args, fmt);
    int res = vprintf(fmt, args);
    va_end(args); return res;
}

int vsnprintf(char *str, size_t size, const char *fmt, va_list args) {
    printf_ctx_t ctx = { NULL, str, 0, size, 0 };
    int res = format_parser(&ctx, fmt, args);
    if (str && size > 0) str[(ctx.pos < size) ? ctx.pos : size - 1] = '\0';
    return res;
}

int snprintf(char *str, size_t size, const char *fmt, ...) {
    va_list args; va_start(args, fmt);
    int res = vsnprintf(str, size, fmt, args);
    va_end(args); return res;
}

int vsprintf(char *str, const char *fmt, va_list args) { return vsnprintf(str, 0xFFFFFFFF, fmt, args); }
int sprintf(char *str, const char *fmt, ...) {
    va_list args; va_start(args, fmt);
    int res = vsprintf(str, fmt, args);
    va_end(args); return res;
}

typedef struct {
    FILE *f;
    const char *str;
    size_t pos;
    int eof;
    int unget;
} scanf_ctx_t;

static int scanf_get_char(scanf_ctx_t *ctx) {
    if (ctx->unget != -1) { int c = ctx->unget; ctx->unget = -1; return c; }
    if (ctx->f) {
        int c = fgetc(ctx->f);
        if (c == EOF) ctx->eof = 1;
        return c;
    }
    if (ctx->str) {
        if (ctx->str[ctx->pos] == '\0') { ctx->eof = 1; return EOF; }
        return (unsigned char)ctx->str[ctx->pos++];
    }
    return EOF;
}

static void scanf_unget_char(scanf_ctx_t *ctx, int c) { ctx->unget = c; }

static int scan_parser(scanf_ctx_t *ctx, const char *fmt, va_list args) {
    int matched = 0;
    while (*fmt) {
        if (isspace((unsigned char)*fmt)) {
            while (isspace((unsigned char)*fmt)) fmt++;
            int c;
            do { c = scanf_get_char(ctx); } while (isspace(c));
            if (c != EOF) scanf_unget_char(ctx, c);
            continue;
        }
        if (*fmt != '%') {
            int c = scanf_get_char(ctx);
            if (c != *fmt) { if (c != EOF) scanf_unget_char(ctx, c); break; }
            fmt++; continue;
        }
        fmt++;
        int suppress = 0;
        if (*fmt == '*') { suppress = 1; fmt++; }
        int width = 0;
        while (*fmt >= '0' && *fmt <= '9') { width = width * 10 + (*fmt - '0'); fmt++; }
        int length = 0;
        if (*fmt == 'h') { length = 1; fmt++; if (*fmt == 'h') { length = 2; fmt++; } }
        else if (*fmt == 'l') { length = 3; fmt++; if (*fmt == 'l') { length = 4; fmt++; } }

        if (*fmt == 'd' || *fmt == 'i' || *fmt == 'u' || *fmt == 'o' || *fmt == 'x' || *fmt == 'X') {
            int c; do { c = scanf_get_char(ctx); } while (isspace(c));
            if (c == EOF) break;
            int neg = 0;
            if (c == '-') { neg = 1; c = scanf_get_char(ctx); }
            else if (c == '+') { c = scanf_get_char(ctx); }

            int base = 10;
            if (*fmt == 'i') {
                if (c == '0') { c = scanf_get_char(ctx); if (c == 'x' || c == 'X') { base = 16; c = scanf_get_char(ctx); } else { base = 8; } }
            } else if (*fmt == 'o') base = 8;
            else if (*fmt == 'x' || *fmt == 'X') base = 16;

            unsigned long long val = 0;
            int any = 0;
            while (c != EOF && (width == 0 || width-- > 0)) {
                int digit = -1;
                if (c >= '0' && c <= '9') digit = c - '0';
                else if (c >= 'a' && c <= 'f') digit = c - 'a' + 10;
                else if (c >= 'A' && c <= 'F') digit = c - 'A' + 10;
                if (digit < 0 || digit >= base) break;
                val = val * base + digit;
                any = 1;
                c = scanf_get_char(ctx);
            }
            if (c != EOF) scanf_unget_char(ctx, c);
            if (!any) break;
            if (neg) val = -val;
            if (!suppress) {
                if (length == 4) *va_arg(args, unsigned long long *) = val;
                else if (length == 3) *va_arg(args, unsigned long *) = val;
                else if (length == 1) *va_arg(args, unsigned short *) = val;
                else if (length == 2) *va_arg(args, unsigned char *) = val;
                else *va_arg(args, unsigned int *) = val;
                matched++;
            }
        } else if (*fmt == 's') {
            int c; do { c = scanf_get_char(ctx); } while (isspace(c));
            if (c == EOF) break;
            char *dest = suppress ? NULL : va_arg(args, char *);
            int len = 0;
            while (c != EOF && !isspace(c) && (width == 0 || width-- > 0)) {
                if (dest) dest[len] = c;
                len++;
                c = scanf_get_char(ctx);
            }
            if (c != EOF) scanf_unget_char(ctx, c);
            if (dest) dest[len] = '\0';
            if (!suppress) matched++;
        } else if (*fmt == 'c') {
            int count = (width == 0) ? 1 : width;
            char *dest = suppress ? NULL : va_arg(args, char *);
            for (int i = 0; i < count; i++) {
                int c = scanf_get_char(ctx);
                if (c == EOF) break;
                if (dest) dest[i] = c;
            }
            if (!suppress) matched++;
        } else if (*fmt == '%') {
            int c = scanf_get_char(ctx);
            if (c != '%') { if (c != EOF) scanf_unget_char(ctx, c); break; }
        }
        fmt++;
    }
    return matched;
}

int vsscanf(const char *str, const char *fmt, va_list args) {
    scanf_ctx_t ctx = { NULL, str, 0, 0, -1 };
    return scan_parser(&ctx, fmt, args);
}

int sscanf(const char *str, const char *fmt, ...) {
    va_list args; va_start(args, fmt);
    int res = vsscanf(str, fmt, args);
    va_end(args); return res;
}

int vfscanf(FILE *stream, const char *fmt, va_list args) {
    scanf_ctx_t ctx = { stream, NULL, 0, 0, -1 };
    return scan_parser(&ctx, fmt, args);
}

int fscanf(FILE *stream, const char *fmt, ...) {
    va_list args; va_start(args, fmt);
    int res = vfscanf(stream, fmt, args);
    va_end(args); return res;
}

int vscanf(const char *fmt, va_list args) { return vfscanf(stdin, fmt, args); }
int scanf(const char *fmt, ...) {
    va_list args; va_start(args, fmt);
    int res = vscanf(fmt, args);
    va_end(args); return res;
}
