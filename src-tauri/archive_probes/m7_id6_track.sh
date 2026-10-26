#!/usr/bin/env bash
# m7_id6_track.sh - ID6 手摇跟踪测试 (纯查询连读, 零风险, 在 Pi 上跑)
# 目的: 区分 ID6 回读 1512 是"真活反馈"还是"卡在新死值"。
# 连读 ID6 位置 ~25s, 期间朱老师用手慢慢来回转动转盘; 数字跟着手变=反馈活, 恒 1512=仍死。
set -u
DEV=/dev/ttyAMA0

echo "svc=$(systemctl is-active duanyan-edge)"
sudo -n systemctl stop duanyan-edge
sleep 1
sudo -n stty -F "$DEV" 9600 raw -echo

echo "=== ID6 手摇跟踪: 20 次连读, 请现在开始慢慢来回转动转盘 ==="
{
  echo warm
  sleep 2
  for i in $(seq 1 20); do
    echo "arm read 6"
    sleep 1.1
  done
  echo link
  echo quit
} | sudo -n ~/edge_daemon "$DEV" 2>&1

echo "=== 恢复服务 ==="
sudo -n systemctl start duanyan-edge
sleep 1
echo "svc_final=$(systemctl is-active duanyan-edge)"
echo "ID6_TRACK_DONE"
