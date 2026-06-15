#include "../lib/libc.h"

#define DRM_IOCTL_MODE_GETRESOURCES 0xC01064A0  // или возьми макрос из drm.h если libc его видит

int main() {
    int fd = open("/dev/dri/card0", 0);
    if (fd < 0) {
        printf("FAIL: open returned %d\n", fd);
        return 1;
    }
    printf("OK: fd=%d\n", fd);

    // пробуем ioctl
    int ret = ioctl(fd, 0, 0);
    printf("ioctl test: %d\n", ret);

    close(fd);
    return 0;
}
