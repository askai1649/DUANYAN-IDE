/* uart_rxscan.c - RX 线脚位扫描:
 * Pi 侧 ttyAMA0 持续发字节; ESP 依次把 U1RXD 输入矩阵接到
 * GPIO0..21 各脚, 每脚观察 ~3s(慢钟) RX FIFO, 哪脚收到数据 → RX 真身
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

#define GPIO_BASE  0x60004000u
#define IOMUX_BASE 0x60009000u
#define GPIO_REG(a) (*(volatile unsigned int *)(a))
#define GPIO_FUNC_OUT_SEL(pin) GPIO_REG(GPIO_BASE + 0x554 + (pin) * 4)
#define GPIO_FUNC_IN_SEL(sig)  GPIO_REG(GPIO_BASE + 0x154 + (sig) * 4)
#define GPIO_ENABLE_W1TC   GPIO_REG(GPIO_BASE + 0x28)

#define UART1_FIFO   (*(volatile unsigned int *)0x60010000)
#define UART1_CLKDIV (*(volatile unsigned int *)0x60010014)
#define UART1_STATUS (*(volatile unsigned int *)0x6001001C)
#define UART1_CONF0  (*(volatile unsigned int *)0x60010020)
#define UART1_CLK_CONF (*(volatile unsigned int *)0x60010078)
#define SYSTEM_PERIP_CLK_EN0 (*(volatile unsigned int *)0x600C0018)

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
    char buf[11]; int n = 0;
    if (v == 0) { uart_putc('0'); return; }
    while (v) { buf[n++] = '0' + (v % 10); v /= 10; }
    while (n) uart_putc(buf[--n]);
}
static void delay_ms(unsigned int ms)
{
    volatile unsigned int n = ms * 16000u;
    while (n--) {}
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

    delay_ms(1000);
    uart_puts("\r\n[RXSCAN] scanning U1RXD source pin 0..21\r\n");

    SYSTEM_PERIP_CLK_EN0 |= (1u << 5);
    UART1_CLKDIV = (40000000u / 115200u);
    UART1_CLK_CONF = (3u << 20) | (1u << 22) | (1u << 24) | (1u << 25);
    UART1_CONF0 = (3u << 2) | (2u << 4) | (1u << 17) | (1u << 18);
    delay_ms(1);
    UART1_CONF0 = (3u << 2) | (2u << 4) | (1u << 28) | (1u << 25);

    /* 全部候选脚释放输出 + IOMUX 输入使能 */
    GPIO_ENABLE_W1TC = 0x003FFFFFu;
    for (int p = 0; p <= 21; p++) {
        GPIO_FUNC_OUT_SEL(p) = 118;
        GPIO_REG(IOMUX_BASE + 4 + p * 4) = (1u << 12) | (1u << 9) | (1u << 8);
    }

    for (int pass = 0; pass < 2; pass++) {
        for (int p = 0; p <= 21; p++) {
            GPIO_FUNC_IN_SEL(15) = p | (1u << 6);
            /* 读空残留 */
            for (int i = 0; i < 130; i++) {
                if ((UART1_STATUS & 0x3FFu) == 0) break;
                (void)UART1_FIFO;
            }
            delay_ms(450);   /* ≈3.2s 慢钟观察窗 */
            unsigned int cnt = UART1_STATUS & 0x3FFu;
            uart_puts("[RXSCAN] pass=");
            put_dec(pass);
            uart_puts(" pin=");
            put_dec(p);
            uart_puts(" fifo=");
            put_dec(cnt > 999 ? 999 : cnt);
            if (cnt > 0 && cnt <= 128) {
                uart_puts(" data:");
                for (unsigned int k = 0; k < cnt && k < 8; k++) {
                    char c = (char)(UART1_FIFO & 0xFFu);
                    uart_putc((c >= 0x20 && c < 0x7F) ? c : '.');
                }
                uart_puts(" << HIT");
            }
            uart_puts("\r\n");
        }
    }
    uart_puts("[RXSCAN] done\r\n");
    for (;;) { delay_ms(1000); }
}
