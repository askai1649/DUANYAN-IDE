/* uart_mux18.c - IO_MUX 直连判决: U0TXD(sig6) 经 MCU_SEL=1 直出 GPIO18
 * 绕开 GPIO 矩阵。Pi 抓 GPIO18 所接脚 @115200:
 * 收到文本 = pad 输出级活, 病根在 GPIO 矩阵; 收不到 = pad 输出级死
 * 控制台仍走 GPIO43 不受影响
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
#define GPIO_ENABLE_W1TS   R(GPIO_BASE + 0x24)
#define IOMUX_PIN(p) R(0x60009000u + 4 + (p) * 4)

static void uart_putc(char c)
{
    for (volatile int w = 0; w < 100000; w++) {
        if (((UART0_STATUS >> 16) & 0x3FF) < 127) break;
    }
    UART0_FIFO = (unsigned int)c;
}
static void uart_puts(const char *s) { while (*s) uart_putc(*s++); }
static void put_num(unsigned int v)
{
    char b[12]; int i = 0;
    if (v == 0) { uart_putc('0'); return; }
    while (v) { b[i++] = '0' + v % 10; v /= 10; }
    while (i--) uart_putc(b[i]);
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

    delay_ms(300);
    uart_puts("\r\n[MUX18] U0TXD direct IO_MUX route to GPIO18 (MCU_SEL=1)\r\n");

    /* U0TXD = sig6; IO_MUX MCU_SEL=1 直连 UART0, 不走矩阵 */
    IOMUX_PIN(18) = (1u << 12) | (2u << 10);
    GPIO_ENABLE_W1TS = (1u << 18);

    for (int r = 0; r < 600; r++) {
        uart_puts("[MUX18] hello-from-gpio18 round ");
        put_num(r);
        uart_puts("\r\n");
        delay_ms(200);
    }
    for (;;) { delay_ms(1000); }
}
