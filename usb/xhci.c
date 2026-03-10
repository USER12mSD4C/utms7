#include "xhci.h"
#include "../drivers/vga.h"
#include "../kernel/memory.h"
#include "../include/string.h"
#include "../include/io.h"

static volatile xhci_cap_regs_t *cap_regs;
static volatile xhci_op_regs_t *op_regs;

void xhci_init(void) {
    vga_write("Initializing XHCI... ");
    
    // TODO: найти контроллер через PCI
    cap_regs = (xhci_cap_regs_t*)XHCI_BASE;
    op_regs = (xhci_op_regs_t*)(XHCI_BASE + cap_regs->caplength);
    
    // Сброс контроллера
    op_regs->usbcmd = 2; // Reset
    while (op_regs->usbcmd & 2) {
        __asm__ volatile ("pause");
    }
    
    // Устанавливаем размер страницы
    if (op_regs->pagesize != 1) {
        vga_write("FAILED (page size)\n");
        return;
    }
    
    // Выделяем DCBAA
    u64 *dcbaa = kmalloc(4096);
    memset(dcbaa, 0, 4096);
    op_regs->dcbaap_low = (u32)(u64)dcbaa;
    op_regs->dcbaap_high = (u32)((u64)dcbaa >> 32);
    
    // Включаем контроллер
    op_regs->usbcmd = 1; // Run/Stop
    while (!(op_regs->usbsts & 1)) {
        __asm__ volatile ("pause");
    }
    
    vga_write("OK\n");
}

int xhci_handle_device(int port) {
    // TODO: реализовать
    (void)port;
    return -1;
}
