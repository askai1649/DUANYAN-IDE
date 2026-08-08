/* uart_bitbangtx.c - 位啜 TX: 纯直驱 GPIO4 @9600 发 HELLO-PI
 * 根因背景: FUNC_OUT_SEL 矩阵路由死, 但 W1TS/W1TC 直驱 pad 活
 * 慢钟校准: stretch=6.811 (Pi 实测 paddrv 方波 6811ms/名义1000ms)
 * BIT_ITERS=245 => 位时 104.17us 实际 (245*6.811/16000)
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
#define GPIO_OUT_W1TS      R(GPIO_BASE + 0x8)
#define GPIO_OUT_W1TC      R(GPIO_BASE + 0xC)
#define GPIO_ENABLE_W1TS   R(GPIO_BASE + 0x24)
#define IOMUX_PIN(p) R(0x60009000u + 4 + (p) * 4)

#define BIT_ITERS 245u
#define TX4_HI() (GPIO_OUT_W1TS = (1u << 4))
#define TX4_LO() (GPIO_OUT_W1TC = (1u << 4))

static void uart_putc(char c)
{
    for (volatile int w = 0; w < 100000; w++) {
        if (((UART0_STATUS >> 16) & 0x3FF) < 127) break;
    }
    UART0_FIFO = (unsigned int)c;
}
static void uart_puts(const char *s) { while (*s) uart_putc(*s++); }
static void delay_ms(unsigned int ms)
{
    volatile unsigned int n = ms * 16000u;
    while (n--) {}
}
static void bit_delay(void)
{
    volatile unsigned int n = BIT_ITERS;
    while (n--) {}
}
static void tx_byte(unsigned char b)
{
    TX4_LO();              /* 起始位 */
    bit_delay();
    for (int i = 0; i < 8; i++) {
        if ((b >> i) & 1) TX4_HI(); else TX4_LO();
        bit_delay();
    }
    TX4_HI();              /* 停止位 */
    bit_delay();
}
static void tx_str(const char *s) { while (*s) tx_byte((unsigned char)*s++); }

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
    uart_puts("\r\n[BBTX] bitbang TX GPIO4 @9600, BIT_ITERS=245\r\n");

    /* 纯直驱: 使能输出, 空闲 High; 不碰 FUNC_OUT_SEL */
    IOMUX_PIN(4) |= (1u << 9);
    TX4_HI();
    GPIO_ENABLE_W1TS = (1u << 4);
    delay_ms(50);

    for (int r = 0; r < 200; r++) {
        tx_str("HELLO-PI ");
        tx_str("\r\n");
        uart_puts("[BBTX] round sent\r\n");
        delay_ms(3000);    /* 实际 ~20s/轮, Pi 抓取窗口内至少 1 帧 */
    }
    for (;;) { delay_ms(1000); }
}
