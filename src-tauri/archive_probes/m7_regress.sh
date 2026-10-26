#!/usr/bin/env bash
# m7_regress.sh - m6 老路径回归 (只读部分, 在 Pi 上跑)
# 验证加装 ZX361 UART1 驱动块 + zx_uart_init() 没碰坏 m6: 心跳(0x01)/颈四关节查询(0x21)/status(0x12)。
# 0x23 序列回放是物理摆头, 单独验证, 不在本只读脚本内。
set -u
DEV=/dev/ttyAMA0

echo "svc=$(systemctl is-active duanyan-edge)"
sudo -n systemctl stop duanyan-edge
sleep 1
sudo -n stty -F "$DEV" 9600 raw -echo

echo "=== 回归(只读): warm / joint(0x21 x4) / status(0x12) / stats / link ==="
printf 'warm\njoint\nstatus\nstats\nlink\nquit\n' \
  | sudo -n ~/edge_daemon "$DEV" 2>&1

echo "=== 恢复服务 ==="
sudo -n systemctl start duanyan-edge
sleep 1
echo "svc_final=$(systemctl is-active duanyan-edge)"
echo "REGRESS_DONE"
