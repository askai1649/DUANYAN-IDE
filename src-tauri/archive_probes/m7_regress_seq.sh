#!/usr/bin/env bash
# m7_regress_seq.sh - 0x23 序列回放回归 (shake 1 循环短摆头, 在 Pi 上跑)
# 验证 m7 加装 ZX 驱动后 seq_play() 路径不退化。shake 走 0x23, S3 阻塞自播、播放期失聪,
# 故用带 sleep 的命令流喂入: 播完再 warm 拉回链路, 确认 tx_noack 归零、link 回暖。
set -u
DEV=/dev/ttyAMA0

echo "svc=$(systemctl is-active duanyan-edge)"
sudo -n systemctl stop duanyan-edge
sleep 1
sudo -n stty -F "$DEV" 9600 raw -echo

echo "=== 0x23 回归: warm -> shake 1 -> (等播完) -> warm -> link ==="
{
  echo warm
  sleep 2
  echo "shake 1"
  sleep 20
  echo warm
  sleep 2
  echo link
  sleep 1
  echo quit
} | sudo -n ~/edge_daemon "$DEV" 2>&1

echo "=== 恢复服务 ==="
sudo -n systemctl start duanyan-edge
sleep 1
echo "svc_final=$(systemctl is-active duanyan-edge)"
echo "SEQ_REGRESS_DONE"
