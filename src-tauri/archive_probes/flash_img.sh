#!/usr/bin/env bash
# flash_img.sh <bin路径> - 烧录 + 读回校验 (防假成功), 在 Pi 上跑
set -u
IMG="$1"
PORT=/dev/ttyACM0
EF=/usr/local/bin/espflash
SZ=$(stat -c%s "$IMG")

echo "img=$IMG size=$SZ"
echo "svc_before=$(systemctl is-active duanyan-edge)"
sudo -n systemctl stop duanyan-edge
echo "svc_after=$(systemctl is-active duanyan-edge)"

echo "--- flash ---"
sudo -n "$EF" -S write-bin 0x0 "$IMG" -p "$PORT" --after hard-reset 2>&1 | tail -6
FRC=${PIPESTATUS[0]}
echo "flash_rc=$FRC"
if [ "$FRC" != "0" ]; then echo "FLASH_FAIL"; exit 1; fi

sleep 6
echo "--- readback verify ---"
sudo -n "$EF" -S read-flash 0 "$SZ" /tmp/flash_back.bin -p "$PORT" 2>&1 | tail -3
if cmp -s "$IMG" /tmp/flash_back.bin; then
  echo "VERIFY_MATCH=YES ($SZ B)"
else
  echo "VERIFY_MATCH=NO"
fi
echo "FLASH_DONE"
