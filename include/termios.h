#ifndef TERMIOS_H
#define TERMIOS_H

#include "types.h"

typedef u32 tcflag_t;
typedef u8 cc_t;

#define NCCS 19

struct termios {
    tcflag_t c_iflag;
    tcflag_t c_oflag;
    tcflag_t c_cflag;
    tcflag_t c_lflag;
    cc_t c_cc[NCCS];
};

#define VMIN     6
#define VTIME    5

#define ICANON   0000002
#define ECHO     0000010
#define ECHOE    0000020
#define ECHOK    0000040
#define ECHONL   0000100
#define ISIG     0000001

#define TCGETS   0x5401
#define TCSETS   0x5402
#define TCSETSW  0x5403
#define TCSETSF  0x5404

#define TIOCGWINSZ 0x5413
#define TIOCSWINSZ 0x5414

struct winsize {
    u16 ws_row;
    u16 ws_col;
    u16 ws_xpixel;
    u16 ws_ypixel;
};

#endif
