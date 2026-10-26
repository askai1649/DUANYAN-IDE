#!/usr/bin/env bash
# m7_id6_motion.sh - ID6 受控运动测试 (解禁后首次运动帧, 在 Pi 上跑)
# 前提: 固件已刷 ZX_FORBID_ID=255u 版(ID6 解禁) + ID6 连接器已固紧 + 朱老师守电源旁随时可断电。
# 目的: 证明不只反馈活、闭环控制也活 —— 命令去某位能走到就停(不单向顶死堵转)。
# 安全: 目标全在实测自由范围[1039,2438]内; 慢速 T1500/T800 柔和; 每步回读确认到位且保持。
# 剧本: 预读确认保持 -> 走中位1500 -> 回读x2确认到位不跑飞 -> 双向1500<->1600 -> 归中。
set -u
DEV=/dev/ttyAMA0

echo "svc=$(systemctl is-active duanyan-edge)"
sudo -n systemctl stop duanyan-edge
sleep 1
sudo -n stty -F "$DEV" 9600 raw -echo

echo "=== ID6 受控运动测试 (守好电源, 异常立即断电) ==="
{
  echo warm
  sleep 2
  echo "### 预读: 确认 ID6 当前保持位置(应 ~1039)" >&2
  echo "arm read 6"; sleep 1
  echo "arm read 6"; sleep 1

  echo "### 走中位 1500 (慢速 T1500), 看是否闭环到位即停" >&2
  echo "arm set 6 1500 1500"; sleep 4
  echo "arm read 6"; sleep 1
  echo "arm read 6"; sleep 1

  echo "### 双向确认: 1500->1600->1500 (小幅, 全程在范围内)" >&2
  echo "arm set 6 1600 800"; sleep 3
  echo "arm read 6"; sleep 1
  echo "arm set 6 1500 800"; sleep 3
  echo "arm read 6"; sleep 1

  echo link
  echo quit
} | sudo -n ~/edge_daemon "$DEV" 2>&1

echo "=== 恢复服务 ==="
sudo -n systemctl start duanyan-edge
sleep 1
echo "svc_final=$(systemctl is-active duanyan-edge)"
echo "ID6_MOTION_DONE"
