#include "panic.h"
#include "../drivers/drm.h"
#include "../include/io.h"
#include "../include/string.h"
#include "sched.h"

#define PANIC_ART_WIDTH 68

static const char* panic_art[] = {
    "++++++++.         +++++++++++++++-...............................",
    "+         ++++++++      +++++++++++-.............................",
    " +++++++++++++++++++++++.-  +++++++++--..........................",
    "++++.                   ++++. .++++++++ .........................",
    " -++++++++++++-++++ -+++++++   +++. ++++++ ......................",
    "++++++.     +++++-       ++++++++ ++ .+++++......................",
    "+ ++   +  -+++++- ++--.      -++++. +- +++++-....................",
    "+  +++++.--++++  ++   +++       -+++  +  ++++ ...................",
    "+++++++++.++++++++++++.+++         ++. ++ ++++...................",
    "+++++++.+ --++++.++++-  +++          ++  + +++...................",
    "++++++   .--+++-.+++ .++++++          ++  + +++ .................",
    "++++ .   -. --.++-  ++.+++++++         ++  + +++.................",
    "++++ -  -     .-  + - ++++++++ +        ++ + +++.................",
    "++++++-. .  +.-++ . .+-++++--- .+        ++  +++.................",
    "++++++++++  + +++++-+++++++.++ .++        + ++++ ................",
    " +++++++++++++++++..  --       ++-+    ++++ +++- ................",
    "  +++++++++++++++++    ++++++++++++   ++ .++   ..................",
    "  ++++++++-+++++++++++++++++ -- +++  +++++-- ....................",
    "   +++++++++. +++++++++.-++++++++-++++-  + ++....................",
    "   .+++-+-++++++++++++- ++-+++ ..++++++ ++++ ....................",
    ". .     +.      ++++++ .       -.- ++++-+..+-....................",
    ".-.++   .+- + ++++++++++  ++++++++++++-.+. - ....................",
    "-.-.         ++++++++++++++++++++++++++  +++ ....................",
    ".. +-     +-.-    ..+++++++++++++++++ ++++++ -...................",
    "+.+  --  +   -   . ++ -  +++++++++++++       -...................",
    " -----. - . - ++++++-+++++++++++++++++  .     ...................",
    "+-.--. .   ++ +++++ - ...+++++++++++++ -++    ...................",
    " ++---. - .+++++++    ++++++++++++++++ +++-   ...................",
    "++-+.--  +++++.     ++++++++++++++++++ ++++  .-..................",
    "-.+  +-+ +.     ++++++++++++++++++++  ++++++ -...................",
    "+ -++ ...-   +++++++++++++++++  -. - +++++++- ...................",
    "       -++. -+++++++++++. -    .  ++-++++++++ -..................",
    "..     -.---. +++++-.    ..-+-+ +++ ++++++++++-..................",
    "...+ -- +. + . .     .  .-. - +++++++++++++++++ ................."
};

#define PANIC_ART_LINES ((u32)(sizeof(panic_art) / sizeof(panic_art[0])))

static void print_hex(u64 num) {
    char hex[] = "0123456789ABCDEF";
    for (int i = 60; i >= 0; i -= 4) {
        print_char(hex[(num >> i) & 0xF]);
    }
}

static void print_num(u32 num) {
    char buf[16];
    int i = 0;
    if (num == 0) {
        print_char('0');
        return;
    }
    while (num > 0) {
        buf[i++] = '0' + (num % 10);
        num /= 10;
    }
    while (i > 0) print_char(buf[--i]);
}

static void panic_pad_art(const char* line) {
    u64 len = strlen(line);
    if (len >= PANIC_ART_WIDTH) {
        print_char(' ');
        return;
    }
    for (u64 i = len; i < PANIC_ART_WIDTH; i++) {
        print_char(' ');
    }
}

void panic(const char* message) {
    __asm__ volatile ("cli");

    u64 rsp;
    u64 rip;
    u64 cr3;

    __asm__ volatile("mov %%rsp, %0" : "=r"(rsp));
    __asm__ volatile("mov %%cr3, %0" : "=r"(cr3));

    rip = (u64)__builtin_return_address(0);

    print_setcolor(0x07, 0);
    print_clear();

    for (u32 i = 0; i < PANIC_ART_LINES; i++) {
        print_setcolor(0x0B, 0);
        print(panic_art[i]);
        panic_pad_art(panic_art[i]);

        print_setcolor(0x0F, 0);

        if (i == 0) {
            print("KERNEL PANIC");
        } else if (i == 1) {
            if (message) print(message);
        } else if (i == 2) {
            print("RIP=");
            print_hex(rip);
        } else if (i == 3) {
            print("RSP=");
            print_hex(rsp);
        } else if (i == 4) {
            print("CR3=");
            print_hex(cr3);
        }

        print("\n");
    }

    outb(0xE9, 'P');
    outb(0xE9, 'A');
    outb(0xE9, 'N');
    outb(0xE9, 'I');
    outb(0xE9, 'C');
    outb(0xE9, ':');

    if (message) {
        while (*message) outb(0xE9, *message++);
    }

    outb(0xE9, '\n');

    while (1) {
        __asm__ volatile ("cli; hlt");
    }
}

void panic_assert(const char* file, u32 line, const char* expr) {
    __asm__ volatile ("cli");

    print_setcolor(0x07, 0);
    print_clear();

    for (u32 i = 0; i < PANIC_ART_LINES; i++) {
        print_setcolor(0x0B, 0);
        print(panic_art[i]);
        panic_pad_art(panic_art[i]);

        print_setcolor(0x0F, 0);

        if (i == 0) {
            print("ASSERTION FAILED");
        } else if (i == 1) {
            print("FILE=");
            print(file);
        } else if (i == 2) {
            print("LINE=");
            print_num(line);
        } else if (i == 3) {
            print("EXPR=");
            print(expr);
        }

        print("\n");
    }

    while (1) {
        __asm__ volatile ("cli; hlt");
    }
}

void double_fault_handler(void) {
    __asm__ volatile ("cli");

    print_setcolor(0x07, 0);
    print_clear();

    for (u32 i = 0; i < PANIC_ART_LINES; i++) {
        print_setcolor(0x0B, 0);
        print(panic_art[i]);
        panic_pad_art(panic_art[i]);

        print_setcolor(0x0F, 0);

        if (i == 0) {
            print("DOUBLE FAULT");
        } else if (i == 1) {
            print("not your fault, right?");
        }

        print("\n");
    }

    while (1) {
        __asm__ volatile ("cli; hlt");
    }
}

void triple_fault_handler(void) {
    __asm__ volatile ("cli");

    print_setcolor(0x07, 0);
    print_clear();

    for (u32 i = 0; i < PANIC_ART_LINES; i++) {
        print_setcolor(0x0B, 0);
        print(panic_art[i]);
        panic_pad_art(panic_art[i]);

        print_setcolor(0x0F, 0);

        if (i == 0) {
            print("TRIPLE FAULT");
        }

        print("\n");
    }

    while (1) {
        __asm__ volatile ("cli; hlt");
    }
}

void panic_exception(int num, u64 error_code, u64 cr2, u64 rip, u64 cs, u64 rsp) {
    __asm__ volatile ("cli");

    u64 cr3;
    __asm__ volatile("mov %%cr3, %0" : "=r"(cr3));
    u64* rbp;
    __asm__ volatile("mov %%rbp, %0" : "=r"(rbp));

    process_t *cur = sched_current();

    print_setcolor(0x07, 0);
    print_clear();

    for (u32 i = 0; i < PANIC_ART_LINES; i++) {
        print_setcolor(0x0B, 0);
        print(panic_art[i]);
        panic_pad_art(panic_art[i]);

        print_setcolor(0x0F, 0);

        if (i == 0) {
            print("KERNEL EXCEPTION");
        } else if (i == 1) {
            print("VECTOR=");
            print_num((u32)num);
            print(" ERR=");
            print_hex(error_code);
        } else if (i == 2) {
            print("CR2=");
            print_hex(cr2);
            print(" RIP=");
            print_hex(rip);
        } else if (i == 3) {
            print("CS=");
            print_hex(cs);
            print(" RSP=");
            print_hex(rsp);
        } else if (i == 4) {
            print("CR3=");
            print_hex(cr3);
        } else if (i == 5) {
            print("PID=");
            if (cur) print_num(cur->pid);
            else print("null");
            print(" NAME=");
            if (cur) print(cur->name);
            else print("null");
        } else if (i == 6) {
            print("KSTACK=");
            if (cur) print_hex(cur->kstack);
            else print_hex(0);
            print(" KTOP=");
            if (cur) print_hex(cur->kstack_top);
            else print_hex(0);
        } else if (i == 7) {
            print("USER_RIP=");
            if (cur) print_hex(cur->user_rip);
            else print_hex(0);
        } else if (i == 8) {
            print("USER_RSP=");
            if (cur) print_hex(cur->user_rsp);
            else print_hex(0);
        } else if (i == 9) {
            print("CR3_PROC=");
            if (cur) print_hex(cur->cr3);
            else print_hex(0);
        } else if (i == 10) {
            print("STATE=");
            if (cur) print_num(cur->state);
            else print("null");
        } else if (i == 11) {
            print("BACKTRACE:");

            u64* frame = (u64*)rsp;

            for (int depth = 0; depth < 32 && frame; depth++) {
                u64 ret_addr = frame[1];
                if (ret_addr == 0) break;
                print("  [");
                print_num((u32)depth);
                print("] ");
                print_hex(ret_addr);

                u64* next_frame = (u64*)frame[0];
                if ((u64)next_frame <= (u64)frame || (u64)next_frame > (u64)frame + 0x10000) break;
                frame = next_frame;
            }
        }


        print("\n");
    }

    u32 label_pad = PANIC_ART_WIDTH + 45;
    if (label_pad > 10) label_pad -= 10;

    for (u32 i = 0; i < label_pad; i++) {
        print_char(' ');
    }

    print("UTMS7/JJBA\n");

    outb(0xE9, 'E');
    outb(0xE9, 'X');
    outb(0xE9, 'C');
    outb(0xE9, ':');
    outb(0xE9, (char)('0' + (num / 10)));
    outb(0xE9, (char)('0' + (num % 10)));
    outb(0xE9, '\n');

    while (1) {
        __asm__ volatile ("cli; hlt");
    }
}
