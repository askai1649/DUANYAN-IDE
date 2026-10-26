#!/usr/bin/env bash
# m7_id6_probe.sh - ID6 安全查询探针 (纯查询帧, 不改状态, 在 Pi 上跑)
# ID6 反馈死+保修中+底座承重: 运动帧(堵转烧机)/力矩关(整臂垮)都危险, 只做纯查询。
# 探: 位置(多次看是否恒 500) + RTE 温度电压; 用健康 ID1 的 pos/temp 做对照,
# 判断"RTE 无应答"是 ID6 独有还是本固件/舵机型号普遍不支持。
set -u
DEV=/dev/ttyAMA0

echo "svc=$(systemctl is-active duanyan-edge)"
sudo -n systemctl stop duanyan-edge
sleep 1
sudo -n stty -F "$DEV" 9600 raw -echo

echo "=== ID6 安全查询探针 (纯查询, 零状态改动) ==="
{
  echo warm
  sleep 2
  echo "### ID1 对照: pos + temp(RTE)" >&2
  echo "arm read 1"; sleep 1
  echo "arm read 1 temp"; sleep 1
  echo "### ID6: pos x3 (看是否恒 500 / 有无跳变)" >&2
  echo "arm read 6"; sleep 1
  echo "arm read 6"; sleep 1
  echo "arm read 6"; sleep 1
  echo "### ID6: temp(RTE) x2" >&2
  echo "arm read 6 temp"; sleep 1
  echo "arm read 6 temp"; sleep 1
  echo link
  echo quit
} | sudo -n ~/edge_daemon "$DEV" 2>&1

echo "=== 恢复服务 ==="
sudo -n systemctl start duanyan-edge
sleep 1
echo "svc_final=$(systemctl is-active duanyan-edge)"
echo "ID6_PROBE_DONE"
