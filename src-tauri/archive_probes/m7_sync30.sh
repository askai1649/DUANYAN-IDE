#!/usr/bin/env bash
# m7_sync30.sh - 0x30 arm_sync 同步写验证 (在 Pi 上跑)
# 三段安全剧本: A) ID6 整帧拒(零风险, 目标=当前值双保险) B) 健康舵机小幅同步(±60脉宽~5°) C) 归位
# 每段后 arm read 回读确认真值。用带 sleep 命令流喂入, 等舵机走位到位再读。
set -u
DEV=/dev/ttyAMA0

echo "svc=$(systemctl is-active duanyan-edge)"
sudo -n systemctl stop duanyan-edge
sleep 1
sudo -n stty -F "$DEV" 9600 raw -echo

echo "=== 0x30 同步写三段验证 ==="
{
  echo warm
  sleep 2
  echo "--- A: ID6 整帧拒 (期望 status=1 applied=0, 零位移) ---" >&2
  echo "arm sync 6:500:500,1:1500:500"
  sleep 2
  echo "arm read 1"
  sleep 1
  echo "--- B: 健康 ID1+ID2 小幅同步 (期望 status=0 applied=2, 走到 ~1560/~1530) ---" >&2
  echo "arm sync 1:1560:600,2:1530:600"
  sleep 3
  echo "arm read 1"
  echo "arm read 2"
  sleep 1
  echo "--- C: 归位 (期望回到 ~1500/~1469) ---" >&2
  echo "arm sync 1:1500:600,2:1469:600"
  sleep 3
  echo "arm read 1"
  echo "arm read 2"
  sleep 1
  echo link
  echo quit
} | sudo -n ~/edge_daemon "$DEV" 2>&1

echo "=== 恢复服务 ==="
sudo -n systemctl start duanyan-edge
sleep 1
echo "svc_final=$(systemctl is-active duanyan-edge)"
echo "SYNC30_DONE"
