/* uart_map.c - 2x2 接线映射: ESP 驱动 GPIO4/GPIO5 四种组合,
 * Pi 侧同时读 GPIO14/GPIO15 → 解出真实连线拓扑
 * 阶段: A) 4=L,5=H  B) 4=H,5=L  C) 4=L,5=L  D) 4=H,5=H, 各 ~10s(慢钟)
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
#define GPIO_OUT_W1TS      GPIO_REG(GPIO_BASE + 0x8)
#define GPIO_OUT_W1TC      GPIO_REG(GPIO_BASE + 0xC)
#define GPIO_ENABLE_W1TS   GPIO_REG(GPIO_BASE + 0x24)
#define GPIO_FUNC_OUT_SEL(pin) GPIO_REG(GPIO_BASE + 0x554 + (pin) * 4)

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
static void set45(int v4, int v5)
{
    if (v4) GPIO_OUT_W1TS = (1u << 4); else GPIO_OUT_W1TC = (1u << 4);
    if (v5) GPIO_OUT_W1TS = (1u << 5); else GPIO_OUT_W1TC = (1u << 5);
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
    uart_puts("\r\n[MAP] 2x2 wiring map: driving GPIO4/GPIO5\r\n");

    GPIO_REG(IOMUX_BASE + 4 + 4 * 4) = (1u << 12) | (2u << 10);
    GPIO_REG(IOMUX_BASE + 4 + 5 * 4) = (1u << 12) | (2u << 10);
    GPIO_FUNC_OUT_SEL(4) = 128;
    GPIO_FUNC_OUT_SEL(5) = 128;
    GPIO_ENABLE_W1TS = (1u << 4) | (1u << 5);

    for (int round = 0; round < 3; round++) {
        set45(0, 1); uart_puts("[MAP] A: 4=L 5=H\r\n"); delay_ms(1500);
        set45(1, 0); uart_puts("[MAP] B: 4=H 5=L\r\n"); delay_ms(1500);
        set45(0, 0); uart_puts("[MAP] C: 4=L 5=L\r\n"); delay_ms(1500);
        set45(1, 1); uart_puts("[MAP] D: 4=H 5=H\r\n"); delay_ms(1500);
    }
    for (;;) { delay_ms(1000); }
}
