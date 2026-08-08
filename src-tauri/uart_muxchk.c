/* uart_muxchk.c - 输入 mux 判决 v1:
 * 现象汇总: pad->GPIO_IN 活; TX 活; U0/U1 RX 全死; RXD_EDGE_CNT 恒 0;
 *           fsm2 曾见 CONST0 时 U1 RXD 仍 1 → 疑 mux 输出钉死
 * 本探针: 写 FUNC_IN_SEL → 回读 → 立即读对应 UART STATUS bit15(RXD)
 *   期望: 挂 CONST0 → RXD=0; 挂 CONST1 → RXD=1
 *   若回读正确但 RXD 恒 1 → mux 到 UART 段死 / RXD 被别处钉住
 *   顺带读 LOWPULSE/HIGHPULSE (默认 4095) 看 RXD 历史
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
#define GPIO_FUNC_IN_SEL(sig)  R(GPIO_BASE + 0x154 + (sig) * 4)

#define U0(a) (*(volatile unsigned int *)(0x60000000u + (a)))
#define U1(a) (*(volatile unsigned int *)(0x60010000u + (a)))
#define CONST1 56u
#define CONST0 60u

static void uart_putc(char c)
{
    for (volatile int w = 0; w < 100000; w++) {
        if (((UART0_STATUS >> 16) & 0x3FF) < 127) break;
    }
    UART0_FIFO = (unsigned int)c;
}
static void uart_puts(const char *s) { while (*s) uart_putc(*s++); }
static void put_hex(unsigned int v)
{
    const char *h = "0123456789ABCDEF";
    uart_puts("0x");
    for (int i = 7; i >= 0; i--) uart_putc(h[(v >> (i * 4)) & 0xF]);
}
static void delay_ms(unsigned int ms)
{
    volatile unsigned int n = ms * 16000u;
    while (n--) {}
}

/* 写 mux → 回读 → 报对应 UART 的 RXD */
static void chk(const char *tag, unsigned int sig, unsigned int val, int is_u1)
{
    GPIO_FUNC_IN_SEL(sig) = val;
    delay_ms(2);
    unsigned int rb = GPIO_FUNC_IN_SEL(sig);
    unsigned int st = is_u1 ? U1(0x1C) : U0(0x1C);
    uart_puts(tag);
    uart_puts(" rb="); put_hex(rb);
    uart_puts(" rxd="); uart_putc('0' + ((st >> 15) & 1u));
    uart_puts("\r\n");
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
    uart_puts("\r\n[MUXCHK] u1 sig15\r\n");
    chk("[MUXCHK] u1 const1:", 15, CONST1 | (1u << 6), 1);
    chk("[MUXCHK] u1 const0:", 15, CONST0 | (1u << 6), 1);
    chk("[MUXCHK] u1 const1b:", 15, CONST1 | (1u << 6), 1);
    chk("[MUXCHK] u1 en0:", 15, 0, 1);                    /* EN=0 → 应恒 0 */

    uart_puts("[MUXCHK] u0 sig6\r\n");
    chk("[MUXCHK] u0 const1:", 6, CONST1 | (1u << 6), 0);
    chk("[MUXCHK] u0 const0:", 6, CONST0 | (1u << 6), 0);
    chk("[MUXCHK] u0 const1b:", 6, CONST1 | (1u << 6), 0);

    uart_puts("[MUXCHK] pulses u1: low="); put_hex(U1(0x28));
    uart_puts(" high="); put_hex(U1(0x2C)); uart_puts("\r\n");
    uart_puts("[MUXCHK] pulses u0: low="); put_hex(U0(0x28));
    uart_puts(" high="); put_hex(U0(0x2C)); uart_puts("\r\n");

    /* 还原 U0RXD: 挂回 GPIO43 (ROM 默认脚), 避免影响打印脚之外的东西——
     * U0RXD 只是输入, 不影响 TX 打印, 但挂常量0会让控制台 RX 吃垃圾, 恢复 */
    GPIO_FUNC_IN_SEL(6) = 43 | (1u << 6);
    GPIO_FUNC_IN_SEL(15) = CONST1 | (1u << 6);
    uart_puts("[MUXCHK] done\r\n");
    for (;;) { delay_ms(1000); }
}
