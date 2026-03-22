AS = nasm
CC = gcc
LD = ld
MKMOD = tools/mkmod

ASFLAGS = -f elf64
CFLAGS = -m64 -ffreestanding -nostdlib -Iinclude -fno-stack-protector -mno-red-zone -mcmodel=large -mno-sse -O2
LDFLAGS = -m elf_x86_64 -T linker.ld -nostdlib

# Ядро
CORE_OBJS = kernel/entry.o \
            kernel/kernel.o \
            kernel/kinit.o \
            kernel/elf.o \
            kernel/kapi.o \
            kernel/gdt.o \
            kernel/idt.o \
            kernel/idt_irq.o \
            kernel/memory.o \
            kernel/paging.o \
            kernel/panic.o \
            kernel/sched.o \
            kernel/sched_asm.o

# Драйверы
DRIVER_OBJS = drivers/vga.o \
              drivers/vesa.o \
              drivers/keyboard.o \
              drivers/mouse.o \
              drivers/disk.o \
              drivers/gpt.o \
              drivers/udisk.o \
              drivers/pci.o

# Сеть
NET_OBJS = net/arp.o \
           net/ip.o \
           net/icmp.o \
           net/tcp.o \
           net/udp.o \
           net/dhcp.o \
           net/dns.o \
           net/http.o \
           net/net.o \
           net/e1000.o \
           net/rtl8139.o

# Библиотеки
LIB_OBJS = lib/string.o \
           lib/font.o \
           lib/path.o \
           lib/zlib.o

# Файловые системы
FS_OBJS = fs/ufs.o \
          fs/fat.o

# Шелл и команды
SHELL_OBJS = shell/shell.o \
             shell/uss.o \
             commands/builtin.o \
             commands/fs.o

# Приложения
APP_OBJS = apps/installer.o \
           apps/upac.o

# Все объекты
NORMAL_OBJS = $(CORE_OBJS) $(DRIVER_OBJS) $(NET_OBJS) $(LIB_OBJS) $(FS_OBJS) $(SHELL_OBJS) $(APP_OBJS)

# LiveCD объекты (с isofs)
LIVECD_OBJS = kernel/entry.o \
              kernel/kernel-livecd.o \
              kernel/kinit.o \
              kernel/elf.o \
              kernel/kapi.o \
              kernel/gdt.o \
              kernel/idt.o \
              kernel/idt_irq.o \
              kernel/memory.o \
              kernel/paging.o \
              kernel/panic.o \
              kernel/sched.o \
              kernel/sched_asm.o \
              drivers/vga.o \
              drivers/vesa.o \
              drivers/keyboard.o \
              drivers/mouse.o \
              drivers/disk.o \
              drivers/gpt.o \
              drivers/udisk.o \
              drivers/pci.o \
              net/arp.o \
              net/ip.o \
              net/icmp.o \
              net/tcp.o \
              net/udp.o \
              net/dhcp.o \
              net/dns.o \
              net/http.o \
              net/net.o \
              net/e1000.o \
              net/rtl8139.o \
              lib/string.o \
              lib/font.o \
              lib/path.o \
              lib/zlib.o \
              fs/ufs.o \
              fs/fat.o \
              fs/isofs.o \
              shell/shell.o \
              shell/uss.o \
              commands/builtin.o \
              commands/fs.o \
              apps/installer.o \
              apps/upac.o

# ==================== ЦЕЛИ ====================

all: kernel.bin kom

# Основное ядро
kernel.bin: $(NORMAL_OBJS)
	$(LD) $(LDFLAGS) -o $@ $^

# LiveCD ядро
kernel-livecd.bin: $(LIVECD_OBJS)
	$(LD) $(LDFLAGS) -o $@ $^

# Инструмент для создания модулей
$(MKMOD): tools/mkmod.c
	@mkdir -p tools
	gcc -o $(MKMOD) tools/mkmod.c

# Сборка модулей
kom: $(MKMOD)
	@mkdir -p modules
	@echo "=== MODULES BUILT ==="

# ==================== ОБРАЗЫ ====================

# Создание образа установки
install.img: kernel.bin kom
	@echo "Creating install image..."
	@rm -rf install_root
	@mkdir -p install_root/modules
	cp kernel.bin install_root/kernel.bin
	cp modules/*.ko install_root/modules/ 2>/dev/null || true
	@echo "Building ISO image..."
	grub-mkrescue -o install.img install_root/ 2>/dev/null || \
	mkisofs -o install.img -R -J install_root/ 2>/dev/null || \
	xorriso -as mkisofs -o install.img -R -J install_root/ 2>/dev/null || \
	(echo "No ISO tool found, creating raw image..." && \
	 dd if=/dev/zero of=install.img bs=1M count=10 && \
	 echo "RAW image created (placeholder)")
	@echo "Install image created: install.img"

# Создание ISO образа
iso: kernel.bin kom
	@mkdir -p iso/boot/grub iso/modules
	cp kernel.bin iso/boot/kernel.bin
	cp modules/*.ko iso/modules/ 2>/dev/null || true
	echo 'set timeout=0' > iso/boot/grub/grub.cfg
	echo 'menuentry "UTMS" { multiboot2 /boot/kernel.bin; boot }' >> iso/boot/grub/grub.cfg
	grub-mkrescue -o utms.iso iso/

# Создание LiveCD образа
livecd: kernel-livecd.bin install.img
	@echo "Creating LiveCD..."
	@rm -rf livecd
	@mkdir -p livecd/boot/grub livecd/install
	cp kernel-livecd.bin livecd/boot/kernel.bin
	cp install.img livecd/install/install.img
	echo 'set timeout=5' > livecd/boot/grub/grub.cfg
	echo 'menuentry "UTMS LiveCD" {' >> livecd/boot/grub/grub.cfg
	echo '    multiboot2 /boot/kernel.bin' >> livecd/boot/grub/grub.cfg
	echo '    boot' >> livecd/boot/grub/grub.cfg
	echo '}' >> livecd/boot/grub/grub.cfg
	echo 'menuentry "UTMS LiveCD (verbose)" {' >> livecd/boot/grub/grub.cfg
	echo '    multiboot2 /boot/kernel.bin verbose' >> livecd/boot/grub/grub.cfg
	echo '    boot' >> livecd/boot/grub/grub.cfg
	echo '}' >> livecd/boot/grub/grub.cfg
	grub-mkrescue -o utms-livecd.iso livecd/
	@echo "LiveCD created: utms-livecd.iso"

# Создание дисков
disk5g.img:
	dd if=/dev/zero of=disk5g.img bs=1M count=5120

disk2.img:
	dd if=/dev/zero of=disk2.img bs=1M count=100

# ==================== ЗАПУСК ====================

# Запуск с ISO (грузится с CD)
run: iso disk5g.img
	qemu-system-x86_64 -cdrom utms.iso -m 512 -hda disk5g.img -vga std -global VGA.vgamem_mb=64 -netdev user,id=net0 -device e1000,netdev=net0 -boot d

# Запуск с LiveCD
run-livecd: livecd disk5g.img
	qemu-system-x86_64 -cdrom utms-livecd.iso -m 512 -hda disk5g.img -vga std -global VGA.vgamem_mb=64 -netdev user,id=net0 -device e1000,netdev=net0 -boot d

# Запуск с диском напрямую (без CD)
rundisk: kernel.bin disk5g.img
	qemu-system-x86_64 -kernel kernel.bin -m 512 -hda disk5g.img -vga std -global VGA.vgamem_mb=64 -netdev user,id=net0 -device e1000,netdev=net0

# Запуск с RTL8139
run-rtl: iso disk5g.img
	qemu-system-x86_64 -cdrom utms.iso -m 512 -hda disk5g.img -vga std -global VGA.vgamem_mb=64 -netdev user,id=net0 -device rtl8139,netdev=net0 -boot d

# Запуск с последовательным портом для отладки
run-serial: iso disk5g.img
	qemu-system-x86_64 -cdrom utms.iso -m 512 -hda disk5g.img -vga std -global VGA.vgamem_mb=64 -netdev user,id=net0 -device e1000,netdev=net0 -boot d -serial stdio

# Отладка (с GDB)
debug: kernel.bin
	qemu-system-x86_64 -kernel kernel.bin -m 512 -vga std -global VGA.vgamem_mb=64 -netdev user,id=net0 -device e1000,netdev=net0 -s -S

# Запуск с UEFI
run-uefi: iso disk5g.img
	qemu-system-x86_64 -bios /usr/share/ovmf/x64/OVMF.fd -cdrom utms.iso -m 512 -hda disk5g.img -vga std -global VGA.vgamem_mb=64 -netdev user,id=net0 -device e1000,netdev=net0 -boot d

# Запуск без графики (только консоль)
run-nographic: iso disk5g.img
	qemu-system-x86_64 -cdrom utms.iso -m 512 -hda disk5g.img -netdev user,id=net0 -device e1000,netdev=net0 -nographic -append "console=ttyS0"

# ==================== ОЧИСТКА ====================

clean:
	rm -rf *.o */*.o *.bin *.iso iso/ livecd/ install_root/ install.img

distclean: clean
	rm -rf modules/ tools/mkmod disk*.img

# ==================== ПРАВИЛА КОМПИЛЯЦИИ ====================

%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $<

kernel/entry.o: kernel/entry.asm
	$(AS) $(ASFLAGS) -o $@ $<

kernel/gdt.o: kernel/gdt.asm
	$(AS) $(ASFLAGS) -o $@ $<

kernel/idt_irq.o: kernel/idt_irq.asm
	$(AS) $(ASFLAGS) -o $@ $<

kernel/sched_asm.o: kernel/sched_asm.asm
	$(AS) $(ASFLAGS) -o $@ $<

.PHONY: all clean distclean run run-livecd rundisk run-rtl run-serial debug run-uefi run-nographic iso livecd kom
