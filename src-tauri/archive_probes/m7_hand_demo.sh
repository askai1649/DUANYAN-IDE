#!/usr/bin/env bash
# m7_hand_demo.sh - "Pi 指挥手" 五指联动演示 (在 Pi 上跑)
# 证明 daemon->m7->ZX361 总线能协同驱动健康舵机 ID1~5 (ID6 禁动不碰)。
# 剧本: 读基线 -> 0x30 同步包全 +80 脉宽(~7°, 一律朝中心区靠, 安全) -> 回读确认 -> 同步归位 -> 回读确认。
# 全部小幅、近中位、读完即归位; 每步 sleep 等 800ms 行程走到位再读。
set -u
DEV=/dev/ttyAMA0

echo "svc=$(systemctl is-active duanyan-edge)"
sudo -n systemctl stop duanyan-edge
sleep 1
sudo -n stty -F "$DEV" 9600 raw -echo

echo "=== Pi 指挥手: ID1~5 五指联动 ==="
{
  echo warm
  sleep 2
  echo "### 1) 基线回读 ID1~5" >&2
  echo "arm read 1"; echo "arm read 2"; echo "arm read 3"; echo "arm read 4"; echo "arm read 5"
  sleep 2
  echo "### 2) 0x30 同步: 五指一并 +80 脉宽" >&2
  echo "arm sync 1:1580:800,2:1549:800,3:1588:800,4:1586:800,5:1531:800"
  sleep 4
  echo "arm read 1"; echo "arm read 2"; echo "arm read 3"; echo "arm read 4"; echo "arm read 5"
  sleep 2
  echo "### 3) 0x30 同步: 归位回基线" >&2
  echo "arm sync 1:1500:800,2:1469:800,3:1508:800,4:1506:800,5:1451:800"
  sleep 4
  echo "arm read 1"; echo "arm read 2"; echo "arm read 3"; echo "arm read 4"; echo "arm read 5"
  sleep 1
  echo link
  echo quit
} | sudo -n ~/edge_daemon "$DEV" 2>&1

echo "=== 恢复服务 ==="
sudo -n systemctl start duanyan-edge
sleep 1
echo "svc_final=$(systemctl is-active duanyan-edge)"
echo "HAND_DEMO_DONE"
