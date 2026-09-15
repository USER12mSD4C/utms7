#include "../lib/libc.h"
#include "../include/termios.h"

#define MAX_LINES 1000
#define MAX_LINE_LEN 256
#define SCREEN_ROWS 24
#define SCREEN_COLS 80
#define VIEW_TOP 1
#define VIEW_ROWS (SCREEN_ROWS - 2)
#define BOTTOM_ROW (SCREEN_ROWS - 1)

typedef struct {
    char text[MAX_LINE_LEN];
    int len;
} Line;

static Line lines[MAX_LINES];
static int num_lines = 0;
static int cursor_row = 0;
static int cursor_col = 0;
static int scroll_offset = 0;
static int insert_mode = 0;
static char filename[256] = "untitled";
static int modified = 0;

static char last_view[VIEW_ROWS][MAX_LINE_LEN];
static char last_top[256];

static struct termios orig_termios;

static void disable_raw_mode(void) {
    tcsetattr(0, 0, &orig_termios);
}

static void enable_raw_mode(void) {
    tcgetattr(0, &orig_termios);
    struct termios raw = orig_termios;
    raw.c_lflag &= ~(ECHO | ICANON);
    tcsetattr(0, 0, &raw);
}

static void move_cursor(int row, int col) {
    char buf[32];
    snprintf(buf, sizeof(buf), "\x1b[%d;%dH", row + 1, col + 1);
    write(1, buf, strlen(buf));
}

static void draw_top(void) {
    char bar[256];
    int n = snprintf(bar, sizeof(bar), " %s %s%s",
                     filename,
                     modified ? "[+] " : "",
                     insert_mode ? "-- INSERT --" : "-- NORMAL --");
    if (strcmp(bar, last_top) == 0) return;
    strncpy(last_top, bar, sizeof(last_top) - 1);
    last_top[sizeof(last_top) - 1] = 0;
    move_cursor(0, 0);
    write(1, "\x1b[7m", 4);
    write(1, bar, n);
    for (int i = n; i < SCREEN_COLS; i++) write(1, " ", 1);
    write(1, "\x1b[0m", 4);
}

static void draw_view(void) {
    for (int i = 0; i < VIEW_ROWS; i++) {
        int li = scroll_offset + i;
        const char* content;
        if (li < num_lines) content = lines[li].text;
        else if (li == num_lines) content = "~";
        else content = "";
        if (strcmp(content, last_view[i]) == 0) continue;
        strncpy(last_view[i], content, MAX_LINE_LEN - 1);
        last_view[i][MAX_LINE_LEN - 1] = 0;
        move_cursor(VIEW_TOP + i, 0);
        write(1, content, strlen(content));
        write(1, "\x1b[K", 3);
    }
}

static void clear_bottom(void) {
    move_cursor(BOTTOM_ROW, 0);
    write(1, "\x1b[K", 3);
}

static void draw_screen(void) {
    draw_top();
    draw_view();
    move_cursor(VIEW_TOP + cursor_row - scroll_offset, cursor_col);
}

static void load_file(const char* path) {
    strncpy(filename, path, sizeof(filename) - 1);
    filename[sizeof(filename) - 1] = 0;

    int fd = open(path, O_RDONLY);
    if (fd < 0) {
        num_lines = 1;
        lines[0].len = 0;
        lines[0].text[0] = 0;
        return;
    }

    char buf[4096];
    int total_read = 0;
    while (total_read < (int)sizeof(buf) - 1) {
        int r = read(fd, buf + total_read, sizeof(buf) - total_read - 1);
        if (r <= 0) break;
        total_read += r;
    }
    buf[total_read] = 0;
    close(fd);

    num_lines = 0;
    int line_start = 0;
    for (int i = 0; i <= total_read; i++) {
        if (buf[i] == '\n' || buf[i] == 0) {
            int len = i - line_start;
            if (len >= MAX_LINE_LEN) len = MAX_LINE_LEN - 1;
            memcpy(lines[num_lines].text, buf + line_start, len);
            lines[num_lines].text[len] = 0;
            lines[num_lines].len = len;
            num_lines++;
            if (num_lines >= MAX_LINES) break;
            line_start = i + 1;
        }
    }
    if (num_lines == 0) {
        num_lines = 1;
        lines[0].len = 0;
        lines[0].text[0] = 0;
    }
}

static void save_file(void) {
    int fd = open(filename, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) return;
    for (int i = 0; i < num_lines; i++) {
        write(fd, lines[i].text, lines[i].len);
        if (i < num_lines - 1) write(fd, "\n", 1);
    }
    close(fd);
    modified = 0;
}

static void insert_char(char c) {
    if (lines[cursor_row].len >= MAX_LINE_LEN - 1) return;
    memmove(&lines[cursor_row].text[cursor_col + 1],
            &lines[cursor_row].text[cursor_col],
            lines[cursor_row].len - cursor_col + 1);
    lines[cursor_row].text[cursor_col] = c;
    lines[cursor_row].len++;
    cursor_col++;
    modified = 1;
}

static void delete_char(void) {
    if (cursor_col >= lines[cursor_row].len) return;
    memmove(&lines[cursor_row].text[cursor_col],
            &lines[cursor_row].text[cursor_col + 1],
            lines[cursor_row].len - cursor_col);
    lines[cursor_row].len--;
    modified = 1;
}

static void insert_newline(void) {
    if (num_lines >= MAX_LINES - 1) return;
    int right_len = lines[cursor_row].len - cursor_col;
    memmove(&lines[cursor_row + 1], &lines[cursor_row],
            (num_lines - cursor_row) * sizeof(Line));
    memcpy(lines[cursor_row + 1].text,
           lines[cursor_row].text + cursor_col, right_len);
    lines[cursor_row + 1].len = right_len;
    lines[cursor_row + 1].text[right_len] = 0;
    lines[cursor_row].len = cursor_col;
    lines[cursor_row].text[cursor_col] = 0;
    num_lines++;
    cursor_row++;
    cursor_col = 0;
    modified = 1;
}

static void backspace(void) {
    if (cursor_col > 0) {
        cursor_col--;
        delete_char();
    } else if (cursor_row > 0) {
        int prev_len = lines[cursor_row - 1].len;
        if (prev_len + lines[cursor_row].len < MAX_LINE_LEN) {
            memcpy(lines[cursor_row - 1].text + prev_len,
                   lines[cursor_row].text, lines[cursor_row].len);
            lines[cursor_row - 1].len += lines[cursor_row].len;
            memmove(&lines[cursor_row], &lines[cursor_row + 1],
                    (num_lines - cursor_row - 1) * sizeof(Line));
            num_lines--;
            cursor_row--;
            cursor_col = prev_len;
            modified = 1;
        }
    }
}

static void handle_command(const char* cmd) {
    if (strcmp(cmd, "w") == 0) {
        save_file();
    } else if (strcmp(cmd, "q") == 0) {
        if (!modified) {
            disable_raw_mode();
            exit(0);
        }
    } else if (strcmp(cmd, "q!") == 0) {
        disable_raw_mode();
        exit(0);
    } else if (strcmp(cmd, "wq") == 0) {
        save_file();
        disable_raw_mode();
        exit(0);
    }
}

static void clamp_cursor(void) {
    if (cursor_row >= num_lines) cursor_row = num_lines - 1;
    if (cursor_col > lines[cursor_row].len) cursor_col = lines[cursor_row].len;
    if (cursor_row < scroll_offset) scroll_offset = cursor_row;
    if (cursor_row >= scroll_offset + VIEW_ROWS) {
        scroll_offset = cursor_row - VIEW_ROWS + 1;
    }
}

int main(int argc, char** argv) {
    if (argc > 1) {
        load_file(argv[1]);
    } else {
        num_lines = 1;
        lines[0].len = 0;
        lines[0].text[0] = 0;
    }

    for (int i = 0; i < VIEW_ROWS; i++) last_view[i][0] = 1;
    last_top[0] = 0;

    enable_raw_mode();
    write(1, "\x1b[2J\x1b[H", 7);
    clear_bottom();
    draw_screen();

    while (1) {
        char c;
        read(0, &c, 1);

        if (c == 27) {
            char seq[3];
            struct pollfd pfd;
            pfd.fd = 0;
            pfd.events = POLLIN;
            pfd.revents = 0;

            int pr = poll(&pfd, 1, 50);
            if (pr <= 0 || !(pfd.revents & POLLIN)) {
                insert_mode = 0;
                draw_screen();
                continue;
            }

            if (read(0, &seq[0], 1) != 1) { insert_mode = 0; draw_screen(); continue; }
            if (read(0, &seq[1], 1) != 1) { insert_mode = 0; draw_screen(); continue; }

            if (seq[0] == '[') {
                switch (seq[1]) {
                    case 'A': cursor_row--; break;
                    case 'B': cursor_row++; break;
                    case 'C': cursor_col++; break;
                    case 'D': cursor_col--; break;
                }
                clamp_cursor();
            } else {
                insert_mode = 0;
            }
            draw_screen();
        } else if (insert_mode) {
            if (c == 127 || c == 8) {
                backspace();
            } else if (c == '\r' || c == '\n') {
                insert_newline();
            } else if (c >= 32) {
                insert_char(c);
            }
            clamp_cursor();
            draw_screen();
        } else {
            if (c == 'i') {
                insert_mode = 1;
                draw_screen();
            } else if (c == 'x') {
                delete_char();
                draw_screen();
            } else if (c == 'h') { cursor_col--; clamp_cursor(); draw_screen(); }
            else if (c == 'j') { cursor_row++; clamp_cursor(); draw_screen(); }
            else if (c == 'k') { cursor_row--; clamp_cursor(); draw_screen(); }
            else if (c == 'l') { cursor_col++; clamp_cursor(); draw_screen(); }
            else if (c == ':') {
                char cmd[32];
                int cmd_len = 0;
                clear_bottom();
                move_cursor(BOTTOM_ROW, 0);
                write(1, ":", 1);
                while (cmd_len < (int)sizeof(cmd) - 1) {
                    char kc;
                    read(0, &kc, 1);
                    if (kc == '\r' || kc == '\n') break;
                    if (kc == 27) { cmd_len = 0; break; }
                    if (kc == 127 || kc == 8) {
                        if (cmd_len > 0) {
                            cmd_len--;
                            write(1, "\b \b", 3);
                        }
                        continue;
                    }
                    cmd[cmd_len++] = kc;
                    write(1, &kc, 1);
                }
                cmd[cmd_len] = 0;
                if (cmd_len > 0) handle_command(cmd);
                clear_bottom();
                draw_screen();
            }
        }
    }

    return 0;
}
