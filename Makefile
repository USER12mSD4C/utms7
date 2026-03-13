AS = nasm
CC = gcc
LD = ld
MKMOD = tools/mkmod

ASFLAGS = -f elf64
CFLAGS = -m64 -ffreestanding -nostdlib -Iinclude -fno-stack-protector -mno-red-zone -mcmodel=large -mno-sse -O2
DEBUG_CFLAGS = -m64 -ffreestanding -nostdlib -Iinclude -fno-stack-protector -mno-red-zone -mcmodel=large -mno-sse -DDEBUG -O0 -g
LDFLAGS = -m elf_x86_64 -T linker.ld -nostdlib

# Основные объекты ядра и драйверов
CORE_OBJS = kernel/entry.o \
            kernel/kernel.o \
            kernel/kinit.o \
            kernel/idt.o \
            kernel/idt_irq.o \
            kernel/memory.o \
            kernel/paging.o \
            kernel/kapi.o \
            kernel/gdt.o \
            kernel/panic.o \
            drivers/vga.o \
            drivers/vesa.o \
            drivers/keyboard.o \
            drivers/mouse.o \
            drivers/disk.o \
            drivers/gpt.o \
            lib/string.o \
            lib/font.o

# Объекты для обычной системы (без установщика)
NORMAL_OBJS = $(CORE_OBJS) \
              fs/ufs.o \
              fs/fat.o \
              shell/shell.o \
              shell/uss.o \
              commands/builtin.o \
              commands/disk.o \
              commands/fs.o \
              apps/uwr.o

# Объекты для livecd (с установщиком)
LIVECD_OBJS = $(CORE_OBJS) \
              fs/ufs.o \
              fs/fat.o \
              shell/shell.o \
              shell/uss.o \
              commands/builtin.o \
              commands/disk.o \
              commands/fs.o \
              apps/installer.o

# ========== ОСНОВНЫЕ ЦЕЛИ ==========

all: kernel.bin kom

# Обычное ядро
kernel.bin: $(NORMAL_OBJS)
	$(LD) $(LDFLAGS) -o $@ $^

# Ядро для livecd
kernel-livecd.bin: $(LIVECD_OBJS)
	$(LD) $(LDFLAGS) -o $@ $^

# ========== МОДУЛИ ==========
MODULE_SOURCES = $(wildcard drivers/*.c fs/*.c)
MODULE_NAMES = $(notdir $(basename $(MODULE_SOURCES)))
MODULE_TARGETS = $(addprefix modules/,$(addsuffix .ko,$(MODULE_NAMES)))

$(MKMOD): tools/mkmod.c
	@mkdir -p tools
	gcc -o $(MKMOD) tools/mkmod.c

kom: $(MKMOD) $(MODULE_TARGETS)
	@echo "=== MODULES BUILT ==="

modules/%.ko: drivers/%.o | $(MKMOD)
	@mkdir -p modules
	$(LD) -r -o $*.linked.o $<
	$(MKMOD) $*.linked.o $@ $*
	rm -f $*.linked.o

modules/%.ko: fs/%.o | $(MKMOD)
	@mkdir -p modules
	$(LD) -r -o $*.linked.o $<
	$(MKMOD) $*.linked.o $@ $*
	rm -f $*.linked.o

# ========== ISO ОБРАЗЫ ==========

# Обычный ISO для запуска
iso: kernel.bin kom
	@mkdir -p iso/boot/grub iso/modules
	cp kernel.bin iso/boot/kernel.bin
	cp modules/*.ko iso/modules/ 2>/dev/null || true
	echo 'set timeout=0' > iso/boot/grub/grub.cfg
	echo 'set default=0' >> iso/boot/grub/grub.cfg
	echo 'menuentry "UTMS" { multiboot2 /boot/kernel.bin; boot }' >> iso/boot/grub/grub.cfg
	grub-mkrescue -o utms.iso iso/

# LiveCD ISO с установщиком
livecd: kernel-livecd.bin kom
	@mkdir -p livecd/boot/grub livecd/modules livecd/system
	cp kernel-livecd.bin livecd/boot/kernel.bin
	cp modules/*.ko livecd/modules/ 2>/dev/null || true
	
	# Создаём директории для установки
	mkdir -p livecd/system/boot livecd/system/modules livecd/system/bin
	
	# Копируем системные файлы для установки
	cp kernel.bin livecd/system/boot/kernel.bin 2>/dev/null || true
	cp modules/*.ko livecd/system/modules/ 2>/dev/null || true
	
	# Создаём README для livecd
	echo "UTMS LiveCD - Installation System" > livecd/README
	echo "Run 'install /dev/sda' to install to disk" >> livecd/README
	
	# Создаём grub.cfg для livecd
	echo 'set timeout=5' > livecd/boot/grub/grub.cfg
	echo 'set default=0' >> livecd/boot/grub/grub.cfg
	echo 'menuentry "UTMS LiveCD (Installation Mode)" {' >> livecd/boot/grub/grub.cfg
	echo '    multiboot2 /boot/kernel.bin' >> livecd/boot/grub/grub.cfg
	echo '    boot' >> livecd/boot/grub/grub.cfg
	echo '}' >> livecd/boot/grub/grub.cfg
	
	grub-mkrescue -o utms-livecd.iso livecd/

# ========== ЗАПУСК ==========

disk5g.img:
	dd if=/dev/zero of=disk5g.img bs=1M count=5120

run: iso disk5g.img
	qemu-system-x86_64 -cdrom utms.iso -m 512 -hda disk5g.img -vga std -global VGA.vgamem_mb=64

run-livecd: livecd disk5g.img
	qemu-system-x86_64 -cdrom utms-livecd.iso -m 512 -hda disk5g.img -vga std -global VGA.vgamem_mb=64 -boot d

# ========== ОБЩИЕ ПРАВИЛА ==========

%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $<

kernel/entry.o: kernel/entry.asm
	$(AS) $(ASFLAGS) -o $@ $<

kernel/gdt.o: kernel/gdt.asm
	$(AS) $(ASFLAGS) -o $@ $<

kernel/idt_irq.o: kernel/idt_irq.asm
	$(AS) $(ASFLAGS) -o $@ $<

# ========== ОЧИСТКА ==========

clean:
	rm -rf *.o */*.o *.bin *.iso iso/ livecd/ disk5g.img qemu.log serial.log

distclean: clean
	rm -rf modules/ tools/mkmod

.PHONY: all clean distclean run run-livecd iso livecd kom
