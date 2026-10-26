#!/usr/bin/env bash
# probe_flash.sh - 烧 uart_zxprobe 并读回校验 (在 Pi 上跑)
set -u
IMG=~/pi_build_out/uart_zxprobe.bin
PORT=/dev/ttyACM0
EF=/usr/local/bin/espflash
SZ=$(stat -c%s "$IMG")

echo "svc_before=$(systemctl is-active duanyan-edge)"
sudo -n systemctl stop duanyan-edge
echo "svc_after=$(systemctl is-active duanyan-edge)"

echo "--- flash ($SZ bytes) ---"
sudo -n "$EF" -S write-bin 0x0 "$IMG" -p "$PORT" --after hard-reset 2>&1 | tail -15
FRC=${PIPESTATUS[0]}
echo "flash_rc=$FRC"
if [ "$FRC" != "0" ]; then echo "FLASH_FAIL"; exit 1; fi

sleep 6
echo "--- readback verify ---"
sudo -n "$EF" -S read-flash 0 "$SZ" /tmp/zxprobe_back.bin -p "$PORT" 2>&1 | tail -5
if cmp -s "$IMG" /tmp/zxprobe_back.bin; then
  echo "VERIFY_MATCH=YES"
else
  echo "VERIFY_MATCH=NO"
fi
echo "PROBE_FLASH_DONE"
