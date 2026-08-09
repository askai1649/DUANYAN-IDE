/* yl69b.c - 探针2: 在 ADC init 之前执行 JS HAL boot_init 的关键动作,
 * 复现 "GATE 写不进去" 现象, 二分定位干扰源。
 * 顺序: WDT -> TIMG0 Timer0 配置 -> GPIO47 输出高 (WS2812 供电) -> RMT 初始化
 *       -> ADC 序列 (同 yl69.c) -> 循环采样
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

/* TIMG0 Timer0 (JS HAL boot_init 会配置它) */
#define TIMG0_T0CONFIG (*(volatile unsigned int *)0x6001F000)
#define TIMG0_T0LO     (*(volatile unsigned int *)0x6001F004)
#define TIMG0_T0UPDATE (*(volatile unsigned int *)0x6001F00C)

/* GPIO / IO_MUX (JS HAL: GPIO47 供电脚) */
#define GPIO_BASE  0x60004000u
#define IOMUX_BASE 0x60009000u
#define GPIO_REG(a) (*(volatile unsigned int *)(a))

/* RMT (JS HAL: hardy_rmt_init) */
#define PERIP_CLK_EN0 (*(volatile unsigned int *)0x600C0018)
#define RMT_BASE      0x60016000u
#define RMT_REG(o)    (*(volatile unsigned int *)(RMT_BASE + (o)))

/* SENS 块 */
#define SENS_BASE           0x60008800u
#define SENS_MEAS1_CTRL1    (*(volatile unsigned int *)(SENS_BASE + 0x08))
#define SENS_MEAS1_CTRL2    (*(volatile unsigned int *)(SENS_BASE + 0x0C))
#define SENS_MEAS1_MUX      (*(volatile unsigned int *)(SENS_BASE + 0x10))
#define SENS_ATTEN1         (*(volatile unsigned int *)(SENS_BASE + 0x14))
#define SENS_POWER_XPD_SAR  (*(volatile unsigned int *)(SENS_BASE + 0x3C))
#define SENS_SLAVE_ADDR1    (*(volatile unsigned int *)(SENS_BASE + 0x40))
#define SENS_PERI_CLK_GATE  (*(volatile unsigned int *)(SENS_BASE + 0x104))
#define SENS_PERI_RESET     (*(volatile unsigned int *)(SENS_BASE + 0x108))

#define MEAS1_EN_PAD_FORCE (1u << 31)
#define MEAS1_START_FORCE  (1u << 18)
#define MEAS1_START_SAR    (1u << 17)
#define MEAS1_DONE_SAR     (1u << 16)
#define CH 5u

static void uart_putc(char c)
{
    while (((UART0_STATUS >> 16) & 0x3FF) >= 127) {}
    UART0_FIFO = (unsigned int)c;
}
static void uart_puts(const char *s) { while (*s) uart_putc(*s++); }
static void put_hex(unsigned int v)
{
    uart_puts("0x");
    for (int i = 7; i >= 0; i--) {
        unsigned int d = (v >> (i * 4)) & 0xF;
        uart_putc(d < 10 ? ('0' + d) : ('A' + d - 10));
    }
}
static void put_dec(unsigned int v)
{
    char buf[11]; int n = 0;
    if (v == 0) { uart_putc('0'); return; }
    while (v) { buf[n++] = '0' + (v % 10); v /= 10; }
    while (n) uart_putc(buf[--n]);
}
static void delay_us(unsigned int us)
{
    volatile unsigned int n = us * 16u;
    while (n--) {}
}
static void delay_ms(unsigned int ms) { while (ms--) delay_us(1000); }

static void disable_wdts(void)
{
    RTC_CNTL_WDTWPROTECT = 0x50D83AA1u;
    RTC_CNTL_WDTCONFIG0 = 0;
    RTC_CNTL_WDTFEED = 1;
    RTC_CNTL_SWD_WPROT = 0x8F1D312Au;
    RTC_CNTL_SWD_CONF |= (1u << 31);
    TIMG0_WDTWPROTECT = 0x50D83AA1u;
    TIMG0_WDTCONFIG0 = 0;
    TIMG0_INT_CLR = 0xFFFFFFFFu;
}

/* ==== 复刻 JS HAL boot_init 的四段动作 ==== */
static void js_boot_timg0(void)
{
    TIMG0_T0CONFIG = (1u << 13) | (1u << 30) | (1u << 31);
}
static void js_boot_gpio47(void)
{
    GPIO_REG(IOMUX_BASE + 47 * 4) = (1u << 12) | (3u << 10);
    GPIO_REG(GPIO_BASE + 0x554 + 47 * 4) = 47;
    GPIO_REG(GPIO_BASE + 0x30) = 1u << (47 - 32);  /* out_en (47>=32) */
    GPIO_REG(GPIO_BASE + 0x14) = 1u << (47 - 32);  /* out high */
}
static void js_boot_rmt(void)
{
    PERIP_CLK_EN0 |= (1u << 9);
    RMT_REG(0xC0) = (1u << 0) | (1u << 1) | (1u << 3)
                  | (1u << 4) | (1u << 24) | (1u << 26) | (1u << 31);
    RMT_REG(0x20) = (1u << 8) | (1u << 16) | (1u << 6);
    RMT_REG(0x20) |= (1u << 24);
    GPIO_REG(IOMUX_BASE + 48 * 4) = (1u << 12) | (3u << 10);
    GPIO_REG(GPIO_BASE + 0x554 + 48 * 4) = 81;
}

static void adc_setup(void)
{
    SENS_PERI_CLK_GATE |= (1u << 30);
    SENS_PERI_RESET |= (1u << 30);
    SENS_PERI_RESET &= ~(1u << 30);
    uart_puts("GATE="); put_hex(SENS_PERI_CLK_GATE); uart_puts("\r\n");
    SENS_MEAS1_CTRL1 |= (0xFFu << 24);
    SENS_POWER_XPD_SAR |= (3u << 29) | (1u << 31);
    SENS_MEAS1_MUX &= ~(1u << 31);
    SENS_MEAS1_CTRL2 |= MEAS1_START_FORCE | MEAS1_EN_PAD_FORCE;
    SENS_ATTEN1 = (SENS_ATTEN1 & ~(3u << (CH * 2))) | (3u << (CH * 2));
    SENS_MEAS1_CTRL2 = (SENS_MEAS1_CTRL2 & ~(0xFFFu << 19)) | ((1u << CH) << 19);
}

static unsigned int adc1_read(void)
{
    unsigned int w = 0;
    while ((SENS_SLAVE_ADDR1 >> 22) & 0xFFu) { if (++w > 100000u) break; }
    SENS_MEAS1_CTRL2 = (SENS_MEAS1_CTRL2 & ~MEAS1_START_SAR);
    delay_us(20);
    SENS_MEAS1_CTRL2 |= MEAS1_START_SAR;
    unsigned int t = 0;
    while (!(SENS_MEAS1_CTRL2 & MEAS1_DONE_SAR)) {
        if (++t > 100000u) return 0xFFFFFFFFu;
        delay_us(1);
    }
    return SENS_MEAS1_CTRL2 & 0xFFFFu;
}

void _start(void) __attribute__((section(".entry"), naked));
void _start(void)
{
    disable_wdts();
    uart_puts("\r\n[YL69B] boot-sim probe\r\n");

    /* 逐段执行 JS boot 动作, 每段后回读 GATE 写入能力 */
    uart_puts("GATE0="); SENS_PERI_CLK_GATE |= (1u << 30);
    put_hex(SENS_PERI_CLK_GATE); uart_puts("\r\n");

    js_boot_timg0();
    uart_puts("GATE1="); SENS_PERI_CLK_GATE |= (1u << 30);
    put_hex(SENS_PERI_CLK_GATE); uart_puts("\r\n");

    js_boot_gpio47();
    uart_puts("GATE2="); SENS_PERI_CLK_GATE |= (1u << 30);
    put_hex(SENS_PERI_CLK_GATE); uart_puts("\r\n");

    js_boot_rmt();
    uart_puts("GATE3="); SENS_PERI_CLK_GATE |= (1u << 30);
    put_hex(SENS_PERI_CLK_GATE); uart_puts("\r\n");

    adc_setup();
    uart_puts("OK\r\n");

    for (int i = 0; i < 20; i++) {
        unsigned int r = adc1_read();
        uart_puts("RAW=");
        if (r == 0xFFFFFFFFu) uart_puts("TIMEOUT"); else put_dec(r);
        uart_puts(" GATE="); put_hex(SENS_PERI_CLK_GATE);
        uart_puts("\r\n");
        delay_ms(400);
    }
    for (;;) {}
}
