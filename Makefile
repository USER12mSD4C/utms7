AS = nasm
CC = gcc
LD = ld

ASFLAGS = -f elf64
CFLAGS = -m64 -ffreestanding -nostdlib -Iinclude -fno-stack-protector -mno-red-zone -mcmodel=large -mno-sse -O2
DEBUG_CFLAGS = -m64 -ffreestanding -nostdlib -Iinclude -fno-stack-protector -mno-red-zone -mcmodel=large -mno-sse -DDEBUG -O0 -g
LDFLAGS = -m elf_x86_64 -T linker.ld -nostdlib

OBJS = kernel/entry.o \
       kernel/kernel.o \
       kernel/kinit.o \
       kernel/idt.o \
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
       fs/ufs.o \
       fs/fat.o \
       lib/string.o \
       lib/font.o \
       shell/shell.o \
       shell/uss.o \
       commands/builtin.o \
       commands/disk.o \
       commands/fs.o \
       apps/uwr.o

kernel.bin: $(OBJS)
	$(LD) $(LDFLAGS) -o $@ $^

DEBUG_OBJS = $(OBJS:%.o=%-debug.o)
kernel-debug.bin: $(DEBUG_OBJS)
	$(LD) $(LDFLAGS) -o $@ $^

%-debug.o: %.c
	$(CC) $(DEBUG_CFLAGS) -c -o $@ $<

kernel/entry-debug.o: kernel/entry.asm
	$(AS) $(ASFLAGS) -o $@ $<

kernel/gdt-debug.o: kernel/gdt.asm
	$(AS) $(ASFLAGS) -o $@ $<

kernel/entry.o: kernel/entry.asm
	$(AS) $(ASFLAGS) -o $@ $<

kernel/gdt.o: kernel/gdt.asm
	$(AS) $(ASFLAGS) -o $@ $<

%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $<

iso: kernel.bin
	mkdir -p iso/boot/grub
	cp kernel.bin iso/boot/kernel.bin
	echo 'set timeout=0' > iso/boot/grub/grub.cfg
	echo 'set default=0' >> iso/boot/grub/grub.cfg
	echo 'menuentry "UTMS" { multiboot2 /boot/kernel.bin; boot }' >> iso/boot/grub/grub.cfg
	grub-mkrescue -o utms.iso iso/

iso-debug: kernel-debug.bin
	mkdir -p iso/boot/grub
	cp kernel-debug.bin iso/boot/kernel.bin
	echo 'set timeout=0' > iso/boot/grub/grub.cfg
	echo 'set default=0' >> iso/boot/grub/grub.cfg
	echo 'menuentry "UTMS (DEBUG)" { multiboot2 /boot/kernel.bin; boot }' >> iso/boot/grub/grub.cfg
	grub-mkrescue -o utms-debug.iso iso/

disk5g.img:
	dd if=/dev/zero of=disk5g.img bs=1M count=5120

run: iso disk5g.img
	qemu-system-x86_64 -cdrom utms.iso -m 512 -hda disk5g.img -vga std -global VGA.vgamem_mb=64

run-debug: iso-debug disk5g.img
	qemu-system-x86_64 -cdrom utms-debug.iso -m 512 -hda disk5g.img -vga std -global VGA.vgamem_mb=64 \
		-d int,cpu_reset -no-reboot -no-shutdown -D qemu.log -s -S -monitor stdio

run-serial: iso-debug disk5g.img
	qemu-system-x86_64 -cdrom utms-debug.iso -m 512 -hda disk5g.img -vga std -global VGA.vgamem_mb=64 \
		-serial file:serial.log -monitor stdio

runD: iso disk5g.img
	qemu-system-x86_64 -cdrom utms.iso -m 512 -hda disk5g.img -vga std -global VGA.vgamem_mb=64 \
		-no-reboot -no-shutdown -d int -D qemu.log

run-real: iso
	@echo "Запись на USB флешку (например /dev/sdb):"
	@echo "sudo dd if=utms.iso of=/dev/sdb bs=1M status=progress"
	@echo "И загрузка с неё на реальном железе"

clean:
	rm -rf *.o */*.o *.bin *.iso iso/ disk5g.img qemu.log serial.log

.PHONY: all clean run run-debug run-serial runD run-real iso iso-debug
