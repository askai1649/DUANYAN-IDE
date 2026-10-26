#!/usr/bin/env bash
# m7_id6_freetrack.sh - ID6 释力自由转跟踪测试 (在 Pi 上跑)
# 终极验证反馈是否真活: 释力(ULK)让 ID6 自由 -> 朱老师手转大弧 -> 连读看数字是否跟着扫大范围。
# 纯查询+释力/恢复力矩(配置帧, 可过禁动过滤), 无运动帧, 无堵转风险。
# 结束发力矩恢复(ULR): 若反馈活则安全保持当前位置。ID6 是底座, 全程朱老师扶住手臂。
set -u
DEV=/dev/ttyAMA0

echo "svc=$(systemctl is-active duanyan-edge)"
sudo -n systemctl stop duanyan-edge
sleep 1
sudo -n stty -F "$DEV" 9600 raw -echo

echo "=== ID6 释力自由转跟踪: 释力后请慢慢转一个大弧(左右来回), 连读 ~28s ==="
{
  echo warm
  sleep 2
  echo "arm torque 6 off"
  sleep 1
  for i in $(seq 1 26); do
    echo "arm read 6"
    sleep 1.0
  done
  echo "arm torque 6 on"
  sleep 1
  echo "arm read 6"
  echo link
  echo quit
} | sudo -n ~/edge_daemon "$DEV" 2>&1

echo "=== 恢复服务 ==="
sudo -n systemctl start duanyan-edge
sleep 1
echo "svc_final=$(systemctl is-active duanyan-edge)"
echo "ID6_FREETRACK_DONE"
