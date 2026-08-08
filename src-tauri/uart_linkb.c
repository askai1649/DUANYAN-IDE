/* uart_linkb.c - 模式 B 主链路 M2: 协议帧双向通信
 * 传输层: hello8 v11 实证结构 (校准突发 + C 逐位环位啜, @9600)
 *   RX=GPIO5(Pi→ESP), TX=GPIO4(ESP→Pi), 控制台 UART0(COM4)
 * 帧格式: AA 55 LEN CMD PAYLOAD[LEN] CRC8  (CRC 覆盖 LEN..payload)
 * 每轮 Pi 发: U*8 校准突发 + 一帧; ESP 回应答帧
 * 应答: ACK(0x06) = AA 55 (1+LEN) 06 原CMD 原PAYLOAD CRC (回显供逐帧核对)
 *       NACK=0x15(未知CMD), 0x16(CRC/帧头错), 均为 AA 55 01 RESP CRC
 * CMD: 0x11 GPIO控制(pay=pin,lvl) 0x12 查询 0x01 心跳 → ACK回显
 * 测试脚: GPIO10 预使能输出, 供 0x11 实测电平翻转
 * v2: ACK 回显化 + 轮次 20000 (支撑 1 小时压测)
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
#define GPIO_IN_ADDR       (volatile unsigned int *)(GPIO_BASE + 0x3C)
#define GPIO_OUT_W1TS      R(GPIO_BASE + 0x8)
#define GPIO_OUT_W1TC      R(GPIO_BASE + 0xC)
#define GPIO_ENABLE_W1TS   R(GPIO_BASE + 0x24)
#define GPIO_ENABLE_W1TC   R(GPIO_BASE + 0x28)
#define IOMUX_PIN(p) R(0x60009000u + 4 + (p) * 4)

#define TX4_HI() (GPIO_OUT_W1TS = (1u << 4))
#define TX4_LO() (GPIO_OUT_W1TC = (1u << 4))

#define CMD_ACK  0x06u
#define CMD_NACK 0x15u

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
static void put_hex2(unsigned int v)
{
    const char *d = "0123456789ABCDEF";
    uart_putc(d[(v >> 4) & 15]); uart_putc(d[v & 15]);
}
static void delay_ms(unsigned int ms)
{
    volatile unsigned int n = ms * 16000u;
    while (n--) {}
}

/* ==== 规范环 (hello8 v11 实证结构) ==== */
static unsigned int asm_ticks(volatile unsigned int *ain, unsigned int n,
                              unsigned int men, unsigned int xorv)
{
    unsigned int s = n, v, lvl, x, h;
    __asm__ volatile (
        "1:\n\t"
        "l32i %[v], %[a], 0\n\t"
        "extui %[lvl], %[v], 5, 1\n\t"
        "xor %[x], %[lvl], %[xv]\n\t"
        "and %[h], %[x], %[m]\n\t"
        "bnez %[h], 2f\n\t"
        "addi %[s], %[s], -1\n\t"
        "bnez %[s], 1b\n\t"
        "2:\n"
        : [s] "+r"(s), [v] "=&r"(v), [lvl] "=&r"(lvl),
          [x] "=&r"(x), [h] "=&r"(h)
        : [a] "r"(ain), [xv] "r"(xorv), [m] "r"(men)
        : "memory");
    return n - s;
}
static void asm_delay(volatile unsigned int *ain, unsigned int n)
{
    asm_ticks(ain, n, 0u, 0u);
}
static unsigned int asm_wait_fall(volatile unsigned int *ain, unsigned int to)
{
    return asm_ticks(ain, to, 1u, 1u);
}
static unsigned int asm_wait_rise(volatile unsigned int *ain, unsigned int to)
{
    return asm_ticks(ain, to, 1u, 0u);
}
static unsigned int rd5(volatile unsigned int *ain)
{
    unsigned int v;
    __asm__ volatile ("l32i %[v], %[a], 0\n\t" : [v] "=r"(v) : [a] "r"(ain));
    return (v >> 5) & 1u;
}
static unsigned int rx_byte(volatile unsigned int *ain, unsigned int bitd)
{
    unsigned int byte = 0;
    asm_delay(ain, bitd + bitd / 2u);
    if (rd5(ain)) byte |= 1u;
    for (unsigned int b = 1; b < 8u; b++) {
        asm_delay(ain, bitd);
        if (rd5(ain)) byte |= (1u << b);
    }
    return byte;
}
static void tx_byte_body(volatile unsigned int *ain, unsigned int v,
                         unsigned int bitd)
{
    for (unsigned int b = 0; b < 8u; b++) {
        asm_delay(ain, bitd);
        if ((v >> b) & 1u) GPIO_OUT_W1TS = (1u << 4);
        else GPIO_OUT_W1TC = (1u << 4);
    }
    asm_delay(ain, bitd);
    GPIO_OUT_W1TS = (1u << 4);   /* 停止位高 */
}
static void tx_byte(volatile unsigned int *ain, unsigned int v,
                    unsigned int bitd)
{
    TX4_LO();
    tx_byte_body(ain, v, bitd);
    asm_delay(ain, bitd);        /* 帧内保护间隔 */
}
static void drain_burst(unsigned int bitd)
{
    for (;;) {
        if (asm_wait_fall(GPIO_IN_ADDR, bitd * 3u) >= bitd * 3u) return;
        asm_wait_rise(GPIO_IN_ADDR, bitd * 14u);
    }
}

/* 读一帧: 等首字节下降沿, 收满 4+LEN 字节。
 * 返回: 1=完整帧(已验CRC前长度), 2=首沿超时, 3=字节流中断 */
static unsigned int read_frame(unsigned char *f, unsigned int bitd)
{
    if (asm_wait_fall(GPIO_IN_ADDR, 15000000u) >= 15000000u) return 2;
    f[0] = (unsigned char)rx_byte(GPIO_IN_ADDR, bitd);
    asm_wait_rise(GPIO_IN_ADDR, bitd * 4u);
    if (asm_wait_fall(GPIO_IN_ADDR, bitd * 6u) >= bitd * 6u) return 3;
    f[1] = (unsigned char)rx_byte(GPIO_IN_ADDR, bitd);
    asm_wait_rise(GPIO_IN_ADDR, bitd * 4u);
    if (asm_wait_fall(GPIO_IN_ADDR, bitd * 6u) >= bitd * 6u) return 3;
    f[2] = (unsigned char)rx_byte(GPIO_IN_ADDR, bitd);
    asm_wait_rise(GPIO_IN_ADDR, bitd * 4u);
    unsigned int len = f[2];
    if (len > 64u) len = 64u;
    unsigned int total = 4u + len + 1u;   /* CMD PAY... CRC */
    for (unsigned int i = 3; i < total; i++) {
        if (asm_wait_fall(GPIO_IN_ADDR, bitd * 6u) >= bitd * 6u) return 3;
        f[i] = (unsigned char)rx_byte(GPIO_IN_ADDR, bitd);
        asm_wait_rise(GPIO_IN_ADDR, bitd * 4u);
    }
    return 1;
}

static unsigned char crc8(const unsigned char *p, unsigned int n)
{
    unsigned char c = 0;
    for (unsigned int i = 0; i < n; i++) {
        c ^= p[i];
        for (int k = 0; k < 8; k++)
            c = (unsigned char)((c & 0x80u) ? ((c << 1) ^ 0x07u) : (c << 1));
    }
    return c;
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
    uart_puts("\r\n[LINKB] v2 M2 protocol link (bitbang g5->g4 @9600)\r\n");

    /* GPIO5 输入; GPIO4 直驱输出空闲高; GPIO10 测试输出脚 */
    GPIO_ENABLE_W1TC = (1u << 5);
    IOMUX_PIN(5) |= (1u << 9);
    IOMUX_PIN(4) |= (1u << 9);
    IOMUX_PIN(10) |= (1u << 9);
    TX4_HI();
    GPIO_ENABLE_W1TS = (1u << 4) | (1u << 10);

    static unsigned char f[80];
    unsigned int seq = 0;
    for (int round = 0; round < 20000; round++) {
        uart_puts("[LB] await burst\r\n");
        if (asm_wait_fall(GPIO_IN_ADDR, 30000000u) >= 30000000u) {
            uart_puts("[LB] timeout\r\n");
            continue;
        }
        unsigned int bitd = asm_wait_rise(GPIO_IN_ADDR, 5000000u);
        if (bitd == 0 || bitd >= 5000000u) {
            uart_puts("[LB] calib noedge\r\n");
            continue;
        }
        drain_burst(bitd);
        uart_puts("[LB] bitd="); put_dec(bitd); uart_puts("\r\n");

        unsigned int st = read_frame(f, bitd);
        if (st != 1) {
            uart_puts("[LB] frame st="); put_dec(st); uart_puts("\r\n");
            continue;
        }
        unsigned int len = f[2];
        if (len > 64u) len = 64u;
        unsigned int total = 4u + len + 1u;
        uart_puts("[LB] frame:");
        for (unsigned int i = 0; i < total; i++) {
            uart_putc(' '); put_hex2(f[i]);
        }
        uart_puts("\r\n");

        /* 帧校验: 头 + CRC (CRC 覆盖 LEN,CMD,PAYLOAD; 存于 f[4+len]) */
        unsigned char resp;
        unsigned char calc = crc8(f + 2, 2u + len);
        if (f[0] != 0xAAu || f[1] != 0x55u || calc != f[4 + len]) {
            resp = 0x16u;   /* CRC/帧头错 NACK */
            uart_puts("[LB] bad frame\r\n");
        } else {
            unsigned int cmd = f[3];
            if (cmd == 0x11u && len >= 2u) {
                unsigned int pin = f[4], lvl = f[5];
                if (pin < 48u) {
                    if (lvl) GPIO_OUT_W1TS = (1u << pin);
                    else GPIO_OUT_W1TC = (1u << pin);
                }
                resp = CMD_ACK;
                uart_puts("[LB] gpio pin="); put_dec(pin);
                uart_puts(" lvl="); put_dec(lvl); uart_puts("\r\n");
            } else if (cmd == 0x12u || cmd == 0x01u) {
                resp = CMD_ACK;
                uart_puts("[LB] hb/qry cmd="); put_hex2(cmd); uart_puts("\r\n");
            } else {
                resp = CMD_NACK;   /* 未知 CMD 不得静默丢弃 */
                uart_puts("[LB] unknown cmd="); put_hex2(cmd); uart_puts("\r\n");
            }
        }

        /* 应答: ACK = AA 55 (1+LEN) 06 原CMD 原PAYLOAD CRC (回显供逐帧核对)
         * NACK = AA 55 01 RESP CRC */
        unsigned char of[24];
        unsigned int olen;
        of[0] = 0xAAu; of[1] = 0x55u;
        if (resp == CMD_ACK) {
            if (len > 16u) len = 16u;
            of[2] = (unsigned char)(1u + len);
            of[3] = CMD_ACK;
            of[4] = f[3];                          /* 回显原 CMD */
            for (unsigned int i = 0; i < len; i++) of[5 + i] = f[4 + i];
            of[5 + len] = crc8(of + 2, 3u + len);  /* LEN,RESP,CMD,PAY */
            olen = 5u + len + 1u;
        } else {
            of[2] = 1u; of[3] = resp;
            of[4] = crc8(of + 2, 2u);
            olen = 5u;
        }
        delay_ms(2);
        for (unsigned int i = 0; i < olen; i++)
            tx_byte(GPIO_IN_ADDR, of[i], bitd);
        seq++;
        uart_puts("[LB] resp="); put_hex2(resp);
        uart_puts(" seq="); put_dec(seq); uart_puts("\r\n");
    }
    for (;;) { delay_ms(1000); }
}
