/* uart_zxprobe.c - ZX361 总线真收发探针 (m7 前置验证)
 * 目的: 实证 UART1 硬外设经 GPIO 矩阵在 GPIO17(TX)/GPIO18(RX) @115200
 *       能与 ZL-ZServo 总线舵机完成一次完整问答 (#001PRAD! → #001Pxxxx!)
 * 历史翻案: uart_ilb/uart_rxsrc 判"矩阵→RXD 死链(FPGA 缺陷)"是误判——
 *       基址 0x154 本来就对 (gpio_reg.h 实证), 但 IN_SEL 使能位是 bit7,
 *       bit6 是反相位! 当年写 (1<<6) 等于"接常量源+反相", RXD 恒 1。
 * 真值 (esp-idf v5.1 esp32s3 官方头文件交叉确认):
 *   GPIO_FUNC{n}_IN_SEL_CFG  = GPIO_BASE + 0x154 + n*4   [5:0]=gpio号, bit6=INV, bit7=ENABLE
 *   GPIO_FUNC{pin}_OUT_SEL_CFG = GPIO_BASE + 0x554 + pin*4
 *   U1RXD_IN_IDX = 15, U1TXD_OUT_IDX = 15  (gpio_sig_map.h)
 *   SYSTEM_UART1_CLK_EN = PERIP_CLK_EN0(0x600C0018) bit5, 复位默认已开
 * 判定: 控制台打出 RX 字节且以 '#' 开头含 'P' → 探针 PASS, 可集成 m7
 *       发得出但收不到 → 先查电平转换器 HV/LV 方向与半双工总线接线
 */
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
#define GPIO_IN            R(GPIO_BASE + 0x3C)
#define GPIO_ENABLE_W1TS   R(GPIO_BASE + 0x24)
#define GPIO_FUNC_OUT_SEL(pin) R(GPIO_BASE + 0x554 + (pin) * 4)
#define GPIO_FUNC_IN_SEL(sig)  R(GPIO_BASE + 0x154 + (sig) * 4)
#define IN_SEL_ENABLE      (1u << 7)   /* bit7! 不是 bit6 (bit6=INV) */
#define IOMUX_PIN(p) R(0x60009000u + 4 + (p) * 4)
#define PERIP_CLK_EN0      R(0x600C0018u)
#define U1(a) (*(volatile unsigned int *)(0x60010000u + (a)))
#define UART0_FIFO   (*(volatile unsigned int *)0x60000000)
#define UART0_STATUS (*(volatile unsigned int *)0x6000001C)

#define U1RXD_SIG 15u   /* U1RXD_IN_IDX */
#define U1TXD_SIG 15u   /* U1TXD_OUT_IDX */
#define PIN_TX    17u
#define PIN_RX    18u

static void uart_putc(char c)
{
    for (volatile int w = 0; w < 100000; w++) {
        if (((UART0_STATUS >> 16) & 0x3FF) < 127) break;
    }
    UART0_FIFO = (unsigned int)c;
}
static void uart_puts(const char *s) { while (*s) uart_putc(*s++); }
static void put_hex8(unsigned int v)
{
    const char *h = "0123456789ABCDEF";
    uart_putc(h[(v >> 4) & 0xF]);
    uart_putc(h[v & 0xF]);
}
static void put_dec(unsigned int v)
{
    char buf[12];
    int n = 0;
    if (v == 0) { uart_putc('0'); return; }
    while (v) { buf[n++] = '0' + (v % 10); v /= 10; }
    while (n--) uart_putc(buf[n]);
}
static void delay_ms(unsigned int ms)
{
    volatile unsigned int n = ms * 16000u;
    while (n--) {}
}

static void u1_putc(char c)
{
    for (volatile int w = 0; w < 200000; w++) {
        if (((U1(0x1C) >> 16) & 0x3FF) < 127) break;
    }
    U1(0x00) = (unsigned int)c;
}
static void u1_puts(const char *s) { while (*s) u1_putc(*s++); }

/* U1 RX FIFO 非空? STATUS bit[9:0]=rxfifo_cnt */
static unsigned int u1_rx_cnt(void) { return U1(0x1C) & 0x3FF; }
static unsigned int u1_getc(void) { return U1(0x00) & 0xFF; }

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
    uart_puts("\r\n[ZXPROBE] start, uart1 gpio17tx/gpio18rx @115200\r\n");

    /* --- UART1 外设初始化 (照抄 u1spam 实证序列, 只改波特率) --- */
    PERIP_CLK_EN0 |= (1u << 5);                 /* UART1 clk, 默认已开, 保险补写 */
    U1(0x14) = (40000000u / 115200u);           /* CLKDIV = 347 */
    U1(0x78) = (3u << 20) | (1u << 22) | (1u << 24) | (1u << 25);  /* CONF1: 阈值 */
    U1(0x20) = (3u << 2) | (2u << 4) | (1u << 17) | (1u << 18);    /* CONF0: 8N1 + FIFO 复位 */
    delay_ms(2);
    U1(0x20) = (3u << 2) | (2u << 4) | (1u << 28) | (1u << 25);    /* 收 RXFIFO_RST */

    /* --- TX: GPIO17 → sig15 (U1TXD) --- */
    IOMUX_PIN(PIN_TX) = (1u << 12) | (1u << 9) | (2u << 10) | (1u << 8);
    GPIO_FUNC_OUT_SEL(PIN_TX) = U1TXD_SIG;
    GPIO_ENABLE_W1TS = (1u << PIN_TX);

    /* --- RX: sig15 (U1RXD) ← GPIO18, 使能位 bit7! --- */
    IOMUX_PIN(PIN_RX) = (1u << 12) | (1u << 9) | (2u << 10) | (1u << 8);
    GPIO_FUNC_IN_SEL(U1RXD_SIG) = PIN_RX | IN_SEL_ENABLE;

    uart_puts("[ZXPROBE] IN_SEL(sig15)=");
    put_hex8(GPIO_FUNC_IN_SEL(U1RXD_SIG) & 0xFF);
    uart_puts(" OUT_SEL(17)=");
    put_hex8(GPIO_FUNC_OUT_SEL(PIN_TX) & 0xFF);
    uart_puts("\r\n");

    /* 上电总线安静期 + 清空可能的杂散 RX */
    delay_ms(200);
    while (u1_rx_cnt()) (void)u1_getc();

    unsigned int round = 0;
    for (;;) {
        round++;
        uart_puts("\r\n[ZXPROBE] round ");
        put_dec(round);
        uart_puts(" TX> #001PRAD!\r\n");

        u1_puts("#001PRAD!");

        /* 收窗: 帧间隙空闲即 break (不用 delay_ms 长延时, 避免被低频时钟拖爆)
         * 115200 下字节间隔 ~87us; 一帧 ~10 字节 ~900us。idle 阈值给足帧内间隙,
         * waited 硬上限兜底 (~数百 ms)。got=0 时也会在上限退出并打印 (nothing)。 */
        unsigned int got = 0;
        char echo[40];
        unsigned int idle = 0;
        unsigned int waited = 0;
        while (waited < 40000) {
            waited++;
            unsigned int before = got;
            while (u1_rx_cnt() && got < 38) {
                echo[got++] = (char)u1_getc();
            }
            if (got > before) {
                idle = 0;                 /* 收到新字节, 清空闲计数 */
            } else if (got > 0) {
                if (++idle > 1500) break; /* 帧已收完, 静默足够 -> 退出 */
            }
            for (volatile int d = 0; d < 150; d++) {}  /* ~10us 级微间隔 */
        }
        echo[got] = 0;

        if (got == 0) {
            uart_puts("[ZXPROBE] RX> (nothing)\r\n");
        } else {
            uart_puts("[ZXPROBE] RX> ");
            uart_puts(echo);
            uart_puts("  hex:");
            for (unsigned int i = 0; i < got; i++) {
                uart_putc(' ');
                put_hex8((unsigned char)echo[i]);
            }
            uart_puts("\r\n");
            if (echo[0] == '#') {
                uart_puts("[ZXPROBE] *** PASS: servo answered! ***\r\n");
            }
        }
        delay_ms(400);
    }
}
