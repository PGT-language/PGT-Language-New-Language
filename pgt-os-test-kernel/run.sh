set -eu
cd "$(dirname "$0")"
DIR="$PWD"
VM=pgt-os
MODE=gui
for a in "$@"; do
    case "$a" in
        --headless) MODE=headless ;;
        *) echo "usage: run.sh [--headless]" >&2; exit 1 ;;
    esac
done
[ -f os.iso ] || { echo "os.iso is missing; run build.sh first" >&2; exit 1; }
if ! VBoxManage showvminfo "$VM" --machinereadable > /dev/null 2>&1; then
    VBoxManage createvm --name "$VM" --ostype Other_64 --register > /dev/null
    VBoxManage storagectl "$VM" --name IDE --add ide > /dev/null
fi
VBoxManage modifyvm "$VM" --memory 256 --vram 16 --firmware bios --ioapic on \
    --boot1 dvd --boot2 none --boot3 none --boot4 none \
    --uart1 0x3F8 4 --uartmode1 file "$DIR/serial.log" > /dev/null
VBoxManage storageattach "$VM" --storagectl IDE --port 0 --device 0 --type dvddrive \
    --medium "$DIR/os.iso" --forceunmount > /dev/null
rm -f serial.log
VBoxManage startvm "$VM" --type "$MODE"
