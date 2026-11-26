set -eu
cd "$(dirname "$0")"
VM=pgt-os
WANT="${1:-PGT OS 0.1}"
bash run.sh --headless
found=0
for i in $(seq 1 30); do
    if [ -f serial.log ] && grep -q "$WANT" serial.log; then
        found=1
        break
    fi
    sleep 1
done
sleep 3
VBoxManage controlvm "$VM" poweroff > /dev/null 2>&1 || true
sleep 2
cat serial.log 2>/dev/null || true
[ "$found" = 1 ]
