# Compiler and Tools definitions
CC = gcc
AS = nasm
LD = ld

# Target flags to build a raw, standalone 32-bit protected-mode binary
CFLAGS = -m32 -ffreestanding -fno-stack-protector -nostartfiles -nodefaultlibs -Wall -Wextra -c
ASFLAGS = -f elf32
LDFLAGS = -m elf_i386 -T linker.ld

# Output targets
KERNEL = myos.bin
DISK_IMAGE = rootos.img

# Build pipeline
all: $(DISK_IMAGE)

boot.o: boot.asm
	$(AS) $(ASFLAGS) boot.asm -o boot.o

kernel.o: kernel.c
	$(CC) $(CFLAGS) kernel.c -o kernel.o

$(KERNEL): boot.o kernel.o
	$(LD) $(LDFLAGS) boot.o kernel.o -o $(KERNEL)

$(DISK_IMAGE): $(KERNEL)
	@echo "[+] Creating a native raw disk layout image..."
	# 1. Create a blank 10MB image file filled with zeros
	dd if=/dev/zero of=$(DISK_IMAGE) bs=1M count=10
	
	# 2. Format the raw layout into a standard FAT16 partition
	mkfs.vfat -F 16 $(DISK_IMAGE)
	
	# 3. Use mtools to safely copy your compiled files directly into the FAT disk structure
	mmd -i $(DISK_IMAGE) ::/boot
	mmd -i $(DISK_IMAGE) ::/boot/grub
	mcopy -i $(DISK_IMAGE) $(KERNEL) ::/boot/myos.bin
	
	# 4. Generate a clean configuration map inside the virtual drive
	@echo 'set timeout=0' > grub.cfg
	@echo 'set default=0' >> grub.cfg
	@echo 'menuentry "RootOS" {' >> grub.cfg
	@echo '    multiboot /boot/myos.bin' >> grub.cfg
	@echo '    boot' >> grub.cfg
	@echo '}' >> grub.cfg
	mcopy -i $(DISK_IMAGE) grub.cfg ::/boot/grub/grub.cfg
	@rm grub.cfg
	
		# 5. Use GRUB's native raw utilities to install the boot sectors directly to the disk image
	grub-install --target=i386-pc --boot-directory=iso/boot --grub-setup=/bin/true --modules="biosdisk part_msdos fat" $(DISK_IMAGE)
	grub-bios-setup -d rootos.img/boot/grub/i386-pc -b rootos.img/boot/grub/i386-pc/boot.img -c rootos.img/boot/grub/i386-pc/core.img $(DISK_IMAGE)

clean:
	rm -rf *.o $(KERNEL) $(DISK_IMAGE) grub.cfg

