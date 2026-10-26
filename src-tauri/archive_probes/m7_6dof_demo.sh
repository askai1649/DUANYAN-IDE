#!/usr/bin/env bash
# m7_6dof_demo.sh - 全 6 自由度联动取放演示 (在 Pi 上跑)
# 6 关节: ID6底座转盘 + ID1~4臂 + ID5夹爪。全部 0x30 同步关键帧驱动(现含 ID6 已解禁)。
# 所有目标卡在 zx_limits.json 安全带内; 底座旋转时手臂保持收拢(中位)以减小扫掠半径; 慢速柔和。
# 剧本: 预备->底座转到取物侧->弯下够->夹住->抬起->底座转到放置侧->放下->松开->归位。
# ⚠️ 底座旋转会带动整条手臂摆动, 周围须清空、线留余量、朱老师守电源旁。
set -u
DEV=/dev/ttyAMA0

echo "svc=$(systemctl is-active duanyan-edge)"
sudo -n systemctl stop duanyan-edge
sleep 1
sudo -n stty -F "$DEV" 9600 raw -echo

echo "=== 全 6 自由度取放演示 (底座会带整臂摆动, 守好电源) ==="
{
  echo warm
  sleep 2

  echo "### K0 预备: 底座中位 + 臂收拢 + 夹爪开" >&2
  echo "arm sync 6:1500:1000,1:1523:1000,2:1491:1000,3:1507:1000,4:1491:1000,5:1250:1000"
  sleep 3

  echo "### K1 底座转向取物侧 (6->1700), 臂保持收拢" >&2
  echo "arm sync 6:1700:1500,1:1523:1500,2:1491:1500,3:1507:1500,4:1491:1500,5:1250:1500"
  sleep 4
  echo "arm read 6"

  echo "### K2 弯下够向目标 (ID1~4 +80), 夹爪保持开" >&2
  echo "arm sync 6:1700:1200,1:1603:1200,2:1571:1200,3:1587:1200,4:1571:1200,5:1250:1200"
  sleep 3

  echo "### K3 夹住 (夹爪 ID5 闭合 1510)" >&2
  echo "arm sync 6:1700:800,1:1603:800,2:1571:800,3:1587:800,4:1571:800,5:1510:800"
  sleep 3
  echo "arm read 5"

  echo "### K4 抬起 (臂回收拢, 夹爪保持闭)" >&2
  echo "arm sync 6:1700:1200,1:1523:1200,2:1491:1200,3:1507:1200,4:1491:1200,5:1510:1200"
  sleep 3

  echo "### K5 底座转向放置侧 (6->1300), 臂收拢夹着" >&2
  echo "arm sync 6:1300:1500,1:1523:1500,2:1491:1500,3:1507:1500,4:1491:1500,5:1510:1500"
  sleep 4
  echo "arm read 6"

  echo "### K6 放下 (臂弯下, 夹爪保持闭)" >&2
  echo "arm sync 6:1300:1200,1:1603:1200,2:1571:1200,3:1587:1200,4:1571:1200,5:1510:1200"
  sleep 3

  echo "### K7 松开 (夹爪张开 1250)" >&2
  echo "arm sync 6:1300:800,1:1603:800,2:1571:800,3:1587:800,4:1571:800,5:1250:800"
  sleep 3

  echo "### K8 归位: 底座回中 + 臂收拢 + 夹爪开" >&2
  echo "arm sync 6:1500:1500,1:1523:1500,2:1491:1500,3:1507:1500,4:1491:1500,5:1250:1500"
  sleep 4
  echo "arm read 6"; echo "arm read 1"; echo "arm read 5"
  sleep 1
  echo link
  echo quit
} | sudo -n ~/edge_daemon "$DEV" 2>&1

echo "=== 恢复服务 ==="
sudo -n systemctl start duanyan-edge
sleep 1
echo "svc_final=$(systemctl is-active duanyan-edge)"
echo "DEMO6DOF_DONE"
