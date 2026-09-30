/* uart_linkl15.c - 演进路线 L1.5: 颈四关节闭环 (模式 B 链路 + PCA9685 I²C 后端)
 * 基线: uart_linkl1.c (传输层位啜 / RMT 灯效 / ADC 传感 / 兜底自治全保留)
 * 后端换装: LEDC 单舵机 → PCA9685 I²C 位啮 6 关节 (SDA=GPIO8 SCL=GPIO9, 准开漏板载上拉)
 *   关节表 jt_*[4]: 颈四关节 J1-J4 自下而上 (2026-09-15 as-built 重映射),
 *     jt_ch[] = PCA CH{0,1,2,3} = pan/tilt/nod/roll (CH4/CH5 空), 上电中位 900
 * CMD 0x20 关节设置: 载荷=[joint][ang_hi][ang_lo][sw_lo_hi][sw_lo_lo][sw_hi_hi][sw_hi_lo]
 *   joint 0..3; ACK 载荷=[0x20回声][ang_hi][ang_lo][status]
 *   ang bit15 = 一次性扫描标志 (glide): 扫描到目标端点自动停扫 (lo=hi),
 *   使上位机点到点只需一帧且后续帧必落在空闲期 (2026-09-16 联动丢帧修复)
 *   status: 0=已写入 PCA / 1=关节非法 / 2=I²C 未被 PCA ACK (软件态已更新, 硬件未生效)
 * CMD 0x21 关节查询: 载荷=[joint]; ACK 载荷(9B)=[0x21回声][joint][mode][ang][sw_lo][sw_hi][hw]
 *   hw = PCA OFF 寄存器回读 vs ang_to_off(jt_ang): 0=一致 / 1=不一致 / 2=I²C 读失败
 *   (软件态与硬件真值分离上报: V+ 反接/I²C 线松时 jt_ang 仍会变, hw 才说真话)
 * CMD 0x22 多关节同步 (v1.3): 载荷=[n][n×(joint,ang_hi,ang_lo)], n≤21,
 *   全帧校验后连续施加 (关节间不插协议轮, 防姿态抽动); ACK 载荷=[0x22回声][applied][status]
 *   status: 0=全部写入 PCA / 1=含非法关节(整帧丢弃不施加) / 2=至少一路 I²C 失败
 * CMD 0x23 序列回放 (v1.5): 一帧上传整段编排, S3 存下后阻塞式自播, 播放期零发帧。
 *   载荷=[loops][steps][steps×(j0,j1,j2,j3 角度=整度, dur=100ms 单位)], steps≤12 (载荷≤64)。
 *   每步四关节各以一次性 glide (同 0x20 bit15) 并行爬向目标角, dur 期间保活; loops 遍后
 *   四关节静态自锁并返回主循环。ACK 载荷=[0x23回声][steps][loops][总秒(截断)]。
 *   动因: 位啮链路撑不住 2min 连续发帧 (36 帧背靠背实测丢 58%), 且同时最多可靠建 ~3 路
 *   周期扫描; 序列回放把编排搬进 S3 自播, 播放期主动失聪 (不进 RX 校准→不会 desync),
 *   一帧上传即绕开两条硬上限 (2026-09-16 2min 拟人联动方案)。
 * 其余 CMD 同 l1 (0x01/0x02/0x10/0x11/0x12, ACK 0x06 / NACK 0x15 / 坏帧 0x16)
 * 扫描任务: jt_lo<jt_hi 的关节 20ms 节拍 0.2° 步进三角扫, 等待窗与兜底自治环内保活
 */
#define UART0_FIFO   (*(volatile unsigned int *)0x60000000)
#define UART0_STATUS (*(volatile unsigned int *)0x6000001C)

#define RTC_CNTL_WDTWPROTECT (*(volatile unsigned int *)0x600080B0)
#define RTC_CNTL_WDTCONFIG0  (*(volatile unsigned int *)0x60008098)
#define RTC_CNTL_WDTFEED     (*(volatile unsigned int *)0x600080AC)
#define RTC_CNTL_SWD_WPROT   (*(volatile unsigned int *)0x600080B8)
#define RTC_CNTL_SWD_CONF    (*(volatile unsigned int *)0x600080B4)
#define TIMG0_WDTWPROTECT (*(volatile unsigned int *)0x6001F064)
#define TIMG0_WDTCONFIG0  (*(volatile unsigned int *)0x6001F048)
#define TIMG0_INT_CLR     (*(volatile unsigned int *)0x6001F07C)

#define R(a) (*(volatile unsigned int *)(a))
#define GPIO_BASE  0x60004000u
#define GPIO_IN_ADDR       (volatile unsigned int *)(GPIO_BASE + 0x3C)
#define GPIO_OUT_W1TS      R(GPIO_BASE + 0x8)
#define GPIO_OUT_W1TC      R(GPIO_BASE + 0xC)
#define GPIO_ENABLE_W1TS   R(GPIO_BASE + 0x24)
#define GPIO_ENABLE_W1TC   R(GPIO_BASE + 0x28)
#define IOMUX_PIN(p) R(0x60009000u + 4 + (p) * 4)

#define TX4_HI() (GPIO_OUT_W1TS = (1u << 4))
#define TX4_LO() (GPIO_OUT_W1TC = (1u << 4))

#define CMD_ACK  0x06u
#define CMD_NACK 0x15u

/* ==== SAR ADC1 (yl69.c 实证资产; 寄存器逐项对照 IDF v5.2 sens_reg.h) ==== */
#define SENS_BASE           0x60008800u
#define SENS_MEAS1_CTRL1    (*(volatile unsigned int *)(SENS_BASE + 0x08))
#define SENS_MEAS1_CTRL2    (*(volatile unsigned int *)(SENS_BASE + 0x0C))
#define SENS_MEAS1_MUX      (*(volatile unsigned int *)(SENS_BASE + 0x10))
#define SENS_ATTEN1         (*(volatile unsigned int *)(SENS_BASE + 0x14))
#define SENS_POWER_XPD_SAR  (*(volatile unsigned int *)(SENS_BASE + 0x3C))
#define SENS_SLAVE_ADDR1    (*(volatile unsigned int *)(SENS_BASE + 0x40))
#define SENS_PERI_CLK_GATE  (*(volatile unsigned int *)(SENS_BASE + 0x104))
#define SENS_PERI_RESET     (*(volatile unsigned int *)(SENS_BASE + 0x108))
#define SARADC_BASE         0x60040000u
#define SARADC_CTRL         (*(volatile unsigned int *)(SARADC_BASE + 0x00))
#define SARADC_CTRL2        (*(volatile unsigned int *)(SARADC_BASE + 0x04))
#define ADC_CH 5u   /* GPIO6 = ADC1_CH5 = 0.1Ω 低侧电流感测 (sense 经 RC 1k+100nF 入脚) */

/* ==== PCA9685 (L1.5 多关节后端) ==== */
#define SDA 8u
#define SCL 9u
#define PCA_ADDR 0x40u
#define PCA_MODE1 0x00u
#define PCA_PRESCALE 0xFEu
#define PCA_CH0_ON_L 0x06u

/* ==== RMT (rmt_fixed.c 胜出配置) ==== */
#define SYSTEM_PERIP_CLK_EN0 (*(volatile unsigned int *)0x600C0018)
#define RMT_BASE   0x60016000u
#define RMT_RAM_CH0   0x60016800u
#define RMT_CH0CONF0  R(RMT_BASE + 0x20)
/* S3 偏移按厂商 reg 头文件 (rmt_reg.h): STATUS=0x50 INT_RAW=0x70
 * INT_ENA=0x78 INT_CLR=0x7C — 之前误用 C3 布局是 RMT 假死根因 */
#define RMT_INT_RAW   R(RMT_BASE + 0x70)
#define RMT_INT_ENA   R(RMT_BASE + 0x78)
#define RMT_INT_CLR   R(RMT_BASE + 0x7C)
#define RMT_SYS_CONF  R(RMT_BASE + 0xC0)
/* CH0CONF0 严格按厂商头文件 rmt_chnconf0 (ESP32-S3 TX 通道):
 * div_cnt[15:8]=2, mem_size[19:16]=1块 (写错到[31:28]是状态机卡死根因!),
 * idle_out_en[6]=1, carrier_en[21]=0(默认是1必须显式关), conf_update=bit24
 * 时钟链: XTAL 40M ÷ (div_num+1=2) ÷ (div_cnt+1=3) ≈ 6.67MHz... 不对 —
 * sclk_div_num=0 → sclk=40M, div_cnt=2 → 13.3MHz, tick=75ns */
#define BASE_CONF ((2u << 8) | (1u << 16) | (1u << 6))

static unsigned int ws_t0h = 4, ws_t0l = 8, ws_t1h = 8, ws_t1l = 4, ws_gap = 4000;

/* ==== 关节表 (L1.5 颈: 四关节自下而上, jt_ch = PCA 通道重映射) ====
 * as-built 2026-09-15: 物理插线左→右 = pan/tilt/nod/roll = CH0/1/2/3
 *   (roll 插头实测在 CH3: CH4 回读 hw=一致但头板不动, 改 CH3 验证);
 * 协议关节号 0..3 = 颈 J1..J4, 通道错位由 jt_ch 吸收, 上位机不再见 CH 号。 */
#define JT_COUNT 4u
static const unsigned char jt_ch[4] = {0u, 1u, 2u, 3u};
static unsigned int jt_ang[4] = {900, 900, 900, 900};  /* 上电中位 */
static unsigned int jt_lo[4], jt_hi[4];   /* 每关节扫描区间, lo<hi 时扫描活 */
static unsigned char jt_dir[4];           /* 每关节扫描方向 */
static unsigned char jt_os[4];            /* 每关节一次性扫描: 到目标端点自动停扫 */
static unsigned int jt_bitd = 0;          /* 最近一次校准位啜, 任务节拍基准 */

/* ==== 0x23 序列回放状态 (一帧上传整段编排, S3 阻塞式自播) ====
 * seq_prog 布局 = [loops][steps][steps×(j0,j1,j2,j3 角度=整度, dur=100ms 单位)],
 * 2 + steps*5 ≤ 64 载荷上限 → steps ≤ 12。seq_playing=1 时 joints_sweep_tick 跳过
 * RX 空闲门 (播放期 S3 不监听、无帧可撞), 四关节并行 glide 直驱 I²C 不被让路窗拖慢。 */
#define SEQ_MAX_STEPS 12u
static unsigned char seq_prog[2 + SEQ_MAX_STEPS * 5];
static unsigned int seq_steps = 0, seq_loops = 0;
static unsigned char seq_playing = 0;

static void uart_putc(char c)
{
    for (volatile int w = 0; w < 100000; w++) {
        if (((UART0_STATUS >> 16) & 0x3FF) < 127) break;
    }
    UART0_FIFO = (unsigned int)c;
}
static void uart_puts(const char *s) { while (*s) uart_putc(*s++); }
static void put_dec(unsigned int v)
{
    char b[12]; int n = 0;
    if (v == 0) { uart_putc('0'); return; }
    while (v) { b[n++] = '0' + v % 10; v /= 10; }
    while (n--) uart_putc(b[n]);
}
static void put_hex2(unsigned int v)
{
    const char *d = "0123456789ABCDEF";
    uart_putc(d[(v >> 4) & 15]); uart_putc(d[v & 15]);
}
static void delay_ms(unsigned int ms)
{
    volatile unsigned int n = ms * 16000u;
    while (n--) {}
}
static void delay_us(unsigned int us)
{
    volatile unsigned int n = us * 16u;
    while (n--) {}
}

/* ==== ADC1 初始化 (照搬 yl69.c 实证序列, 只增 IOMUX 模拟输入配置) ==== */
static void adc_init(void)
{
    /* GPIO6: FUN_IE 开输入, MCU_SEL=1(RTC 功能) → ADC 模拟通路 */
    IOMUX_PIN(6) = (1u << 12) | (1u << 3);
    /* SENS 内部 SARADC 寄存器时钟门 + FSM 复位脉冲 */
    SENS_PERI_CLK_GATE |= (1u << 30);
    SENS_PERI_RESET |= (1u << 30);
    SENS_PERI_RESET &= ~(1u << 30);
    /* 关内部 amp */
    SENS_MEAS1_CTRL1 |= (3u << 30) | (3u << 28) | (3u << 26) | (2u << 24);
    /* 模拟上电: force_xpd_sar=3(常开) + sarclk_en */
    SENS_POWER_XPD_SAR |= (3u << 29) | (1u << 31);
    /* RTC 控制器软件接管 */
    SENS_MEAS1_MUX &= ~(1u << 31);
    SENS_MEAS1_CTRL2 |= (1u << 18) | (1u << 31);
    /* 通道 CH5 = 0.1Ω 低侧电流感测: 衰减 0dB (atten=0, ~0-0.75V 档),
     * 对 0~300mV 的采样信号分辨率最佳 (11dB 全量程档在 mV 级噪声大不可用) */
    SENS_ATTEN1 &= ~(3u << (ADC_CH * 2));
    SENS_MEAS1_CTRL2 = (SENS_MEAS1_CTRL2 & ~(0xFFFu << 19))
                     | ((1u << ADC_CH) << 19);
    /* 数字侧默认值 */
    SARADC_CTRL |= (1u << 0);
    SARADC_CTRL &= ~(1u << 1);
    SARADC_CTRL2 |= (1u << 11);
    SARADC_CTRL2 &= ~(1u << 24);
}
/* oneshot 采样: 返回 16bit raw; 0xFFFFFFFF = 超时 */
static unsigned int adc1_read(void)
{
    unsigned int w = 0;
    while ((SENS_SLAVE_ADDR1 >> 22) & 0xFFu) { if (++w > 100000u) break; }
    SENS_MEAS1_CTRL2 = (SENS_MEAS1_CTRL2 & ~(1u << 17));
    delay_us(20);
    SENS_MEAS1_CTRL2 |= (1u << 17);
    unsigned int t = 0;
    while (!(SENS_MEAS1_CTRL2 & (1u << 16))) {
        if (++t > 100000u) return 0xFFFFFFFFu;
        delay_us(1);
    }
    return SENS_MEAS1_CTRL2 & 0xFFFFu;
}
static void put_hex(unsigned int v)
{
    const char *d = "0123456789ABCDEF";
    uart_puts("0x");
    for (int i = 28; i >= 0; i -= 4) uart_putc(d[(v >> i) & 15]);
}

/* ==== 规范环 (hello8 v11 实证结构) ==== */
static unsigned int asm_ticks(volatile unsigned int *ain, unsigned int n,
                              unsigned int men, unsigned int xorv)
{
    unsigned int s = n, v, lvl, x, h;
    __asm__ volatile (
        "1:\n\t"
        "l32i %[v], %[a], 0\n\t"
        "extui %[lvl], %[v], 5, 1\n\t"
        "xor %[x], %[lvl], %[xv]\n\t"
        "and %[h], %[x], %[m]\n\t"
        "bnez %[h], 2f\n\t"
        "addi %[s], %[s], -1\n\t"
        "bnez %[s], 1b\n\t"
        "2:\n"
        : [s] "+r"(s), [v] "=&r"(v), [lvl] "=&r"(lvl),
          [x] "=&r"(x), [h] "=&r"(h)
        : [a] "r"(ain), [xv] "r"(xorv), [m] "r"(men)
        : "memory");
    return n - s;
}
static void asm_delay(volatile unsigned int *ain, unsigned int n)
{
    asm_ticks(ain, n, 0u, 0u);
}
static unsigned int asm_wait_fall(volatile unsigned int *ain, unsigned int to)
{
    return asm_ticks(ain, to, 1u, 1u);
}
/* 前向声明: 定义在舵机区 (LEDC 之后), read_frame/主循环先行引用 */
static void servo_task_tick(unsigned int delta_ticks);
/* 带任务回调的等降沿窗口: L1 周期任务节拍源 (等待不白等,
 * 分块跑规范环, 每块结束回调一次传入已耗 ticks —— 等待窗内扫描任务照常活着。
 * 注: 不用 inline asm 内 call —— 被调函数会破坏 asm 输出寄存器分配) */
static unsigned int asm_wait_fall_cb(volatile unsigned int *ain, unsigned int to,
                                     void (*cb)(unsigned int))
{
    unsigned int elapsed = 0;
    const unsigned int chunk = 32768u;
    while (to > elapsed + chunk) {
        unsigned int t = asm_ticks(ain, chunk, 1u, 1u);
        elapsed += t;
        if (t < chunk) return elapsed;   /* 块内已见降沿 */
        cb(t);
    }
    return elapsed + asm_ticks(ain, to - elapsed, 1u, 1u);
}
static unsigned int asm_wait_rise(volatile unsigned int *ain, unsigned int to)
{
    return asm_ticks(ain, to, 1u, 0u);
}
static unsigned int rd5(volatile unsigned int *ain)
{
    unsigned int v;
    __asm__ volatile ("l32i %[v], %[a], 0\n\t" : [v] "=r"(v) : [a] "r"(ain));
    return (v >> 5) & 1u;
}
static unsigned int rx_byte(volatile unsigned int *ain, unsigned int bitd)
{
    unsigned int byte = 0;
    asm_delay(ain, bitd + bitd / 2u);
    if (rd5(ain)) byte |= 1u;
    for (unsigned int b = 1; b < 8u; b++) {
        asm_delay(ain, bitd);
        if (rd5(ain)) byte |= (1u << b);
    }
    return byte;
}
static void tx_byte_body(volatile unsigned int *ain, unsigned int v,
                         unsigned int bitd)
{
    for (unsigned int b = 0; b < 8u; b++) {
        asm_delay(ain, bitd);
        if ((v >> b) & 1u) GPIO_OUT_W1TS = (1u << 4);
        else GPIO_OUT_W1TC = (1u << 4);
    }
    asm_delay(ain, bitd);
    GPIO_OUT_W1TS = (1u << 4);   /* 停止位高 */
}
static void tx_byte(volatile unsigned int *ain, unsigned int v,
                    unsigned int bitd)
{
    TX4_LO();
    tx_byte_body(ain, v, bitd);
    asm_delay(ain, bitd);
}
static void drain_burst(unsigned int bitd)
{
    for (;;) {
        if (asm_wait_fall(GPIO_IN_ADDR, bitd * 3u) >= bitd * 3u) return;
        asm_wait_rise(GPIO_IN_ADDR, bitd * 14u);
    }
}
static unsigned int read_frame(unsigned char *f, unsigned int bitd)
{
    if (asm_wait_fall_cb(GPIO_IN_ADDR, 15000000u, servo_task_tick) >= 15000000u) return 2;
    f[0] = (unsigned char)rx_byte(GPIO_IN_ADDR, bitd);
    asm_wait_rise(GPIO_IN_ADDR, bitd * 4u);
    if (asm_wait_fall_cb(GPIO_IN_ADDR, bitd * 6u, servo_task_tick) >= bitd * 6u) return 3;
    f[1] = (unsigned char)rx_byte(GPIO_IN_ADDR, bitd);
    asm_wait_rise(GPIO_IN_ADDR, bitd * 4u);
    if (asm_wait_fall_cb(GPIO_IN_ADDR, bitd * 6u, servo_task_tick) >= bitd * 6u) return 3;
    f[2] = (unsigned char)rx_byte(GPIO_IN_ADDR, bitd);
    asm_wait_rise(GPIO_IN_ADDR, bitd * 4u);
    unsigned int len = f[2];
    if (len > 64u) len = 64u;
    unsigned int total = 4u + len + 1u;
    for (unsigned int i = 3; i < total; i++) {
        if (asm_wait_fall_cb(GPIO_IN_ADDR, bitd * 6u, servo_task_tick) >= bitd * 6u) return 3;
        f[i] = (unsigned char)rx_byte(GPIO_IN_ADDR, bitd);
        asm_wait_rise(GPIO_IN_ADDR, bitd * 4u);
    }
    return 1;
}
static unsigned char crc8(const unsigned char *p, unsigned int n)
{
    unsigned char c = 0;
    for (unsigned int i = 0; i < n; i++) {
        c ^= p[i];
        for (int k = 0; k < 8; k++)
            c = (unsigned char)((c & 0x80u) ? ((c << 1) ^ 0x07u) : (c << 1));
    }
    return c;
}

/* ==== WS2812 RMT 发射 ==== */
static void rmt_tx_start(void)
{
    RMT_INT_CLR = 1;
    RMT_CH0CONF0 = BASE_CONF | (1u << 1) | (1u << 2);   /* mem_rd_rst + apb_mem_rst (WT自清) */
    RMT_CH0CONF0 = BASE_CONF;
    RMT_CH0CONF0 = BASE_CONF | (1u << 24);              /* conf_update */
    RMT_CH0CONF0 = BASE_CONF | (1u << 0);               /* tx_start */
}
/* UART 锚定测帧长: 每 86.8us 查一次 TX_END, 返回帧占几个锚 */
static int measure_anchors(void)
{
    volatile unsigned int *ram = (volatile unsigned int *)RMT_RAM_CH0;
    for (int i = 0; i < 24; i++) ram[i] = 4u | (1u << 15) | (8u << 16);
    ram[24] = 500;
    ram[25] = 0;
    rmt_tx_start();
    for (int k = 0; k < 30; k++) {
        uart_putc('~');
        while (((UART0_STATUS >> 16) & 0x3FF) != 0) { }
        if (RMT_INT_RAW & 1u) return k + 1;
    }
    return -1;
}
static void rmt_init(void)
{
    SYSTEM_PERIP_CLK_EN0 |= (1u << 9);
    /* 厂商头文件实证布局: bit0=apb_fifo_mask(直访RAM), bit1=mem_clk_force_on,
     * bit3=mem_force_pu, sclk_div_num[11:4]=1, sclk_sel[25:24]=3(XTAL,
     * 默认1=80M 老板 ROM 已供钟所以老配置能活; 新板 80M 疑似死→XTAL),
     * bit26=sclk_active, bit31=clk_en */
    /* sclk_div_num[11:4]=0 → sclk=XTAL 40M; 通道 div_cnt=2 → 13.3MHz */
    RMT_SYS_CONF = (1u << 0) | (1u << 1) | (1u << 3)
                 | (3u << 24) | (1u << 26) | (1u << 31);
    RMT_CH0CONF0 = BASE_CONF;
    RMT_CH0CONF0 |= (1u << 24);             /* conf_update */
    RMT_INT_ENA |= 1;
    /* 诊断: 回读验证写入生效 */
    uart_puts("[L1] sysconf="); put_hex(R(RMT_BASE + 0xC0));
    uart_puts(" inten="); put_hex(R(RMT_BASE + 0x78)); uart_puts("\r\n");
    /* GPIO48: IOMUX 选 GPIO + 最强驱动; 矩阵输出 <- RMT_CH0_TX (sig 81) */
    IOMUX_PIN(48) = (1u << 12) | (3u << 10);
    R(GPIO_BASE + 0x554 + 48 * 4) = 81;

    int a = measure_anchors();
    uart_puts("[L1] anchors="); put_dec((unsigned int)a); uart_puts("\r\n");
    if (a < 1) a = 1;
    /* 锚定测频: 长帧 21600 ticks 占几个 86.8us 锚 (首测竞态丢弃, 取第二次) */
    for (int pass = 0; pass < 2; pass++) {
        volatile unsigned int *ram = (volatile unsigned int *)RMT_RAM_CH0;
        for (int i = 0; i < 44; i++)
            ram[i] = 200u | (1u << 15) | (200u << 16);
        ram[44] = 4000;
        ram[45] = 0;
        rmt_tx_start();
        int a2 = -1;
        for (int k = 0; k < 60; k++) {
            uart_putc('~');
            while (((UART0_STATUS >> 16) & 0x3FF) != 0) { }
            if (RMT_INT_RAW & 1u) { a2 = k + 1; break; }
        }
        if (pass == 1 && a2 >= 1) {
            unsigned int mhz10 = (21600u * 100u) / ((unsigned int)a2 * 868u);
            uart_puts("[L1] sclk_mhz_x10="); put_dec(mhz10); uart_puts("\r\n");
            /* 胜出配比 4/8/8/4 基准 11.7MHz(scale10=117): 按实测缩放 */
            unsigned int scale10 = mhz10;
            if (scale10 < 10) scale10 = 10;
            if (scale10 > 250) scale10 = 250;
            ws_t0h = (4 * scale10 + 58) / 117;
            ws_t0l = (8 * scale10 + 58) / 117;
            ws_t1h = (8 * scale10 + 58) / 117;
            ws_t1l = (4 * scale10 + 58) / 117;
            ws_gap = (3500 * scale10 + 58) / 117;
            if (ws_gap > 15000) ws_gap = 15000;
        }
    }
    uart_puts("[L1] t0h="); put_dec(ws_t0h);
    uart_puts(" t1h="); put_dec(ws_t1h);
    uart_puts(" gap="); put_dec(ws_gap); uart_puts("\r\n");
}
static unsigned int ws_show(unsigned int grb)
{
    volatile unsigned int *ram = (volatile unsigned int *)RMT_RAM_CH0;
    for (int i = 0; i < 24; i++) {
        unsigned int hi = ws_t0h, lo = ws_t0l;
        if ((grb >> (23 - i)) & 1) { hi = ws_t1h; lo = ws_t1l; }
        ram[i] = hi | (1u << 15) | (lo << 16);
    }
    ram[24] = ws_gap;
    ram[25] = 0;
    R(RMT_BASE + 0xC8) = 1u;    /* REF_CNT_RST bit0: ch0 分频器复位 */
    rmt_tx_start();
    unsigned int guard = 2000000;
    unsigned int hit = 0;
    while (guard--) {
        if (RMT_INT_RAW & 1u) { hit = 1; break; }
    }
    if (!hit) {   /* 诊断: 超时 dump 状态机 */
        uart_puts("[L1] st50="); put_hex(R(RMT_BASE + 0x50));
        uart_puts(" conf="); put_hex(RMT_CH0CONF0);
        uart_puts(" raw="); put_hex(RMT_INT_RAW); uart_puts("\r\n");
    }
    return hit ? (2000000u - guard) : 2000000u;   /* 诊断: 自旋消耗量 */
}
static unsigned int hsv_grb(unsigned int hue, unsigned int val)
{
    unsigned int r = 0, g = 0, b = 0, p = hue & 255u;
    if (p < 85u) { r = 255 - p * 3; b = p * 3; }
    else if (p < 170u) { p -= 85u; g = p * 3; b = 255 - p * 3; }
    else { p -= 170u; r = p * 3; g = 255 - p * 3; }
    r = r * val / 255u; g = g * val / 255u; b = b * val / 255u;
    return (g << 16) | (r << 8) | b;
}

/* ==== PCA9685 I²C 位啮后端 (L1.5: 准开漏, 板载上拉) ==== */
static void i2c_d(void) { delay_us(5); }
static void sda_lo(void) { GPIO_OUT_W1TC = (1u << SDA); GPIO_ENABLE_W1TS = (1u << SDA); }
static void sda_hi(void) { GPIO_ENABLE_W1TC = (1u << SDA); }
static void scl_lo(void) { GPIO_OUT_W1TC = (1u << SCL); GPIO_ENABLE_W1TS = (1u << SCL); }
static void scl_hi(void) { GPIO_ENABLE_W1TC = (1u << SCL); }
static unsigned int sda_rd(void) { return (R(GPIO_BASE + 0x3C) >> SDA) & 1u; }
static void i2c_init(void)
{
    IOMUX_PIN(SDA) = (1u << 12) | (3u << 10) | (1u << 9);
    IOMUX_PIN(SCL) = (1u << 12) | (3u << 10) | (1u << 9);
    GPIO_OUT_W1TS = (1u << SDA) | (1u << SCL);
    GPIO_ENABLE_W1TC = (1u << SDA) | (1u << SCL);
}
static void i2c_start(void)
{ sda_hi(); i2c_d(); scl_hi(); i2c_d(); sda_lo(); i2c_d(); scl_lo(); i2c_d(); }
static void i2c_stop(void)
{ sda_lo(); i2c_d(); scl_hi(); i2c_d(); sda_hi(); i2c_d(); }
static unsigned int i2c_wbyte(unsigned int v)
{
    for (int b = 7; b >= 0; b--) {
        if ((v >> b) & 1u) sda_hi(); else sda_lo();
        i2c_d(); scl_hi(); i2c_d(); scl_lo(); i2c_d();
    }
    sda_hi(); i2c_d(); scl_hi(); i2c_d();
    unsigned int ack = sda_rd() ? 0u : 1u;
    scl_lo(); i2c_d();
    return ack;
}
static unsigned int i2c_rbyte(unsigned int ack)
{
    unsigned int v = 0;
    sda_hi();
    for (int b = 7; b >= 0; b--) {
        scl_hi(); i2c_d();
        if (sda_rd()) v |= (1u << b);
        scl_lo(); i2c_d();
    }
    if (ack) sda_lo(); else sda_hi();
    i2c_d(); scl_hi(); i2c_d(); scl_lo(); i2c_d();
    sda_hi();
    return v;
}
static unsigned int pca_w(unsigned int reg, unsigned int val)
{
    i2c_start();
    unsigned int ok = i2c_wbyte(PCA_ADDR << 1);
    ok &= i2c_wbyte(reg);
    ok &= i2c_wbyte(val);
    i2c_stop();
    return ok;
}
static unsigned int pca_r(unsigned int reg)
{
    i2c_start();
    unsigned int ok = i2c_wbyte(PCA_ADDR << 1);
    ok &= i2c_wbyte(reg);
    i2c_start();
    ok &= i2c_wbyte((PCA_ADDR << 1) | 1u);
    unsigned int v = i2c_rbyte(0);
    i2c_stop();
    return ok ? v : 0xFFFFFFFFu;
}
/* 角度→OFF 刻度: ang(0..1800, 0.1°) → 1.0..2.0ms @ 50Hz → 4096 刻度 */
static unsigned int ang_to_off(unsigned int ang)
{
    if (ang > 1800u) ang = 1800u;
    unsigned int px10 = 10000u + (ang * 50u) / 9u;
    return (px10 * 512u) / 25000u;
}
/* 单事务 AI 写: ON_L/ON_H/OFF_L/OFF_H 一笔完成 (MODE1 AI 已在 pca_init 置位)。
 * 扫描步进的 I²C 盲窗由此 4 笔≈1.4ms 缩到 1 笔≈0.8ms, 撞帧概率减半。 */
static unsigned int pca_set(unsigned int ch, unsigned int ang)
{
    if (ch > 15u) return 0;
    unsigned int off = ang_to_off(ang);
    unsigned int base = PCA_CH0_ON_L + 4u * ch;
    i2c_start();
    unsigned int ok = i2c_wbyte(PCA_ADDR << 1);
    ok &= i2c_wbyte(base);
    ok &= i2c_wbyte(0x00u);
    ok &= i2c_wbyte(0x00u);
    ok &= i2c_wbyte(off & 0xFFu);
    ok &= i2c_wbyte((off >> 8) & 0xFFu);
    i2c_stop();
    return ok;
}
/* RX 空闲门: 链路 RX(GPIO5, 与 asm_ticks extui bit5 同脚) 持续高 us 微秒才返回 1。
 * 扫描步进的 I²C 事务必须落在 RX 静默期: 帧起始沿撞进事务 → 该帧 st=3,
 * 扫描连续活跃时每帧都撞 = 链路永久失聪 (2026-09-15 实机实证)。
 * 门后仍有 ≈0.8ms 盲窗 → 撞帧 ≈4%/帧, 单帧丢弃可自恢复, 不再失聪。 */
static unsigned int rx_idle_us(unsigned int us)
{
    for (unsigned int t = 0; t < us; t += 50u) {
        if (!((R(GPIO_BASE + 0x3Cu) >> 5) & 1u)) return 0u;
        delay_us(50);
    }
    return 1u;
}
static void pca_init(void)
{
    i2c_init();
    uart_puts("[L15] pca scan:");
    unsigned int hits = 0;
    for (unsigned int a = 0x08u; a < 0x78u; a++) {
        i2c_start();
        if (i2c_wbyte(a << 1)) { uart_putc(32); put_hex2(a); hits++; }
        i2c_stop();
    }
    uart_puts("\r\n");
    if (!hits) { uart_puts("[L15] PCA ABSENT - joints dead\r\n"); return; }
    pca_w(PCA_MODE1, 0x10u);      /* sleep */
    pca_w(PCA_PRESCALE, 0x79u);   /* 50Hz */
    pca_w(PCA_MODE1, 0x00u);
    delay_ms(5);
    pca_w(PCA_MODE1, 0xA0u);      /* AI + RESTART */
    unsigned int pre = pca_r(PCA_PRESCALE);
    uart_puts("[L15] pre rb="); put_hex2(pre); uart_puts("\r\n");
    for (unsigned int j = 0; j < JT_COUNT; j++) pca_set(jt_ch[j], jt_ang[j]);
    uart_puts("[L15] joints at 900 hold\r\n");
}
/* 周期扫描任务 (L1.5): 三角波步进 1.0° (=10 LSB, 远超死区防走针)。
 * 节拍源 = servo_task_tick(bitd): 主循环自治扫描 2000*bitd(≈200ms) 慢拍保链路;
 * 速度史: 0.5°/拍=2.5°/s 实测太慢 → 步进改 1.0°; 但主循环快拍 (192*bitd)
 * +周期扫描实证链路全聋 (2026-09-30, rx_idle 门救不了) → 主循环保持慢拍。
 * 丝滑快摆走 0x23 序列回放: seq_play 硬编码 192*bitd 免门直驱 (播放期 S3
 * 主动失聪无帧可撞), 1.0°/拍 × 20ms = 50°/s (daemon `shake` 命令, 140° 单程 2.8s)。
 * 兜底自治环内以 delay_ms 节拍直驱, Pi 掉线不停关节。 */
static void joints_sweep_tick(void)
{
    /* 无活跃扫描: 零成本返回 —— 门控采样绝不进 RX 等待窗 (同上坑的另一半) */
    unsigned int active = 0;
    for (unsigned int j = 0; j < JT_COUNT; j++)
        if (jt_lo[j] < jt_hi[j]) { active = 1u; break; }
    if (!active) return;
    /* RX 不静默则本拍整拍让路 (等待窗内绝不动 I²C); 下拍再试, 节拍自跟随。
     * 序列回放期 (seq_playing) S3 主动失聪不监听 RX, 无帧可撞 → 免门直驱, 四关节并行平滑。 */
    if (!seq_playing && !rx_idle_us(2500u)) return;
    for (unsigned int j = 0; j < JT_COUNT; j++) {
        if (!(jt_lo[j] < jt_hi[j])) continue;
        if (jt_dir[j] == 0) {
            if (jt_ang[j] + 10u >= jt_hi[j]) {
                jt_ang[j] = jt_hi[j];
                if (jt_os[j]) { jt_os[j] = 0u; jt_lo[j] = jt_hi[j]; }  /* glide 到位自停 */
                else jt_dir[j] = 1;
            }
            else jt_ang[j] += 10u;
        } else {
            if (jt_ang[j] <= jt_lo[j] + 10u) {
                jt_ang[j] = jt_lo[j];
                if (jt_os[j]) { jt_os[j] = 0u; jt_hi[j] = jt_lo[j]; }  /* glide 到位自停 */
                else jt_dir[j] = 0;
            }
            else jt_ang[j] -= 10u;
        }
        pca_set(jt_ch[j], jt_ang[j]);
    }
}
static void servo_task_tick(unsigned int delta_ticks)
{
    /* delta = 本次等待块已耗 ticks; 主循环自治扫描用 2000*bitd(≈200ms) 慢拍:
     * 192*bitd 快拍+周期扫描实证链路全聋 (2026-09-30), 快摆一律走 0x23 回放。 */
    static unsigned int acc = 0;
    if (jt_bitd == 0) return;
    acc += delta_ticks;
    if (acc >= 2000u * jt_bitd) { acc = 0; joints_sweep_tick(); }
}

/* ==== 0x23 序列回放: 装载一步 + 阻塞式播完整段 ====
 * 复用 joints_sweep_tick 的四关节并行一次性 glide (bit15 one-shot 到位自锁):
 * seq_load_step 把四关节各自的目标角设成 [lo,hi] 一次性扫描区间, 播放循环按 20ms
 * 节拍直驱 joints_sweep_tick (免 RX 门), dur 到点即装载下一步。 */
static void seq_load_step(unsigned int i)
{
    const unsigned char *s = &seq_prog[2 + i * 5u];
    for (unsigned int j = 0; j < JT_COUNT; j++) {
        unsigned int end = (unsigned int)s[j] * 10u;   /* 整度 → 0.1° 协议角值 */
        if (end > 1800u) end = 1800u;
        unsigned int start = jt_ang[j];                 /* 从当前位置起 (不跳变) */
        unsigned int lo = (start < end) ? start : end;
        unsigned int hi = (start < end) ? end : start;
        jt_lo[j] = lo; jt_hi[j] = hi;
        jt_os[j] = 1u;                                  /* 一次性 glide: 到位自动停扫自锁 */
        jt_dir[j] = (start < hi) ? 0u : 1u;             /* 指向区间内部 (同 0x20 glide) */
    }
}
static void seq_play(void)
{
    seq_playing = 1u;
    /* 真 20ms 时基: bitd = 一个位啜 (@9600 = 104us) 的 asm_ticks 迭代数 → 1 迭代 ≈ 1us。
     * 推导: bitd 迭代 = 1bit = 1/9600 s → 192*bitd 迭代 = 192/9600 = 0.02s = 20ms。
     * 历史坑: 曾写 2000*jt_bitd (误标 20ms, 实为 ~200ms) → 每拍 200ms 而非 20ms,
     * loops=1 冒烟实证 16s 编排实播 ~150s (探针测得 S3 直到 t≈155s 才恢复响应),
     * 且 0.5°/拍 × 稀少拍数 → 关节根本走不到目标角。192*bitd 同时修好时长与到位。
     * 播放期 seq_playing 免 RX 门 (S3 主动失聪、无帧可撞), 故可用 20ms 快拍 (25°/s,
     * 用户定案速率); 不像 servo_task_tick 自治扫描须留 200ms 慢拍压低 RX 撞帧率。 */
    unsigned int tick = 192u * jt_bitd;
    if (tick == 0u) tick = 192u * 100u;   /* 兜底: bitd 未校准时按标称 100 */
    for (unsigned int L = 0; L < seq_loops; L++) {
        for (unsigned int i = 0; i < seq_steps; i++) {
            seq_load_step(i);
            unsigned int dur = (unsigned int)seq_prog[2 + i * 5u + 4u] * 100u;  /* ×100ms */
            if (dur < 20u) dur = 20u;
            unsigned int el = 0;
            while (el < dur) { joints_sweep_tick(); asm_delay(GPIO_IN_ADDR, tick); el += 20u; }
        }
    }
    /* 收尾: 四关节静态自锁 (清扫描区间), 返回主循环后链路必落空闲期, 上位机后续帧不被撞 */
    for (unsigned int j = 0; j < JT_COUNT; j++) {
        jt_lo[j] = jt_hi[j] = jt_ang[j];
        jt_os[j] = 0u;
    }
    seq_playing = 0u;
    uart_puts("[L15] 0x23 seq done, joints static\r\n");
}

/* 链接脚本符号 (pi_build_esp.py LD_SCRIPT): .bss 区间边界 */
extern unsigned int __bss_start[];
extern unsigned int __bss_end[];

void _start(void) __attribute__((section(".entry"), naked));
void _start(void)
{
    /* .bss 清零 —— 镜像只加载 .entry/.text/.rodata/.data, ROM 不清 .bss,
     * 之前也没人清。未清零的 jt_lo/jt_hi/jt_dir 是 DRAM 残留垃圾, 会让扫描
     * 任务误判"区间活跃", 进而在 read_frame 的字节间等待窗 (asm_wait_fall_cb)
     * 内跑 pca_set —— 一次 ≈1.6ms 的 I²C 事务, 远超 6*bitd(≈620us) → 每帧必 st=3。
     * l1 踩同一个垃圾 .bss 无症状, 只因 servo_apply 是一次寄存器写。 */
    unsigned int bss_words = 0, bss_dirty = 0;
    for (unsigned int *p = __bss_start; p < __bss_end; p++) {
        if (*p) bss_dirty++;
        *p = 0;
        bss_words++;
    }

    RTC_CNTL_WDTWPROTECT = 0x50D83AA1u;
    RTC_CNTL_WDTCONFIG0 = 0;
    RTC_CNTL_WDTFEED = 1;
    RTC_CNTL_SWD_WPROT = 0x8F1D312Au;
    RTC_CNTL_SWD_CONF |= (1u << 31);
    TIMG0_WDTWPROTECT = 0x50D83AA1u;
    TIMG0_WDTCONFIG0 = 0;
    TIMG0_INT_CLR = 0xFFFFFFFFu;

    delay_ms(300);
    uart_puts("\r\n[LINKL15] mode-B link + sensor + 6-joint PCA9685 (L1.5)\r\n");
    uart_puts("[L15] bss words="); put_dec(bss_words);
    uart_puts(" dirty="); put_dec(bss_dirty);
    uart_puts(bss_dirty ? " (cleared)\r\n" : "\r\n");

    /* GPIO5 输入; GPIO4 直驱输出空闲高; GPIO10 测试输出脚 */
    GPIO_ENABLE_W1TC = (1u << 5);
    IOMUX_PIN(5) |= (1u << 9);
    IOMUX_PIN(4) |= (1u << 9);
    IOMUX_PIN(10) |= (1u << 9);
    TX4_HI();
    GPIO_ENABLE_W1TS = (1u << 4) | (1u << 10);

    rmt_init();
    adc_init();
    unsigned int adc_probe = adc1_read();
    uart_puts("[L1] adc_probe=");
    if (adc_probe == 0xFFFFFFFFu) uart_puts("TIMEOUT"); else put_dec(adc_probe);
    uart_puts("\r\n");
    ws_show(0x0000FFu);   /* 开机亮蓝: RMT+灯珠通路活体证明 */
    uart_puts("[L1] boot blue\r\n");
    pca_init();           /* L1.5: PCA 50Hz + 六关节中位上电保持 */

    static unsigned char f[80];
    unsigned int seq = 0;
    unsigned int mode = 0, hue = 0, val = 0;
    unsigned int idle_rounds = 0;
    const unsigned int AUTONOMOUS_AFTER = 5u;   /* 连续 5 轮无帧 → 自治兜底 */
    for (int round = 0; round < 20000; round++) {
        uart_puts("[L1] await burst\r\n");
        if (asm_wait_fall_cb(GPIO_IN_ADDR, 30000000u, servo_task_tick) >= 30000000u) {
            idle_rounds++;
            uart_puts("[L1] timeout "); put_dec(idle_rounds); uart_puts("\r\n");
            if (idle_rounds >= AUTONOMOUS_AFTER) {
                /* 模式 A 兜底: Pi 缺席, 本地采样→阈值决策→直驱灯珠 */
                uart_puts("[L1] pi lost -> autonomous\r\n");
                ws_show(hsv_grb(213u, 50u));   /* 紫: 降级态入场信号 */
                for (;;) {
                    unsigned int araw = adc1_read();
                    unsigned int ah;
                    if (araw == 0xFFFFFFFFu) ah = 213u;   /* ADC 故障→紫 */
                    else if (araw >= 3000u)  ah = 0u;     /* 干→红 (同 edge_daemon DRY_TH) */
                    else if (araw <= 1500u)  ah = 85u;    /* 湿→蓝 (同 WET_TH) */
                    else                     ah = 170u;   /* 中→绿 */
                    ws_show(hsv_grb(ah, 60u));
                    joints_sweep_tick();  /* 兜底自治不停关节: 扫描任务保活 */
                    uart_puts("[L1] auto raw=");
                    if (araw == 0xFFFFFFFFu) uart_puts("TIMEOUT"); else put_dec(araw);
                    uart_puts(" hue="); put_dec(ah); uart_puts("\r\n");
                    /* 短窗口探 Pi 回归 (任一 falling edge 即回链) */
                    if (asm_wait_fall(GPIO_IN_ADDR, 6000000u) < 6000000u) {
                        uart_puts("[L1] pi back -> linked\r\n");
                        idle_rounds = 0;
                        asm_wait_rise(GPIO_IN_ADDR, 5000000u);  /* 对齐高相, 避免半周期校准 */
                        break;   /* 回主循环完整重等重校 (本沿已消耗) */
                    }
                    delay_ms(400);
                }
            }
            continue;
        }
        unsigned int bitd = asm_wait_rise(GPIO_IN_ADDR, 5000000u);
        if (bitd == 0 || bitd >= 5000000u) {
            uart_puts("[L1] calib noedge\r\n");
            continue;
        }
        idle_rounds = 0;   /* Pi 在场: 清零降级计数 */
        jt_bitd = bitd;    /* L1.5: 扫描任务节拍基准随本次校准更新 */
        drain_burst(bitd);
        uart_puts("[L1] bitd="); put_dec(bitd); uart_puts("\r\n");

        unsigned int st = read_frame(f, bitd);
        if (st != 1) {
            uart_puts("[L1] frame st="); put_dec(st); uart_puts("\r\n");
            continue;
        }
        unsigned int len = f[2];
        if (len > 64u) len = 64u;
        unsigned int total = 4u + len + 1u;
        uart_puts("[L1] frame:");
        for (unsigned int i = 0; i < total; i++) {
            uart_putc(' '); put_hex2(f[i]);
        }
        uart_puts("\r\n");

        unsigned char resp;
        unsigned char rp[10];         /* 自定义 ACK 载荷 (不含回声 CMD) */
        unsigned int rplen = 0;
        unsigned int do_play = 0u;    /* 0x23: ACK 发出后再阻塞播放 (见循环尾) */
        unsigned char calc = crc8(f + 2, 2u + len);
        if (f[0] != 0xAAu || f[1] != 0x55u || calc != f[4 + len]) {
            resp = 0x16u;
            uart_puts("[L1] bad frame\r\n");
        } else {
            unsigned int cmd = f[3];
            if (cmd == 0x02u) {
                /* 传感查询: 采样 ADC1_CH5, ACK 载荷=[0x02回声][sid][raw_hi][raw_lo][status] */
                unsigned int raw = adc1_read();
                rp[0] = 1u;   /* sid=1 YL-69 土壤湿度 */
                if (raw == 0xFFFFFFFFu) {
                    rp[1] = 0xFFu; rp[2] = 0xFFu; rp[3] = 1u;
                    uart_puts("[L1] adc TIMEOUT\r\n");
                } else {
                    rp[1] = (unsigned char)(raw >> 8);
                    rp[2] = (unsigned char)(raw & 0xFFu);
                    rp[3] = 0u;
                    uart_puts("[L1] adc raw="); put_dec(raw); uart_puts("\r\n");
                }
                rplen = 4u;
                resp = CMD_ACK;
            } else if (cmd == 0x10u && len >= 3u) {
                if (f[4] != 0xFFu) hue = f[4];   /* 0xFF=彩虹继续当前相位 */
                val = f[5]; mode = f[6];
                if (mode == 1u) {
                    unsigned int v = val ? val : 120u;
                    unsigned int g = ws_show(hsv_grb(hue, v));
                    uart_puts("[L1] rainbow hue="); put_dec(hue);
                    uart_puts(" val="); put_dec(v);
                    uart_puts(" g="); put_dec(g); uart_puts("\r\n");
                } else {
                    unsigned int g = ws_show(hsv_grb(hue, val));
                    uart_puts("[L1] static hue="); put_dec(hue);
                    uart_puts(" val="); put_dec(val);
                    uart_puts(" g="); put_dec(g); uart_puts("\r\n");
                }
                resp = CMD_ACK;
            } else if (cmd == 0x11u && len >= 2u) {
                unsigned int pin = f[4], lvl = f[5];
                if (pin < 48u) {
                    if (lvl) GPIO_OUT_W1TS = (1u << pin);
                    else GPIO_OUT_W1TC = (1u << pin);
                }
                resp = CMD_ACK;
                uart_puts("[L1] gpio pin="); put_dec(pin);
                uart_puts(" lvl="); put_dec(lvl); uart_puts("\r\n");
            } else if (cmd == 0x20u && len >= 7u) {
                /* 关节设置: [joint][ang_hi][ang_lo][sw_lo_hi][sw_lo_lo][sw_hi_hi][sw_hi_lo] */
                unsigned int j = f[4];
                unsigned int ang = ((unsigned int)f[5] << 8) | f[6];
                unsigned int oneshot = (ang >> 15) & 1u;   /* bit15 = glide 一次性扫描 */
                ang &= 0x7FFFu;
                unsigned int slo = ((unsigned int)f[7] << 8) | f[8];
                unsigned int shi = ((unsigned int)f[9] << 8) | f[10];
                if (ang > 1800u) ang = 1800u;
                if (slo > 1800u) slo = 1800u;
                if (shi > 1800u) shi = 1800u;
                if (j >= JT_COUNT) {
                    rp[0] = (unsigned char)(jt_ang[0] >> 8);
                    rp[1] = (unsigned char)(jt_ang[0] & 0xFFu);
                    rp[2] = 1u;   /* 关节非法 */
                    uart_puts("[L15] bad joint="); put_dec(j); uart_puts("\r\n");
                } else {
                    jt_lo[j] = slo; jt_hi[j] = shi;
                    jt_ang[j] = ang;
                    jt_os[j] = (unsigned char)oneshot;
                    /* glide 一次性扫描: 起点必落在某一端点 (af), 方向须指向区间
                     * 内部, 否则首拍即判"到位"自停 → 整段无位移。af<shi 说明起
                     * 点在低端 (lo) 须向上爬 (dir=0); 相等则起点在高端须向下 (dir=1)。
                     * 周期扫描 (oneshot=0) 沿用旧 jt_dir, 不覆盖其三角波相位。 */
                    if (oneshot) jt_dir[j] = (ang < shi) ? 0u : 1u;
                    /* pca_set 返回值即 I²C ACK 汇总 —— 丢弃它会让 status 恒为 0,
                     * PCA 掉线/未应答时上位机误判"已生效" (2026-09-14 修正) */
                    unsigned int hw = pca_set(jt_ch[j], ang);
                    rp[0] = (unsigned char)(ang >> 8);
                    rp[1] = (unsigned char)(ang & 0xFFu);
                    rp[2] = hw ? 0u : 2u;
                    uart_puts("[L15] j"); put_dec(j);
                    uart_puts(" ang="); put_dec(ang);
                    uart_puts(" sweep="); put_dec(slo);
                    uart_puts(".."); put_dec(shi);
                    if (!hw) uart_puts(" I2C_FAIL");
                    uart_puts("\r\n");
                }
                rplen = 3u;
                resp = CMD_ACK;
            } else if (cmd == 0x21u) {
                /* 关节查询: 载荷=[joint]; ACK 载荷(9B)=[0x21回声][joint][mode][ang][sw_lo][sw_hi][hw] */
                unsigned int j = (len >= 1u) ? f[4] : 0u;
                if (j >= JT_COUNT) j = 0u;
                /* 硬件真值回读: PCA OFF_L/OFF_H vs 期望刻度。软件态 jt_ang 在 I²C
                 * 失败时照样更新, 只有寄存器回读能证明脉冲真的在发。约 0.8ms
                 * (2 笔事务), 发生在 read_frame 收全之后, 不影响位啜接收窗。 */
                unsigned int obase = PCA_CH0_ON_L + 4u * jt_ch[j];
                unsigned int offh = pca_r(obase + 3u);
                unsigned int offl = pca_r(obase + 2u);
                unsigned int hv;
                if (offh == 0xFFFFFFFFu || offl == 0xFFFFFFFFu) {
                    hv = 2u;                       /* I²C 读失败 */
                } else {
                    /* OFF_H bit4 = 强制全关标志, 落在 0x1F 掩码内 → 与期望值必然
                     * 不等, 正确报 1 (通道确实没在出脉冲) */
                    unsigned int got = ((offh & 0x1Fu) << 8) | (offl & 0xFFu);
                    hv = (got == ang_to_off(jt_ang[j])) ? 0u : 1u;
                }
                rp[0] = (unsigned char)j;
                rp[1] = (jt_lo[j] < jt_hi[j]) ? 1u : 0u;
                rp[2] = (unsigned char)(jt_ang[j] >> 8);
                rp[3] = (unsigned char)(jt_ang[j] & 0xFFu);
                rp[4] = (unsigned char)(jt_lo[j] >> 8);
                rp[5] = (unsigned char)(jt_lo[j] & 0xFFu);
                rp[6] = (unsigned char)(jt_hi[j] >> 8);
                rp[7] = (unsigned char)(jt_hi[j] & 0xFFu);
                rp[8] = (unsigned char)hv;
                rplen = 9u;
                resp = CMD_ACK;
                uart_puts("[L15] qry j"); put_dec(j);
                uart_puts(" ang="); put_dec(jt_ang[j]);
                uart_puts(" mode="); put_dec(rp[1]);
                uart_puts(" hw="); put_dec(hv); uart_puts("\r\n");
            } else if (cmd == 0x22u && len >= 1u) {
                /* 多关节同步: [n][n×(joint,ang_hi,ang_lo)] 全帧校验后连续施加 */
                unsigned int n = f[4];
                unsigned int ok_fmt = (n <= 21u && 1u + n * 3u <= len);
                unsigned int bad = 0;
                for (unsigned int i = 0; ok_fmt && i < n; i++)
                    if (f[5 + i * 3u] >= JT_COUNT) bad = 1;
                if (!ok_fmt) {
                    resp = CMD_NACK;
                    uart_puts("[L15] 0x22 bad fmt\r\n");
                } else if (bad) {
                    rp[0] = 0u; rp[1] = 1u; rplen = 2u; resp = CMD_ACK;
                    uart_puts("[L15] 0x22 bad joint, dropped\r\n");
                } else {
                    unsigned int hw = 1u;
                    for (unsigned int i = 0; i < n; i++) {
                        unsigned int j = f[5 + i * 3u];
                        unsigned int ang = ((unsigned int)f[6 + i * 3u] << 8)
                                         | f[7 + i * 3u];
                        if (ang > 1800u) ang = 1800u;
                        jt_ang[j] = ang;
                        hw &= pca_set(jt_ch[j], ang);   /* 任一路未 ACK → status=2 */
                    }
                    rp[0] = (unsigned char)n; rp[1] = hw ? 0u : 2u;
                    rplen = 2u; resp = CMD_ACK;
                    uart_puts("[L15] 0x22 applied="); put_dec(n);
                    if (!hw) uart_puts(" I2C_FAIL");
                    uart_puts("\r\n");
                }
            } else if (cmd == 0x23u && len >= 2u) {
                /* 序列回放: [loops][steps][steps×(j0..j3 整度, dur 100ms)] 一帧上传整段编排 */
                unsigned int loops = f[4];
                unsigned int steps = f[5];
                if (steps < 1u || steps > SEQ_MAX_STEPS || loops < 1u
                    || 2u + steps * 5u > len) {
                    resp = CMD_NACK;
                    uart_puts("[L15] 0x23 bad fmt steps="); put_dec(steps);
                    uart_puts(" loops="); put_dec(loops);
                    uart_puts(" len="); put_dec(len); uart_puts("\r\n");
                } else {
                    seq_loops = loops; seq_steps = steps;
                    for (unsigned int i = 0; i < 2u + steps * 5u; i++) seq_prog[i] = f[4 + i];
                    unsigned int per = 0;   /* 单遍时长 (100ms 单位) */
                    for (unsigned int i = 0; i < steps; i++)
                        per += (unsigned int)seq_prog[2 + i * 5u + 4u];
                    unsigned int secs = (per * loops) / 10u;
                    rp[0] = (unsigned char)steps;
                    rp[1] = (unsigned char)loops;
                    rp[2] = (unsigned char)(secs > 255u ? 255u : secs);
                    rplen = 3u; resp = CMD_ACK; do_play = 1u;
                    uart_puts("[L15] 0x23 seq steps="); put_dec(steps);
                    uart_puts(" loops="); put_dec(loops);
                    uart_puts(" ~"); put_dec(secs); uart_puts("s -> ack then play\r\n");
                }
            } else if (cmd == 0x12u || cmd == 0x01u) {
                resp = CMD_ACK;
                uart_puts("[L1] hb/qry cmd="); put_hex2(cmd); uart_puts("\r\n");
            } else {
                resp = CMD_NACK;
                uart_puts("[L1] unknown cmd="); put_hex2(cmd); uart_puts("\r\n");
            }
        }

        /* 彩虹模式: 每协议轮推进相位 (Pi 下发节奏即动画节奏) */
        if (mode == 1u && val) {
            hue = (hue + 2u) & 255u;
        }

        /* 应答 (同 linkb v2): ACK 回显原 CMD+载荷; 0x02 用自定义传感载荷 */
        unsigned char of[24];
        unsigned int olen;
        of[0] = 0xAAu; of[1] = 0x55u;
        if (resp == CMD_ACK) {
            if (rplen > 0) {
                of[2] = (unsigned char)(1u + rplen);
                of[3] = CMD_ACK;
                of[4] = f[3];
                for (unsigned int i = 0; i < rplen; i++) of[5 + i] = rp[i];
                of[5 + rplen] = crc8(of + 2, 3u + rplen);
                olen = 5u + rplen + 1u;
            } else {
                if (len > 16u) len = 16u;
                of[2] = (unsigned char)(1u + len);
                of[3] = CMD_ACK;
                of[4] = f[3];
                for (unsigned int i = 0; i < len; i++) of[5 + i] = f[4 + i];
                of[5 + len] = crc8(of + 2, 3u + len);
                olen = 5u + len + 1u;
            }
        } else {
            of[2] = 1u; of[3] = resp;
            of[4] = crc8(of + 2, 2u);
            olen = 5u;
        }
        delay_ms(2);
        for (unsigned int i = 0; i < olen; i++)
            tx_byte(GPIO_IN_ADDR, of[i], bitd);
        seq++;
        uart_puts("[L1] resp="); put_hex2(resp);
        uart_puts(" seq="); put_dec(seq); uart_puts("\r\n");
        /* 0x23: ACK 已上线, 此刻起阻塞播放整段编排 (播放期 deaf, 播完自动归静态返回主循环) */
        if (do_play) seq_play();
    }
    for (;;) { delay_ms(1000); }
}
