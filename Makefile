AS = nasm
CC = gcc
LD = ld
MKMOD = tools/mkmod

ASFLAGS = -f elf64
CFLAGS = -m64 -ffreestanding -nostdlib -Iinclude -fno-stack-protector -mno-red-zone -mcmodel=large -mno-sse -O2
LDFLAGS = -m elf_x86_64 -T linker.ld -nostdlib

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
            kernel/sched.o \
            kernel/sched_asm.o \
            drivers/vga.o \
            drivers/vesa.o \
            drivers/keyboard.o \
            drivers/mouse.o \
            drivers/disk.o \
            drivers/gpt.o \
            drivers/udisk.o \
            lib/string.o \
            lib/font.o \
            lib/path.o

NORMAL_OBJS = $(CORE_OBJS) \
              fs/ufs.o \
              fs/fat.o \
              shell/shell.o \
              shell/uss.o \
              commands/builtin.o \
              commands/fs.o \
              apps/uwr.o

LIVECD_OBJS = kernel/entry.o \
              kernel/kernel-livecd.o \
              kernel/kinit.o \
              kernel/idt.o \
              kernel/idt_irq.o \
              kernel/memory.o \
              kernel/paging.o \
              kernel/kapi.o \
              kernel/gdt.o \
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
              lib/string.o \
              lib/font.o \
              lib/path.o \
              fs/ufs.o \
              fs/fat.o \
              shell/shell.o \
              shell/uss.o \
              commands/builtin.o \
              commands/fs.o \
              apps/installer.o \
              apps/uwr.o

all: kernel.bin kom

kernel.bin: $(NORMAL_OBJS)
	$(LD) $(LDFLAGS) -o $@ $^

kernel-livecd.bin: $(LIVECD_OBJS)
	$(LD) $(LDFLAGS) -o $@ $^

$(MKMOD): tools/mkmod.c
	@mkdir -p tools
	gcc -o $(MKMOD) tools/mkmod.c

kom: $(MKMOD)
	@mkdir -p modules
	@echo "=== MODULES BUILT ==="

iso: kernel.bin kom
	@mkdir -p iso/boot/grub iso/modules
	cp kernel.bin iso/boot/kernel.bin
	cp modules/*.ko iso/modules/ 2>/dev/null || true
	echo 'set timeout=0' > iso/boot/grub/grub.cfg
	echo 'menuentry "UTMS" { multiboot2 /boot/kernel.bin; boot }' >> iso/boot/grub/grub.cfg
	grub-mkrescue -o utms.iso iso/

livecd: kernel-livecd.bin kom
	@mkdir -p livecd/boot/grub livecd/modules livecd/system/boot livecd/system/modules
	cp kernel-livecd.bin livecd/boot/kernel.bin
	cp kernel.bin livecd/system/boot/kernel.bin 2>/dev/null || true
	cp modules/*.ko livecd/modules/ 2>/dev/null || true
	cp modules/*.ko livecd/system/modules/ 2>/dev/null || true
	echo 'set timeout=5' > livecd/boot/grub/grub.cfg
	echo 'menuentry "UTMS LiveCD" {' >> livecd/boot/grub/grub.cfg
	echo '    multiboot2 /boot/kernel.bin' >> livecd/boot/grub/grub.cfg
	echo '    boot' >> livecd/boot/grub/grub.cfg
	echo '}' >> livecd/boot/grub/grub.cfg
	grub-mkrescue -o utms-livecd.iso livecd/

disk5g.img:
	dd if=/dev/zero of=disk5g.img bs=1M count=5120

run: iso disk5g.img
	qemu-system-x86_64 -cdrom utms.iso -m 512 -hda disk5g.img -vga std -global VGA.vgamem_mb=64

run-livecd: livecd disk5g.img
	qemu-system-x86_64 -cdrom utms-livecd.iso -m 512 -hda disk5g.img -vga std -global VGA.vgamem_mb=64 -boot d

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

clean:
	rm -rf *.o */*.o *.bin *.iso iso/ livecd/ disk5g.img

distclean: clean
	rm -rf modules/ tools/mkmod

.PHONY: all clean distclean run run-livecd iso livecd kom
