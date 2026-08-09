/* uart_linkm4.c - 模式 B M4: 协议链路 + WS2812 灯效 + 传感回传
 * 基线: linkm3 (M3 验收资产原样保留, 传输层/RMT/灯效零改动)
 * 新增: SAR ADC1_CH5 (GPIO6, YL-69 土壤湿度) oneshot 采样,
 *       序列照搬 yl69.c 实证配置 (IDF v5.2 sens_reg.h 逐项对照)
 * CMD 0x02 传感查询: Pi->S3, ACK 载荷 = [0x02 回声][sid][raw_hi][raw_lo][status]
 *   sid=1 YL-69 土壤湿度; status: 0=OK  1=ADC 超时
 * 其余 CMD 与 linkm3 相同 (0x10 灯效, 0x11 GPIO, 0x12/0x01 ACK 回显, 未知 NACK)
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
#define ADC_CH 5u   /* GPIO6 = ADC1_CH5 (YL-69 AO) */

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
    /* 通道 CH5, 衰减 11dB (atten=3, 0~3.3V 全量程) */
    SENS_ATTEN1 = (SENS_ATTEN1 & ~(3u << (ADC_CH * 2))) | (3u << (ADC_CH * 2));
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
    if (asm_wait_fall(GPIO_IN_ADDR, 15000000u) >= 15000000u) return 2;
    f[0] = (unsigned char)rx_byte(GPIO_IN_ADDR, bitd);
    asm_wait_rise(GPIO_IN_ADDR, bitd * 4u);
    if (asm_wait_fall(GPIO_IN_ADDR, bitd * 6u) >= bitd * 6u) return 3;
    f[1] = (unsigned char)rx_byte(GPIO_IN_ADDR, bitd);
    asm_wait_rise(GPIO_IN_ADDR, bitd * 4u);
    if (asm_wait_fall(GPIO_IN_ADDR, bitd * 6u) >= bitd * 6u) return 3;
    f[2] = (unsigned char)rx_byte(GPIO_IN_ADDR, bitd);
    asm_wait_rise(GPIO_IN_ADDR, bitd * 4u);
    unsigned int len = f[2];
    if (len > 64u) len = 64u;
    unsigned int total = 4u + len + 1u;
    for (unsigned int i = 3; i < total; i++) {
        if (asm_wait_fall(GPIO_IN_ADDR, bitd * 6u) >= bitd * 6u) return 3;
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
    uart_puts("[M4] sysconf="); put_hex(R(RMT_BASE + 0xC0));
    uart_puts(" inten="); put_hex(R(RMT_BASE + 0x78)); uart_puts("\r\n");
    /* GPIO48: IOMUX 选 GPIO + 最强驱动; 矩阵输出 <- RMT_CH0_TX (sig 81) */
    IOMUX_PIN(48) = (1u << 12) | (3u << 10);
    R(GPIO_BASE + 0x554 + 48 * 4) = 81;

    int a = measure_anchors();
    uart_puts("[M4] anchors="); put_dec((unsigned int)a); uart_puts("\r\n");
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
            uart_puts("[M4] sclk_mhz_x10="); put_dec(mhz10); uart_puts("\r\n");
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
    uart_puts("[M4] t0h="); put_dec(ws_t0h);
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
        uart_puts("[M4] st50="); put_hex(R(RMT_BASE + 0x50));
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

void _start(void) __attribute__((section(".entry"), naked));
void _start(void)
{
    RTC_CNTL_WDTWPROTECT = 0x50D83AA1u;
    RTC_CNTL_WDTCONFIG0 = 0;
    RTC_CNTL_WDTFEED = 1;
    RTC_CNTL_SWD_WPROT = 0x8F1D312Au;
    RTC_CNTL_SWD_CONF |= (1u << 31);
    TIMG0_WDTWPROTECT = 0x50D83AA1u;
    TIMG0_WDTCONFIG0 = 0;
    TIMG0_INT_CLR = 0xFFFFFFFFu;

    delay_ms(300);
    uart_puts("\r\n[LINKM4] M2/M3 link + WS2812 + sensor feedback\r\n");

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
    uart_puts("[M4] adc_probe=");
    if (adc_probe == 0xFFFFFFFFu) uart_puts("TIMEOUT"); else put_dec(adc_probe);
    uart_puts("\r\n");
    ws_show(0x0000FFu);   /* 开机亮蓝: RMT+灯珠通路活体证明 */
    uart_puts("[M4] boot blue\r\n");

    static unsigned char f[80];
    unsigned int seq = 0;
    unsigned int mode = 0, hue = 0, val = 0;
    for (int round = 0; round < 20000; round++) {
        uart_puts("[M4] await burst\r\n");
        if (asm_wait_fall(GPIO_IN_ADDR, 30000000u) >= 30000000u) {
            uart_puts("[M4] timeout\r\n");
            continue;
        }
        unsigned int bitd = asm_wait_rise(GPIO_IN_ADDR, 5000000u);
        if (bitd == 0 || bitd >= 5000000u) {
            uart_puts("[M4] calib noedge\r\n");
            continue;
        }
        drain_burst(bitd);
        uart_puts("[M4] bitd="); put_dec(bitd); uart_puts("\r\n");

        unsigned int st = read_frame(f, bitd);
        if (st != 1) {
            uart_puts("[M4] frame st="); put_dec(st); uart_puts("\r\n");
            continue;
        }
        unsigned int len = f[2];
        if (len > 64u) len = 64u;
        unsigned int total = 4u + len + 1u;
        uart_puts("[M4] frame:");
        for (unsigned int i = 0; i < total; i++) {
            uart_putc(' '); put_hex2(f[i]);
        }
        uart_puts("\r\n");

        unsigned char resp;
        unsigned char rp[8];          /* 自定义 ACK 载荷 (不含回声 CMD) */
        unsigned int rplen = 0;
        unsigned char calc = crc8(f + 2, 2u + len);
        if (f[0] != 0xAAu || f[1] != 0x55u || calc != f[4 + len]) {
            resp = 0x16u;
            uart_puts("[M4] bad frame\r\n");
        } else {
            unsigned int cmd = f[3];
            if (cmd == 0x02u) {
                /* 传感查询: 采样 ADC1_CH5, ACK 载荷=[0x02回声][sid][raw_hi][raw_lo][status] */
                unsigned int raw = adc1_read();
                rp[0] = 1u;   /* sid=1 YL-69 土壤湿度 */
                if (raw == 0xFFFFFFFFu) {
                    rp[1] = 0xFFu; rp[2] = 0xFFu; rp[3] = 1u;
                    uart_puts("[M4] adc TIMEOUT\r\n");
                } else {
                    rp[1] = (unsigned char)(raw >> 8);
                    rp[2] = (unsigned char)(raw & 0xFFu);
                    rp[3] = 0u;
                    uart_puts("[M4] adc raw="); put_dec(raw); uart_puts("\r\n");
                }
                rplen = 4u;
                resp = CMD_ACK;
            } else if (cmd == 0x10u && len >= 3u) {
                if (f[4] != 0xFFu) hue = f[4];   /* 0xFF=彩虹继续当前相位 */
                val = f[5]; mode = f[6];
                if (mode == 1u) {
                    unsigned int v = val ? val : 120u;
                    unsigned int g = ws_show(hsv_grb(hue, v));
                    uart_puts("[M4] rainbow hue="); put_dec(hue);
                    uart_puts(" val="); put_dec(v);
                    uart_puts(" g="); put_dec(g); uart_puts("\r\n");
                } else {
                    unsigned int g = ws_show(hsv_grb(hue, val));
                    uart_puts("[M4] static hue="); put_dec(hue);
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
                uart_puts("[M4] gpio pin="); put_dec(pin);
                uart_puts(" lvl="); put_dec(lvl); uart_puts("\r\n");
            } else if (cmd == 0x12u || cmd == 0x01u) {
                resp = CMD_ACK;
                uart_puts("[M4] hb/qry cmd="); put_hex2(cmd); uart_puts("\r\n");
            } else {
                resp = CMD_NACK;
                uart_puts("[M4] unknown cmd="); put_hex2(cmd); uart_puts("\r\n");
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
        uart_puts("[M4] resp="); put_hex2(resp);
        uart_puts(" seq="); put_dec(seq); uart_puts("\r\n");
    }
    for (;;) { delay_ms(1000); }
}
