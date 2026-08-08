#!/bin/bash
# Pi 侧: GPIO15 (脚10 RXD) 高阻采样, 观察是否跟随 ESP GPIO4 的 1s方波
# 需要 gpio 组权限; 若无权限则打印权限错误
echo "--- try sysfs export ---"
if echo 15 > /sys/class/gpio/export 2>/tmp/gpio_err; then
    echo "in" > /sys/class/gpio/gpio15/direction 2>/dev/null
    echo "SAMPLING GPIO15 (16 samples, 0.5s apart):"
    for i in $(seq 1 16); do
        v=$(cat /sys/class/gpio/gpio15/value 2>/dev/null)
        echo "t$i=$v"
        sleep 0.5
    done
    echo 15 > /sys/class/gpio/unexport 2>/dev/null
else
    echo "EXPORT_FAILED: $(cat /tmp/gpio_err)"
    echo "--- try /dev/gpiomem ---"
    ls -l /dev/gpiomem 2>/dev/null || echo "no gpiomem"
fi
echo GPIO_SAMPLE_DONE
