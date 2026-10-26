#!/usr/bin/env bash
# m7_diag_move.sh - 单帧运动隔离诊断 (在 Pi 上跑)
# 病因隔离: 0x30 同步包不驱动舵机, 但查询帧正常。测单帧 0x31 运动能否驱动健康 ID1。
#   若单帧动 → 同步包 {G0000} 特有问题; 若单帧不动 → 力矩被关(ULK), 需 torque on 恢复。
# 健康 ID1 小幅 1500->1560 (+60脉宽~5°), 读完立即归位。零风险 ID6 不碰。
set -u
DEV=/dev/ttyAMA0

echo "svc=$(systemctl is-active duanyan-edge)"
sudo -n systemctl stop duanyan-edge
sleep 1
sudo -n stty -F "$DEV" 9600 raw -echo

echo "=== 单帧运动隔离诊断 (ID1) ==="
{
  echo warm
  sleep 2
  echo "### baseline"      >&2
  echo "arm read 1"
  sleep 1
  echo "### single 0x31 move 1500->1560" >&2
  echo "arm set 1 1560 600"
  sleep 3
  echo "arm read 1"
  sleep 1
  echo "### revert 1560->1500" >&2
  echo "arm set 1 1500 600"
  sleep 3
  echo "arm read 1"
  sleep 1
  echo link
  echo quit
} | sudo -n ~/edge_daemon "$DEV" 2>&1

echo "=== 恢复服务 ==="
sudo -n systemctl start duanyan-edge
sleep 1
echo "svc_final=$(systemctl is-active duanyan-edge)"
echo "DIAG_MOVE_DONE"
