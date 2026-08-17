#include "unix.h"
#include "sched.h"
#include "memory.h"
#include "../include/string.h"

#define MAX_UNIX_SOCKETS 64

static unix_socket_t sockets[MAX_UNIX_SOCKETS];
static int next_socket_id = 1;

int unix_init(void) {
    memset(sockets, 0, sizeof(sockets));
    return 0;
}

static unix_socket_t *find_socket_by_path(const char *path) {
    for (int i = 0; i < MAX_UNIX_SOCKETS; i++) {
        if (sockets[i].id != 0 && !sockets[i].closed && sockets[i].path[0] != '\0') {
            if (strcmp(sockets[i].path, path) == 0) return &sockets[i];
        }
    }
    return NULL;
}

static unix_socket_t *alloc_socket(void) {
    for (int i = 0; i < MAX_UNIX_SOCKETS; i++) {
        if (sockets[i].id == 0) {
            memset(&sockets[i], 0, sizeof(unix_socket_t));
            sockets[i].id = next_socket_id++;
            return &sockets[i];
        }
    }
    return NULL;
}

int unix_socket_create(void) {
    unix_socket_t *sock = alloc_socket();
    if (!sock) return -1;

    process_t *p = sched_current();
    if (!p) return -1;

    int fd = -1;
    for (int i = 0; i < 32; i++) {
        if (!p->fds[i].used) { fd = i; break; }
    }
    if (fd == -1) return -1;

    p->fds[fd].used = 1;
    p->fds[fd].type = FD_TYPE_UNIX;
    p->fds[fd].data.unix_sock = sock;
    return fd;
}

int unix_bind(int fd, const char *path) {
    process_t *p = sched_current();
    if (!p || fd < 0 || fd >= 32 || !p->fds[fd].used) return -1;
    if (p->fds[fd].type != FD_TYPE_UNIX) return -1;

    unix_socket_t *sock = (unix_socket_t *)p->fds[fd].data.unix_sock;
    if (!sock || sock->closed) return -1;

    if (find_socket_by_path(path)) return -1;

    strncpy(sock->path, path, UNIX_MAX_PATH - 1);
    sock->path[UNIX_MAX_PATH - 1] = '\0';
    return 0;
}

int unix_listen(int fd, int backlog) {
    process_t *p = sched_current();
    if (!p || fd < 0 || fd >= 32 || !p->fds[fd].used) return -1;
    if (p->fds[fd].type != FD_TYPE_UNIX) return -1;

    unix_socket_t *sock = (unix_socket_t *)p->fds[fd].data.unix_sock;
    if (!sock || sock->closed) return -1;
    if (sock->path[0] == '\0') return -1;

    sock->listening = 1;
    sock->backlog_count = 0;
    sock->backlog_head = 0;
    sock->backlog_tail = 0;
    return 0;
}

int unix_accept(int fd) {
    process_t *p = sched_current();
    if (!p || fd < 0 || fd >= 32 || !p->fds[fd].used) return -1;
    if (p->fds[fd].type != FD_TYPE_UNIX) return -1;

    unix_socket_t *sock = (unix_socket_t *)p->fds[fd].data.unix_sock;
    if (!sock || sock->closed || !sock->listening) return -1;

    while (sock->backlog_count == 0) {
        if (sock->closed) return -1;
        sched_block_on(sock);
    }

    unix_socket_t *client = sock->backlog[sock->backlog_head];
    sock->backlog_head = (sock->backlog_head + 1) % 16;
    sock->backlog_count--;

    unix_socket_t *new_sock = alloc_socket();
    if (!new_sock) return -1;

    new_sock->connected = 1;
    new_sock->peer = client;
    client->peer = new_sock;
    client->connected = 1;

    int new_fd = -1;
    for (int i = 0; i < 32; i++) {
        if (!p->fds[i].used) { new_fd = i; break; }
    }
    if (new_fd == -1) {
        new_sock->closed = 1;
        return -1;
    }

    p->fds[new_fd].used = 1;
    p->fds[new_fd].type = FD_TYPE_UNIX;
    p->fds[new_fd].data.unix_sock = new_sock;

    sched_wakeup(client);
    return new_fd;
}

int unix_connect(int fd, const char *path) {
    process_t *p = sched_current();
    if (!p || fd < 0 || fd >= 32 || !p->fds[fd].used) return -1;
    if (p->fds[fd].type != FD_TYPE_UNIX) return -1;

    unix_socket_t *sock = (unix_socket_t *)p->fds[fd].data.unix_sock;
    if (!sock || sock->closed) return -1;

    unix_socket_t *server = find_socket_by_path(path);
    if (!server || !server->listening) return -1;
    if (server->backlog_count >= 16) return -1;

    server->backlog[server->backlog_tail] = sock;
    server->backlog_tail = (server->backlog_tail + 1) % 16;
    server->backlog_count++;

    sched_wakeup(server);

    while (!sock->connected && !sock->closed) {
        sched_block_on(sock);
    }

    if (sock->closed) return -1;
    return 0;
}

int unix_send(int fd, const void *buf, int len) {
    process_t *p = sched_current();
    if (!p || fd < 0 || fd >= 32 || !p->fds[fd].used) return -1;
    if (p->fds[fd].type != FD_TYPE_UNIX) return -1;

    unix_socket_t *sock = (unix_socket_t *)p->fds[fd].data.unix_sock;
    if (!sock || sock->closed || !sock->connected) return -1;

    unix_socket_t *peer = sock->peer;
    if (!peer || peer->closed) return -1;

    int written = 0;
    const u8 *data = (const u8 *)buf;

    while (written < len) {
        while (peer->write_len >= UNIX_BUF_SIZE) {
            if (peer->closed || sock->closed) return written > 0 ? written : -1;
            sched_block_on(peer);
        }

        int space = UNIX_BUF_SIZE - peer->write_len;
        int to_copy = len - written;
        if (to_copy > space) to_copy = space;

        for (int i = 0; i < to_copy; i++) {
            int idx = (peer->write_pos + peer->write_len) % UNIX_BUF_SIZE;
            peer->write_buf[idx] = data[written + i];
        }
        peer->write_len += to_copy;
        written += to_copy;

        sched_wakeup(peer);
    }

    return written;
}

int unix_recv(int fd, void *buf, int len) {
    process_t *p = sched_current();
    if (!p || fd < 0 || fd >= 32 || !p->fds[fd].used) return -1;
    if (p->fds[fd].type != FD_TYPE_UNIX) return -1;

    unix_socket_t *sock = (unix_socket_t *)p->fds[fd].data.unix_sock;
    if (!sock || sock->closed) return -1;

    while (sock->write_len == 0) {
        if (sock->closed || (sock->peer && sock->peer->closed)) return 0;
        sched_block_on(sock);
    }

    int read_count = 0;
    u8 *data = (u8 *)buf;

    while (read_count < len && sock->write_len > 0) {
        data[read_count] = sock->write_buf[sock->write_pos];
        sock->write_pos = (sock->write_pos + 1) % UNIX_BUF_SIZE;
        sock->write_len--;
        read_count++;
    }

    if (sock->peer && sock->write_len < UNIX_BUF_SIZE) {
        sched_wakeup(sock->peer);
    }

    return read_count;
}

int unix_close(int fd) {
    process_t *p = sched_current();
    if (!p || fd < 0 || fd >= 32 || !p->fds[fd].used) return -1;
    if (p->fds[fd].type != FD_TYPE_UNIX) return -1;

    unix_socket_t *sock = (unix_socket_t *)p->fds[fd].data.unix_sock;
    if (!sock) return -1;

    sock->closed = 1;
    sched_wakeup(sock);

    if (sock->peer) {
        sock->peer->peer = NULL;
        sched_wakeup(sock->peer);
    }

    p->fds[fd].used = 0;
    p->fds[fd].data.unix_sock = NULL;
    return 0;
}
