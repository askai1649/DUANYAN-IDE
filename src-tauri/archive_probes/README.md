# 归档区：探测/实验固件（2026-08 工程治理）

本目录收纳模式 B 链路打通过程中的**一次性探测固件**（86 → 归档 77）。

主干只保留里程碑链路与 HAL：

| 保留文件 | 角色 |
|---|---|
| `adc_common_v44.c` / `adc_hal_v44.c` | ADC HAL |
| `adc1.c` / `yl69.c` | YL-69 土壤湿度探针驱动 |
| `uart_link.c` → `linkb` → `linkm3` → `linkm4` → `linkm5` | 线协议固件演进链（M1→M5），当前烧录版为 `uart_linkm5.c` |

归档文件如需复活：直接 `Move-Item` 移回上级目录，用
`flash_test.exe COM4 --build src-tauri\<文件名>.c` 构建即可（flash_test 按传入路径构建，无硬编码清单）。
