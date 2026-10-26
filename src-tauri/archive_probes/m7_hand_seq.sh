#!/usr/bin/env bash
# m7_hand_seq.sh - 按关节地图自下而上逐个测试 (在 Pi 上跑)
# 地图(用户定): ID6=最底转盘(禁动); ID1->ID5 自下而上; ID5=夹爪(顶端)。
# 剧本: 每个 ID 单独 +80 脉宽小幅动 -> 回读确认 -> 立即归位, 顺序 1->2->3->4->5。
# 让朱老师目视确认每一节对应哪个关节、方向、到位精度(重点看 ID2/ID5 负载)。ID6 全程不碰。
set -u
DEV=/dev/ttyAMA0

echo "svc=$(systemctl is-active duanyan-edge)"
sudo -n systemctl stop duanyan-edge
sleep 1
sudo -n stty -F "$DEV" 9600 raw -echo

echo "=== 自下而上逐关节测试 ID1->ID5 (ID5=夹爪) ==="
{
  echo warm
  sleep 2

  echo "### ID1 (最下节, 近转盘) 1500->1580" >&2
  echo "arm set 1 1580 700"; sleep 3; echo "arm read 1"
  echo "arm set 1 1500 700"; sleep 3

  echo "### ID2 (第二节) 1469->1549" >&2
  echo "arm set 2 1549 700"; sleep 3; echo "arm read 2"
  echo "arm set 2 1469 700"; sleep 3

  echo "### ID3 (第三节) 1508->1588" >&2
  echo "arm set 3 1588 700"; sleep 3; echo "arm read 3"
  echo "arm set 3 1508 700"; sleep 3

  echo "### ID4 (第四节) 1506->1586" >&2
  echo "arm set 4 1586 700"; sleep 3; echo "arm read 4"
  echo "arm set 4 1506 700"; sleep 3

  echo "### ID5 (夹爪/顶端) 1451->1531" >&2
  echo "arm set 5 1531 700"; sleep 3; echo "arm read 5"
  echo "arm set 5 1451 700"; sleep 3

  echo "### 全部归位确认 ID1~5" >&2
  echo "arm read 1"; echo "arm read 2"; echo "arm read 3"; echo "arm read 4"; echo "arm read 5"
  sleep 1
  echo link
  echo quit
} | sudo -n ~/edge_daemon "$DEV" 2>&1

echo "=== 恢复服务 ==="
sudo -n systemctl start duanyan-edge
sleep 1
echo "svc_final=$(systemctl is-active duanyan-edge)"
echo "HAND_SEQ_DONE"
