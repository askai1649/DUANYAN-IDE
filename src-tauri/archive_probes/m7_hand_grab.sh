#!/usr/bin/env bash
# m7_hand_grab.sh - "弯下去-夹住-抬起-松开" 多关节协同抓握 (在 Pi 上跑)
# 测 0x30 同步包逐关键帧驱动 ID1~5 协同。所有目标卡在 zx_limits.json 已确认安全行程内:
#   ID1 中位1523[644,2266] ID2 1491[582,2450] ID3 1507[562,2319] ID4 1491[594,2436] ID5 1485[961,1721]
# ID6(转盘,反馈死)全程不入帧(禁动)。弯=各关节中位+80(顺时针,探过安全); 夹爪张开走大范围(1250,远离接触安全)、
# 闭合走保守值(1520,避免撞指尖堵转)。方向若目视反了, 翻符号重跑。慢速 T=1000~1200 柔和走位。
set -u
DEV=/dev/ttyAMA0

echo "svc=$(systemctl is-active duanyan-edge)"
sudo -n systemctl stop duanyan-edge
sleep 1
sudo -n stty -F "$DEV" 9600 raw -echo

echo "=== 协同抓握: 弯下去-夹住-抬起-松开 ==="
{
  echo warm
  sleep 2

  echo "### K0 预备: 臂回中位 + 夹爪张开(1250)" >&2
  echo "arm sync 1:1523:1000,2:1491:1000,3:1507:1000,4:1491:1000,5:1250:1000"
  sleep 3

  echo "### K1 弯下去: ID1~4 中位+80 够向目标, 夹爪保持张开" >&2
  echo "arm sync 1:1603:1200,2:1571:1200,3:1587:1200,4:1571:1200,5:1250:1200"
  sleep 4
  echo "arm read 2"

  echo "### K2 夹住: 臂保持弯, 夹爪 ID5 闭合(1520)" >&2
  echo "arm sync 1:1603:600,2:1571:600,3:1587:600,4:1571:600,5:1520:800"
  sleep 3
  echo "arm read 5"

  echo "### K3 抬起: ID1~4 回中位起身, 夹爪保持闭合(夹着东西)" >&2
  echo "arm sync 1:1523:1200,2:1491:1200,3:1507:1200,4:1491:1200,5:1520:1200"
  sleep 4

  echo "### K4 松开归位: 夹爪张开(1250), 全臂回中位" >&2
  echo "arm sync 1:1523:1000,2:1491:1000,3:1507:1000,4:1491:1000,5:1250:1000"
  sleep 3
  echo "arm read 1"; echo "arm read 2"; echo "arm read 3"; echo "arm read 4"; echo "arm read 5"
  sleep 1
  echo link
  echo quit
} | sudo -n ~/edge_daemon "$DEV" 2>&1

echo "=== 恢复服务 ==="
sudo -n systemctl start duanyan-edge
sleep 1
echo "svc_final=$(systemctl is-active duanyan-edge)"
echo "HAND_GRAB_DONE"
