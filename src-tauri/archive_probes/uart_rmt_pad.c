/* uart_rmt_pad.c - RMT→GPIO48 矩阵通路判决针
 * 疑点: TX_END 正常触发但灯全程不亮; 此板 GPIO 矩阵输出路由曾判死
 * 方法: RMT 连发 44 个 item 的 1:1 方波(长帧), 期间把 GPIO48 切成
 *       输入, CPU 高速循环采样 GPIO_IN2, 统计高低样本数:
 *       hi>0 且 lo>0 → 波形到达 pad(矩阵活); 全0/全1 → 矩阵死
 * 顺带: 直驱 48 拉高拉低各 1s, 排除灯珠供电问题(WS2812 直流高不显色,
 *       只作电气参考)
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
#define GPIO_BASE 0x60004000u
#define GPIO_IN2  R(GPIO_BASE + 0x44)
#define RMT_BASE  0x60016000u
#define RMT_RAM_CH0 0x60016800u
#define RMT_CH0CONF0 R(RMT_BASE + 0x20)
#define RMT_INT_RAW  R(RMT_BASE + 0x70)
#define RMT_INT_ENA  R(RMT_BASE + 0x78)
#define RMT_INT_CLR  R(RMT_BASE + 0x7C)
#define RMT_SYS_CONF R(RMT_BASE + 0xC0)
#define SYSTEM_PERIP_CLK_EN0 R(0x600C0018)
#define BASE_CONF ((2u << 8) | (1u << 16) | (1u << 6))

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
static void delay_ms(unsigned int ms)
{
    volatile unsigned int n = ms * 16000u;
    while (n--) {}
}

static void rmt_long_start(void)
{
    /* 长帧: 44 item × 400tick + gap 4000 = 21600 ticks, 用于锚定测频 */
    volatile unsigned int *ram = (volatile unsigned int *)RMT_RAM_CH0;
    for (int i = 0; i < 44; i++)
        ram[i] = 200u | (1u << 15) | (200u << 16);
    ram[44] = 4000;
    ram[45] = 0;
    RMT_INT_CLR = 1;
    RMT_CH0CONF0 = BASE_CONF | (1u << 1) | (1u << 2);
    RMT_CH0CONF0 = BASE_CONF;
    RMT_CH0CONF0 = BASE_CONF | (1u << 24);
    RMT_CH0CONF0 = BASE_CONF | (1u << 0);
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
    uart_puts("\r\n[RMTPAD] RMT->GPIO48 matrix verdict\r\n");

    /* RMT 初始化 (同 linkm3 胜出配置) */
    SYSTEM_PERIP_CLK_EN0 |= (1u << 9);
    RMT_SYS_CONF = (1u << 0) | (1u << 1) | (1u << 3)
                 | (3u << 24) | (1u << 26) | (1u << 31);
    RMT_CH0CONF0 = BASE_CONF;
    RMT_CH0CONF0 |= (1u << 24);
    RMT_INT_ENA |= 1;
    R(GPIO_BASE + 0x554 + 48 * 4) = 81;        /* pin48 <- RMT sig81 */

    /* GPIO48 切输入(48 在 IN2 组, bit16) */
    R(0x60009000u + 4 + 48 * 4) = (1u << 12) | (3u << 10);
    R(GPIO_BASE + 0x28) = (1u << 16);          /* ENABLE_W1TC: 关输出 */

    /* ① 锚定测频: 21600 ticks 占几个 86.8us 锚 */
    for (int rep = 0; rep < 3; rep++) {
        rmt_long_start();
        int a = -1;
        for (int k = 0; k < 60; k++) {
            uart_putc('~');
            while (((UART0_STATUS >> 16) & 0x3FF) != 0) { }
            if (RMT_INT_RAW & 1u) { a = k + 1; break; }
        }
        /* mhz*10 = ticks*100 / (a*868) */
        unsigned int mhz10 = (a >= 1) ? (21600u * 100u) / ((unsigned int)a * 868u) : 0;
        uart_puts("[F] rep="); put_dec(rep);
        uart_puts(" anchors="); put_dec((unsigned int)a);
        uart_puts(" sclk_mhz_x10="); put_dec(mhz10); uart_puts("\r\n");
        delay_ms(200);
    }

    /* ② pad 回读: 再发长帧, 高速采样 */
    for (int rep = 0; rep < 2; rep++) {
        rmt_long_start();
        unsigned int hi = 0, lo = 0;
        for (unsigned int i = 0; i < 30000u; i++) {
            if ((GPIO_IN2 >> 16) & 1u) hi++; else lo++;
        }
        uart_puts("[P] rep="); put_dec(rep);
        uart_puts(" hi="); put_dec(hi);
        uart_puts(" lo="); put_dec(lo); uart_puts("\r\n");
        delay_ms(200);
    }

    /* 对照: 不发 RMT 时的 pad 电平 */
    unsigned int hi = 0, lo = 0;
    for (unsigned int i = 0; i < 30000u; i++) {
        if ((GPIO_IN2 >> 16) & 1u) hi++; else lo++;
    }
    uart_puts("[P] idle hi="); put_dec(hi);
    uart_puts(" lo="); put_dec(lo); uart_puts("\r\n");
    uart_puts("[DONE]\r\n");
    for (;;) { delay_ms(1000); }
}
