set -eu
cd "$(dirname "$0")"
PGT="${PGT:-../build/pgt}"
LIMINE_DIR="${LIMINE_DIR:-/usr/share/limine}"
timeout 110 "$PGT" build . --freestanding -o os.elf
rm -rf iso
mkdir -p iso/boot/limine iso/EFI/BOOT
cp os.elf iso/boot/os.elf
cp limine.conf iso/boot/limine/limine.conf
cp "$LIMINE_DIR/limine-bios.sys" "$LIMINE_DIR/limine-bios-cd.bin" "$LIMINE_DIR/limine-uefi-cd.bin" iso/boot/limine/
cp "$LIMINE_DIR/BOOTX64.EFI" iso/EFI/BOOT/
rm -f os.iso
xorriso -as mkisofs -R -r -J -b boot/limine/limine-bios-cd.bin -no-emul-boot -boot-load-size 4 \
    -boot-info-table -hfsplus -apm-block-size 2048 --efi-boot boot/limine/limine-uefi-cd.bin \
    -efi-boot-part --efi-boot-image --protective-msdos-label iso -o os.iso 2> xorriso.log || { cat xorriso.log; exit 1; }
rm -f xorriso.log
if command -v limine > /dev/null; then
    limine bios-install os.iso > /dev/null
fi
echo "built os.elf and os.iso"
