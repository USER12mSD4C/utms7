#ifndef UNIX_H
#define UNIX_H

#include "../include/types.h"

#define UNIX_MAX_PATH 108
#define UNIX_BUF_SIZE 4096

typedef struct unix_socket {
    int id;
    char path[UNIX_MAX_PATH];
    int listening;
    int connected;
    int closed;

    struct unix_socket *peer;

    u8 write_buf[UNIX_BUF_SIZE];
    int write_pos;
    int write_len;

    struct unix_socket *backlog[16];
    int backlog_count;
    int backlog_head;
    int backlog_tail;
} unix_socket_t;

int unix_init(void);
int unix_socket_create(void);
int unix_bind(int fd, const char *path);
int unix_listen(int fd, int backlog);
int unix_accept(int fd);
int unix_connect(int fd, const char *path);
int unix_send(int fd, const void *buf, int len);
int unix_recv(int fd, void *buf, int len);
int unix_close(int fd);

#endif
