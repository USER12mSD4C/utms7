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

NET_OBJS = net/arp.o \
           net/ip.o \
           net/icmp.o \
           net/tcp.o \
           net/udp.o \
           net/dhcp.o \
           net/dns.o \
           net/http.o \
           net/net.o \
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

# LiveCD объекты
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
              net/rtl8139.o \
              lib/string.o \
              lib/font.o \
              lib/path.o \
              lib/zlib.o \
              fs/ufs.o \
              fs/fat.o \
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

# Создание ISO образа
iso: kernel.bin kom
	@mkdir -p iso/boot/grub iso/modules
	cp kernel.bin iso/boot/kernel.bin
	cp modules/*.ko iso/modules/ 2>/dev/null || true
	echo 'set timeout=0' > iso/boot/grub/grub.cfg
	echo 'menuentry "UTMS" { multiboot2 /boot/kernel.bin; boot }' >> iso/boot/grub/grub.cfg
	grub-mkrescue -o utms.iso iso/

# Создание LiveCD образа
livecd: kernel-livecd.bin kernel.bin kom
	@echo "Creating LiveCD directory structure..."
	@rm -rf livecd
	@mkdir -p livecd/boot/grub
	@mkdir -p livecd/system/boot
	@mkdir -p livecd/system/modules
	@mkdir -p livecd/modules
	
	@echo "Copying LiveCD kernel..."
	cp kernel-livecd.bin livecd/boot/kernel.bin
	
	@echo "Copying installation kernel..."
	cp kernel.bin livecd/system/boot/kernel.bin
	
	@echo "Copying modules..."
	-cp modules/*.ko livecd/modules/ 2>/dev/null || true
	-cp modules/*.ko livecd/system/modules/ 2>/dev/null || true
	
	@echo "Copying additional files..."
	-cp -r include livecd/include/ 2>/dev/null || true
	-cp -r apps livecd/apps/ 2>/dev/null || true
	-cp -r commands livecd/commands/ 2>/dev/null || true
	-cp README.md livecd/ 2>/dev/null || true
	
	@echo "Creating grub.cfg..."
	echo 'set timeout=5' > livecd/boot/grub/grub.cfg
	echo 'set default=0' >> livecd/boot/grub/grub.cfg
	echo '' >> livecd/boot/grub/grub.cfg
	echo 'menuentry "UTMS LiveCD" {' >> livecd/boot/grub/grub.cfg
	echo '    multiboot2 /boot/kernel.bin' >> livecd/boot/grub/grub.cfg
	echo '    boot' >> livecd/boot/grub/grub.cfg
	echo '}' >> livecd/boot/grub/grub.cfg
	echo '' >> livecd/boot/grub/grub.cfg
	echo 'menuentry "UTMS LiveCD (verbose)" {' >> livecd/boot/grub/grub.cfg
	echo '    multiboot2 /boot/kernel.bin verbose' >> livecd/boot/grub/grub.cfg
	echo '    boot' >> livecd/boot/grub/grub.cfg
	echo '}' >> livecd/boot/grub/grub.cfg
	
	@echo "Creating ISO image..."
	grub-mkrescue -o utms-livecd.iso livecd/
	
	@echo "LiveCD created: utms-livecd.iso"
	@echo "Size: `du -h utms-livecd.iso | cut -f1`"

# Создание дисков
disk5g.img:
	dd if=/dev/zero of=disk5g.img bs=1M count=5120

disk2.img:
	dd if=/dev/zero of=disk2.img bs=1M count=100

# ==================== ЗАПУСК ====================

# Запуск с ISO (грузится с CD)
run: iso disk5g.img
	qemu-system-x86_64 -cdrom utms.iso -m 512 -hda disk5g.img -vga std -global VGA.vgamem_mb=64 -net nic,model=rtl8139 -net user -boot d

# Запуск с LiveCD
run-livecd: livecd disk5g.img
	qemu-system-x86_64 -cdrom utms-livecd.iso -m 512 -hda disk5g.img -vga std -global VGA.vgamem_mb=64 -boot d -net nic,model=rtl8139 -net user

# Запуск с диском напрямую (без CD)
rundisk: kernel.bin disk5g.img
	qemu-system-x86_64 -kernel kernel.bin -m 512 -hda disk5g.img -vga std -global VGA.vgamem_mb=64 -net nic,model=rtl8139 -net user

# Запуск с двумя дисками
run-dual: iso disk5g.img disk2.img
	qemu-system-x86_64 -cdrom utms.iso -m 512 -hda disk5g.img -hdb disk2.img -vga std -global VGA.vgamem_mb=64 -net nic,model=rtl8139 -net user -boot d

# Запуск с сетью
run-net: kernel.bin disk5g.img
	qemu-system-x86_64 -kernel kernel.bin -m 512 -hda disk5g.img -vga std -global VGA.vgamem_mb=64 -net nic,model=rtl8139 -net user -append "console=ttyS0"

# Отладка
debug: kernel.bin
	qemu-system-x86_64 -kernel kernel.bin -m 512 -vga std -global VGA.vgamem_mb=64 -net nic,model=rtl8139 -net user -s -S

# Запуск с UEFI (если нужно)
run-uefi: iso disk5g.img
	qemu-system-x86_64 -bios /usr/share/ovmf/x64/OVMF.fd -cdrom utms.iso -m 512 -hda disk5g.img -vga std -global VGA.vgamem_mb=64 -net nic,model=rtl8139 -net user -boot d

# ==================== ОЧИСТКА ====================

clean:
	rm -rf *.o */*.o *.bin *.iso iso/ livecd/

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

.PHONY: all clean distclean run run-livecd rundisk run-dual run-net debug run-uefi iso livecd kom
