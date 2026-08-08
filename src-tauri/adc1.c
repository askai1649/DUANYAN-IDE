/* adc1.c - ESP32-S3 裸机 SAR ADC 首测探针 (YL-69 土壤湿度 @ GPIO6 = ADC1_CH5)
 * 策略: 寄存器偏移来自 TRM, 用"写后回读 + 全寄存器扫描"实测验证每个配置位,
 *       绝不做未验证假设 (继承 RGB 马拉松方法论)
 * 预期: 干探头 RAW 偏高(~2800+), 湿探头明显下降, 读数连续无跳变 */
#define UART0_FIFO    (*(volatile unsigned int *)0x60000000)
#define UART0_STATUS  (*(volatile unsigned int *)0x6000001C)
#define RTC_CNTL_WDTWPROTECT (*(volatile unsigned int *)0x600080B0)
#define RTC_CNTL_WDTCONFIG0  (*(volatile unsigned int *)0x60008098)
#define RTC_CNTL_WDTFEED     (*(volatile unsigned int *)0x600080AC)
#define RTC_CNTL_SWD_WPROT   (*(volatile unsigned int *)0x600080B8)
#define RTC_CNTL_SWD_CONF    (*(volatile unsigned int *)0x600080B4)
#define TIMG0_WDTWPROTECT (*(volatile unsigned int *)0x6001F064)
#define TIMG0_WDTCONFIG0 (*(volatile unsigned int *)0x6001F048)
#define TIMG0_INT_CLR   (*(volatile unsigned int *)0x6001F07C)
#define REG(addr) (*(volatile unsigned int *)(addr))

/* APB_SARADC 基址 0x60040000 */
#define ADC_BASE      0x60040000u
#define ADC_CTRL      REG(ADC_BASE + 0x00)
#define ADC_CTRL2     REG(ADC_BASE + 0x04)
#define ADC_FSM       REG(ADC_BASE + 0x08)
#define ADC_SAR_STATUS1 REG(ADC_BASE + 0x0C)
#define ADC_SAR_STATUS2 REG(ADC_BASE + 0x10)
#define ADC_SAR_DATA  REG(ADC_BASE + 0x24)
#define ADC_ONETIME   REG(ADC_BASE + 0x28)
#define ADC_PATT_TAB1 REG(ADC_BASE + 0x2C)
#define ADC_MEAS1_CTRL2 REG(ADC_BASE + 0x34)
#define ADC_MEAS2_CTRL2 REG(ADC_BASE + 0x38)
#define ADC_ARB_CTRL  REG(ADC_BASE + 0x54)
#define ADC_FILTER_CTRL0 REG(ADC_BASE + 0x58)
#define ADC_FILTER_CTRL1 REG(ADC_BASE + 0x5C)

static void putc_(char c) {
    while (((UART0_STATUS >> 16) & 0x3FF) >= 127) { }
    UART0_FIFO = (unsigned int)c;
}
static void puts_(const char *s) { while (*s) putc_(*s++); }
static void drain(void) { while (((UART0_STATUS >> 16) & 0x3FF) != 0) { } }
static void putu(unsigned int v) {
    char b[12]; int i = 0;
    if (v == 0) { putc_('0'); return; }
    while (v) { b[i++] = (char)('0' + (v % 10)); v /= 10; }
    while (i) putc_(b[--i]);
}
static inline unsigned int fifo_cnt(void) {
    return (UART0_STATUS >> 16) & 0x3FF;
}

static unsigned int g_lpc = 350;
static unsigned int cal_loop(void) {
    putc_('C'); putc_('C'); putc_('C'); putc_('C'); putc_('C');
    unsigned int k = 0;
    while (fifo_cnt() != 0) { k++; }
    return k;
}
static void delay_us(unsigned int us) {
    unsigned int it = (g_lpc * us) / 434u;
    volatile unsigned int v = 0;
    while (it--) { v++; }
    (void)v;
}

/* 原始基线: 不写任何配置, 先看 SAR ADC 寄存器的复位默认值 */
static void dump_base(const char *tag) {
    puts_(tag);
    for (unsigned int off = 0; off <= 0x5Cu; off += 4u) {
        putc_(' '); putu(REG(ADC_BASE + off));
    }
    puts_("\r\n"); drain();
}

void x_main(void);
void _start(void) __attribute__((section(".entry"), used, noreturn));
void _start(void) { x_main(); for (;;) { } }

void x_main(void) {
    /* 看门狗禁用 (标准序列) */
    RTC_CNTL_WDTWPROTECT = 0x50D83AA1u;
    RTC_CNTL_WDTFEED = 1u;
    RTC_CNTL_WDTCONFIG0 = 0u;
    RTC_CNTL_WDTWPROTECT = 0u;
    RTC_CNTL_SWD_WPROT = 0x8F1D312Au;
    RTC_CNTL_SWD_CONF = 0x50D83AA1u;
    RTC_CNTL_SWD_CONF = 0x8F1D312Au;
    TIMG0_WDTWPROTECT = 0x50D83AA1u;
    TIMG0_WDTCONFIG0 = 0u;
    TIMG0_WDTWPROTECT = 0u;
    TIMG0_INT_CLR = 1u;

    puts_("[DUANYAN-OK] adc1 GPIO6\r\n"); drain();
    g_lpc = cal_loop();
    puts_("LPC="); putu(g_lpc); puts_("\r\n"); drain();

    /* 第1步: 基线转储 - SAR ADC 复位默认值 */
    dump_base("BASE");

    /* 第2步: 配置 FSM 采样周期 + 读回验证 */
    ADC_FSM = (0x1u << 28) | (0x1u << 24) | (0x1u << 20) | (0x2u << 16) | 0x4u;
    puts_("FSM="); putu(ADC_FSM); puts_("\r\n"); drain();

    /* 第3步: ONETIME 通道配置 (CH5/GPIO6, 11dB, 12bit) 写后回读 */
    ADC_ONETIME = (5u << 7) | (3u << 5) | (3u << 3);
    puts_("OT="); putu(ADC_ONETIME); puts_("\r\n"); drain();

    /* 第4步: 仲裁器强制单次模式 (force_mask=1, rtc_fix=1, arb_fix=1) */
    ADC_ARB_CTRL = (1u << 10) | (1u << 8) | (1u << 6);
    puts_("ARB="); putu(ADC_ARB_CTRL); puts_("\r\n"); drain();

    /* 第5步: CTRL2 配置 + 回读 */
    ADC_CTRL2 = 0u;
    puts_("C2="); putu(ADC_CTRL2); puts_("\r\n"); drain();

    /* 第6步: 尝试一次触发, 转储全寄存器, 寻找 SAR_DATA 响应 */
    ADC_ONETIME |= (1u << 1);   /* start */
    { volatile unsigned long d = 0; while (d < 20000ul) d++; }
    dump_base("POST");
    puts_("DONE="); putu((ADC_ONETIME >> 0) & 1u);
    putc_(' '); putu(ADC_SAR_DATA); puts_("\r\n"); drain();

    /* 第7步: 主循环 - 反复单次采样, 每 500ms 打印一次 RAW
     * 若 RAW 恒 0/恒 4095/恒固定 -> 配置未生效, 需回读分析 */
    for (;;) {
        unsigned int sum = 0;
        for (int i = 0; i < 8; i++) {
            ADC_ONETIME = (5u << 7) | (3u << 5) | (3u << 3) | (1u << 1);
            volatile unsigned long d = 0; while (d < 5000ul) d++;
            sum += ADC_SAR_DATA;
        }
        unsigned int raw = sum >> 3;
        puts_("RAW="); putu(raw);
        /* 换算: 12bit 满量程 3.1V (11dB 衰减实测满量程) */
        putc_(' '); putu(raw * 31 / 4096); putc_('0'); putc_('m'); putc_('V');
        puts_("\r\n"); drain();
        delay_us(500000);
    }
}
