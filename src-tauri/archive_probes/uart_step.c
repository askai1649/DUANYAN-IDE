/* uart_step.c - 分级标记探针: uart_loop v6 全量逻辑 + 每步标记打印
 * 与 uart_loop.c 的两处差异:
 *   1. uart_putc 用有界等待 (bare 存活版), 排除无限等待挂死
 *   2. 每个外设操作组后打印标记, 定位挂死区间
 * 若全程跑完 → 挂死源是无限等待 putc; 若停在某标记后 → 该步操作挂死
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

#define UART1_FIFO   (*(volatile unsigned int *)0x60010000)
#define UART1_CLKDIV (*(volatile unsigned int *)0x60010014)
#define UART1_STATUS (*(volatile unsigned int *)0x6001001C)
#define UART1_CONF0  (*(volatile unsigned int *)0x60010020)
#define UART1_CLK_CONF (*(volatile unsigned int *)0x60010078)
#define SYSTEM_PERIP_CLK_EN0 (*(volatile unsigned int *)0x600C0018)
#define GPIO_BASE  0x60004000u
#define IOMUX_BASE 0x60009000u
#define GPIO_REG(a) (*(volatile unsigned int *)(a))
#define GPIO_FUNC_OUT_SEL(pin) GPIO_REG(GPIO_BASE + 0x554 + (pin) * 4)
#define GPIO_FUNC_IN_SEL(sig)  GPIO_REG(GPIO_BASE + 0x154 + (sig) * 4)
#define GPIO_ENABLE_W1TS       GPIO_REG(GPIO_BASE + 0x24)
#define GPIO_ENABLE_READ       GPIO_REG(GPIO_BASE + 0x20)

static void uart_putc(char c)
{
    for (volatile int w = 0; w < 100000; w++) {   /* 有界等待 */
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
static void u1_putc(char c)
{
    for (volatile int w = 0; w < 100000; w++) {   /* 有界等待 */
        if (((UART1_STATUS >> 16) & 0x3FF) < 127) break;
    }
    UART1_FIFO = (unsigned int)c;
}

void _start(void) __attribute__((section(".entry"), naked));
void _start(void)
{
    uart_puts("\r\n[STEP] S0 enter\r\n");

    RTC_CNTL_WDTWPROTECT = 0x50D83AA1u;
    RTC_CNTL_WDTCONFIG0 = 0;
    RTC_CNTL_WDTFEED = 1;
    RTC_CNTL_SWD_WPROT = 0x8F1D312Au;
    RTC_CNTL_SWD_CONF |= (1u << 31);
    TIMG0_WDTWPROTECT = 0x50D83AA1u;
    TIMG0_WDTCONFIG0 = 0;
    TIMG0_INT_CLR = 0xFFFFFFFFu;
    uart_puts("[STEP] S1 wd-off\r\n");

    delay_ms(2000);
    uart_puts("[STEP] S2 delay\r\n");

    SYSTEM_PERIP_CLK_EN0 |= (1u << 5);
    UART1_CLKDIV = (40000000u / 115200u);
    UART1_CLK_CONF = (3u << 20) | (1u << 22) | (1u << 24) | (1u << 25);
    UART1_CONF0 = (3u << 2) | (2u << 4) | (1u << 17) | (1u << 18);
    delay_ms(1);
    UART1_CONF0 = (3u << 2) | (2u << 4) | (1u << 28) | (1u << 25);
    uart_puts("[STEP] S3 u1init\r\n");

    GPIO_REG(IOMUX_BASE + 4 + 4 * 4) = (1u << 12) | (1u << 9) | (2u << 10) | (1u << 8);
    uart_puts("[STEP] S4 iomux\r\n");

    GPIO_FUNC_OUT_SEL(4) = 15;
    GPIO_ENABLE_W1TS = (1u << 4);
    GPIO_FUNC_IN_SEL(15) = 4 | (1u << 6);
    uart_puts("[STEP] S5 matrix\r\n");

    delay_ms(10);
    for (int round = 0; round < 5; round++) {
        for (int i = 0; i < 128; i++) {
            if ((UART1_STATUS & 0x3FFu) == 0) break;
            (void)UART1_FIFO;
        }
        const char *msg = "HELLO";
        for (int i = 0; i < 5; i++) u1_putc(msg[i]);
        delay_ms(20);
        unsigned int cnt = UART1_STATUS & 0x3FFu;
        uart_puts("[STEP] round=");
        uart_putc('0' + round);
        uart_puts(" rxfifo=");
        uart_putc('0' + (cnt > 9 ? 9 : cnt));
        uart_puts(" data=");
        int ok = (cnt == 5);
        for (unsigned int i = 0; i < cnt && i < 8; i++) {
            uart_putc((char)(UART1_FIFO & 0xFF));
        }
        uart_puts(ok ? "  << MATCH\r\n" : "  << MISMATCH\r\n");
        delay_ms(800);
    }
    uart_puts("[STEP] done\r\n");
    for (;;) { delay_ms(1000); }
}
