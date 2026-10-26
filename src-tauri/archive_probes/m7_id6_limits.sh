#!/usr/bin/env bash
# m7_id6_limits.sh - ID6 释力手转录机械限位 (零驱动零堵转, 在 Pi 上跑)
# 方法: 力矩关(ULK) -> 朱老师把 ID6 慢慢转到两端物理到头(各停3s) -> 连读捕获真实 [lo,hi]
#       -> 力矩恢复(ULR) -> 命令回中位1500(大行程给足T1800) -> 回读确认。
# 顺带再确认连接器固紧后反馈依然活。ID6 是底座, 释力期朱老师扶住手臂。
set -u
DEV=/dev/ttyAMA0

echo "svc=$(systemctl is-active duanyan-edge)"
sudo -n systemctl stop duanyan-edge
sleep 1
sudo -n stty -F "$DEV" 9600 raw -echo

echo "=== ID6 释力录限位: 释力后请慢转到一端到头(停3s)->再转到另一端到头(停3s), 连读 30s ==="
{
  echo warm
  sleep 2
  echo "arm torque 6 off"
  sleep 1
  for i in $(seq 1 30); do
    echo "arm read 6"
    sleep 1.0
  done
  echo "arm torque 6 on"
  sleep 1
  echo "### 回中位 1500 (大行程 T1800)" >&2
  echo "arm set 6 1500 1800"
  sleep 5
  echo "arm read 6"
  echo link
  echo quit
} | sudo -n ~/edge_daemon "$DEV" 2>&1

echo "=== 恢复服务 ==="
sudo -n systemctl start duanyan-edge
sleep 1
echo "svc_final=$(systemctl is-active duanyan-edge)"
echo "ID6_LIMITS_DONE"
