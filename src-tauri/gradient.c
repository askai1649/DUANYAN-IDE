/* DUANYAN 渐变彩灯固件 - ESP32-S3 板载 WS2812 (GPIO48)
 *
 * 时序策略: 开机自校准 —— 实测 nop-spin 循环与 TIMG0 定时器 tick 的比率,
 * 之后所有延时用"校准后的 spin 次数"实现, 不依赖任何假设的 CPU 频率。
 * 每个延时带迭代上限保险丝, 永不挂死。
 *
 * 入口: void _start(void) __attribute__((section(".entry"), used));
 */

/* ===== UART0 (诊断输出, 不动 CLKDIV) ===== */
#define UART0_BASE      0x60000000UL
#define UART0_FIFO      (*(volatile unsigned int *)(UART0_BASE + 0x00))
#define UART0_STATUS    (*(volatile unsigned int *)(UART0_BASE + 0x1C))

/* ===== 看门狗 (esp-idf v5.2.2 官方偏移) ===== */
#define RTC_CNTL_BASE   0x60008000UL
#define RTC_CNTL_WDTCONFIG0 (*(volatile unsigned int *)(RTC_CNTL_BASE + 0x98))
#define RTC_CNTL_WDTFEED    (*(volatile unsigned int *)(RTC_CNTL_BASE + 0xAC))
#define RTC_CNTL_WDTWPROTECT (*(volatile unsigned int *)(RTC_CNTL_BASE + 0xB0))
#define RTC_CNTL_SWD_CONF     (*(volatile unsigned int *)(RTC_CNTL_BASE + 0xB4))
#define RTC_CNTL_SWD_WPROTECT (*(volatile unsigned int *)(RTC_CNTL_BASE + 0xB8))
#define TIMG0_BASE      0x6001F000UL
#define TIMG0_WDTCONFIG0  (*(volatile unsigned int *)(TIMG0_BASE + 0x48))
#define TIMG0_WDTWPROTECT (*(volatile unsigned int *)(TIMG0_BASE + 0x64))
#define TIMG0_INT_CLR_TIMERS (*(volatile unsigned int *)(TIMG0_BASE + 0x7C))
#define WDT_WKEY_VALUE  0x50D83AA1UL
#define SWD_WKEY_VALUE  0x8F1D312AUL

/* ===== TIMG0 Timer0 时基 ===== */
#define TIMG0_T0CONFIG  (*(volatile unsigned int *)(TIMG0_BASE + 0x00))
#define TIMG0_T0LO      (*(volatile unsigned int *)(TIMG0_BASE + 0x04))
#define TIMG0_T0UPDATE  (*(volatile unsigned int *)(TIMG0_BASE + 0x0C))

/* ===== GPIO48 = WS2812 (OUT1 组, bit16) ===== */
#define GPIO_BASE       0x60004000UL
#define GPIO_OUT1_W1TS  (*(volatile unsigned int *)(GPIO_BASE + 0x14))
#define GPIO_OUT1_W1TC  (*(volatile unsigned int *)(GPIO_BASE + 0x18))
#define GPIO_ENABLE1_W1TS (*(volatile unsigned int *)(GPIO_BASE + 0x30))
#define GPIO_IN1_REG    (*(volatile unsigned int *)(GPIO_BASE + 0x40))
#define GPIO_FUNC48_OUT_SEL (*(volatile unsigned int *)(GPIO_BASE + 0x614))
#define GPIO_FUNC48_IN_SEL  (*(volatile unsigned int *)(GPIO_BASE + 0x214))
#define PIN48_BIT       (1u << 16)
#define IO_MUX_PIN48    (*(volatile unsigned int *)(0x60009000UL + 48 * 4))

#define PIN_HI()  (GPIO_OUT1_W1TS = PIN48_BIT)
#define PIN_LO()  (GPIO_OUT1_W1TC = PIN48_BIT)

static unsigned int ticks_per_us;  /* 校准结果: 每微秒 tick 数 (UART 基准实测) */

static void uart_putc(char c);
static void uart_puts(const char *s);
static void uart_num(unsigned int v);
static unsigned int timer_now(void);
static void wait_ticks(unsigned int start, unsigned int ticks);
static void calibrate(void);
static void ws2812_send(unsigned int grb);
static unsigned int wheel(unsigned int pos);

void _start(void) __attribute__((section(".entry"), used));
void _start(void) {
    uart_puts("[DUANYAN-OK] gradient boot!\r\n");

    /* 1. 关三只狗 */
    RTC_CNTL_WDTWPROTECT = WDT_WKEY_VALUE;
    RTC_CNTL_WDTFEED = 1;
    RTC_CNTL_WDTCONFIG0 = 0;
    RTC_CNTL_WDTWPROTECT = 0;
    RTC_CNTL_SWD_WPROTECT = SWD_WKEY_VALUE;
    RTC_CNTL_SWD_CONF = (1u << 30);
    RTC_CNTL_SWD_WPROTECT = 0;
    TIMG0_WDTWPROTECT = WDT_WKEY_VALUE;
    TIMG0_WDTCONFIG0 = 0;
    TIMG0_WDTWPROTECT = 0;
    TIMG0_INT_CLR_TIMERS = 0xFFFFFFFF;

    /* 2. 启动 TIMG0 Timer0: APB 源, 分频=1, 递增, 使能 */
    TIMG0_T0CONFIG = (1u << 13) | (1u << 30) | (1u << 31);

    /* 3. 自校准: 用 UART 115200 基准实测 ticks/us */
    calibrate();
    uart_puts("[DUANYAN-OK] ticks/us=");
    uart_num(ticks_per_us);
    uart_puts("\r\n");

    /* 4. GPIO48 配置: IO MUX 选 GPIO(PIN_FUNC_GPIO=1) + 输入使能 + 最强驱动 */
    IO_MUX_PIN48 = (1u << 12)    /* MCU_SEL = 1 (GPIO) */
                 | (1u << 4)     /* FUN_IE 输入使能 (回读自检用) */
                 | (3u << 10);   /* FUN_DRV = 3 最强驱动 */
    GPIO_FUNC48_OUT_SEL = 48;                /* pin48 输出 <- GPIO48 信号 */
    GPIO_FUNC48_IN_SEL = 48 | (1u << 7);     /* GPIO48 输入 <- pin48 */
    GPIO_ENABLE1_W1TS = PIN48_BIT;

    /* 4.5 回读自检: 驱动高/低各 2ms, 读引脚实际电平 */
    for (int probe = 0; probe < 2; probe++) {
        if (probe == 0) { PIN_HI(); } else { PIN_LO(); }
        wait_ticks(timer_now(), ticks_per_us * 2000);
        unsigned int lvl = (GPIO_IN1_REG >> 16) & 1;
        uart_puts("[DUANYAN-OK] probe=");
        uart_num(probe);
        uart_puts(" readback=");
        uart_num(lvl);
        uart_puts("\r\n");
    }
    PIN_LO();

    /* 5. 彩虹渐变: hue 0..255, 每帧 20ms */
    unsigned int hue = 0;
    for (unsigned int frame = 0; ; frame++) {
        ws2812_send(wheel(hue));

        if ((frame & 0x0F) == 0) {
            uart_puts("[DUANYAN-OK] frame=");
            uart_num(frame);
            uart_puts(" hue=");
            uart_num(hue);
            uart_puts(" tpu=");
            uart_num(ticks_per_us);
            uart_puts("\r\n");
        }

        /* 每 128 帧重复一次回读自检 (启动行可能被监控窗口错过) */
        if ((frame & 0x7F) == 0x7F) {
            PIN_HI();
            wait_ticks(timer_now(), ticks_per_us * 2000);
            unsigned int lvl = (GPIO_IN1_REG >> 16) & 1;
            PIN_LO();
            uart_puts("[DUANYAN-OK] probe readback=");
            uart_num(lvl);
            uart_puts("\r\n");
        }

        hue++;
        if (hue >= 256) { hue = 0; }
        wait_ticks(timer_now(), ticks_per_us * 20000);  /* 20ms */
    }
}

/* ===== 校准与延时 ===== */

static unsigned int timer_now(void) {
    TIMG0_T0UPDATE = 1;
    return TIMG0_T0LO;
}

/* 等待 ticks 个 tick (带轮询次数保险丝, 定时器异常时不会挂死) */
static void wait_ticks(unsigned int start, unsigned int ticks) {
    unsigned int cap = (ticks + 10) * 100 + 100000;
    while ((timer_now() - start) < ticks && cap--) { }
}

/* 校准: 发 10 字节 (100 bit ≈ 868us @115200), FIFO 排空+末字节发完 ≈ 955us,
 * 用定时器计时反推 ticks/us。UART 波特率由 ROM 配置且已验证可读, 是绝对基准。 */
static void calibrate(void) {
    unsigned int t0 = timer_now();
    for (int i = 0; i < 10; i++) { uart_putc('~'); }
    while (((UART0_STATUS >> 16) & 0x3FF) != 0) { }   /* 等 TX FIFO 排空 */
    unsigned int ticks = timer_now() - t0;
    ticks_per_us = ticks / 955;
    if (ticks_per_us == 0) { ticks_per_us = 10; }     /* 定时器异常时的保底值 */
}

/* ===== WS2812 ===== */

/* 发送 24bit GRB (MSB first)。
 * tick=100ns 且每次轮询自带 ~100ns 开销, 目标值需收紧补偿:
 * T0H 目标 0.25us (实际≈0.35, <0.45 零码上限)
 * T1H 目标 0.70us (实际≈0.80, >0.60 壹码下限)
 * 周期 1.1us, 复位 320us */
static void ws2812_send(unsigned int grb) {
    unsigned int t0h = (ticks_per_us * 25 + 99) / 100;
    unsigned int t1h = (ticks_per_us * 70 + 99) / 100;
    unsigned int per = (ticks_per_us * 110 + 99) / 100;
    if (t0h == 0) { t0h = 1; }

    for (int i = 23; i >= 0; i--) {
        unsigned int t0 = timer_now();
        PIN_HI();
        if ((grb >> i) & 1) {
            wait_ticks(t0, t1h);
        } else {
            wait_ticks(t0, t0h);
        }
        PIN_LO();
        wait_ticks(t0, per);
    }
    PIN_LO();
    wait_ticks(timer_now(), ticks_per_us * 320);
}

/* HSV 色轮 → GRB (亮度 1/16) */
static unsigned int wheel(unsigned int pos) {
    unsigned int r, g, b;
    pos = 255 - pos;
    if (pos < 85) {
        r = 255 - pos * 3; g = 0; b = pos * 3;
    } else if (pos < 170) {
        pos -= 85;
        r = 0; g = pos * 3; b = 255 - pos * 3;
    } else {
        pos -= 170;
        r = pos * 3; g = 255 - pos * 3; b = 0;
    }
    r >>= 4; g >>= 4; b >>= 4;
    return (g << 16) | (r << 8) | b;
}

/* ===== UART ===== */

static void uart_putc(char c) {
    while (((UART0_STATUS >> 16) & 0x3FF) >= 127) { }
    UART0_FIFO = (unsigned int)c;
}

static void uart_puts(const char *s) {
    while (*s) { uart_putc(*s++); }
}

static void uart_num(unsigned int v) {
    char tmp[10];
    int t = 0;
    do { tmp[t++] = (char)('0' + (v % 10)); v /= 10; } while (v > 0);
    while (t > 0) { uart_putc(tmp[--t]); }
}
