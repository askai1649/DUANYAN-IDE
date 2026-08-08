/* yl69.c - ESP32-S3 裸机 SAR ADC1 oneshot 探针 (YL-69 @ GPIO6 = ADC1_CH5)
 * 寄存器布局逐项对照 ESP-IDF v5.2 esp32s3/sens_reg.h + adc_ll.h:
 *   SENS 基址 0x60008800; READER1_CTRL=+0x0, MEAS1_CTRL1=+0x8, MEAS1_CTRL2=+0xC,
 *   MEAS1_MUX=+0x10, ATTEN1=+0x14, AMP_CTRL3=+0x20, POWER_XPD_SAR=+0x3C
 * 序列: amp 强制关 -> force_xpd_sar=3(常开) + sarclk_en=1 -> RTC 控制器软件启动
 *       -> CH5 atten=3(11dB, 0~3.3V 全量程) -> 触发 meas1_start_sar 0->1 -> 轮询 done
 */
#define UART0_FIFO   (*(volatile unsigned int *)0x60000000)
#define UART0_STATUS (*(volatile unsigned int *)0x6000001C)

/* RTC_CNTL 看门狗 */
#define RTC_CNTL_WDTWPROTECT (*(volatile unsigned int *)0x600080B0)
#define RTC_CNTL_WDTCONFIG0  (*(volatile unsigned int *)0x60008098)
#define RTC_CNTL_WDTFEED     (*(volatile unsigned int *)0x600080AC)
#define RTC_CNTL_SWD_WPROT   (*(volatile unsigned int *)0x600080B8)
#define RTC_CNTL_SWD_CONF    (*(volatile unsigned int *)0x600080B4)
/* TIMG0 看门狗 */
#define TIMG0_WDTWPROTECT (*(volatile unsigned int *)0x6001F064)
#define TIMG0_WDTCONFIG0  (*(volatile unsigned int *)0x6001F048)
#define TIMG0_INT_CLR     (*(volatile unsigned int *)0x6001F07C)

/* SYSTEM 外设时钟门控 */
#define SYSTEM_PERIP_CLK_EN0 (*(volatile unsigned int *)0x600C0038)

/* SENS 块 (0x60008800) */
#define SENS_BASE           0x60008800u
#define SENS_READER1_CTRL   (*(volatile unsigned int *)(SENS_BASE + 0x00))
#define SENS_MEAS1_CTRL1    (*(volatile unsigned int *)(SENS_BASE + 0x08))
#define SENS_MEAS1_CTRL2    (*(volatile unsigned int *)(SENS_BASE + 0x0C))
#define SENS_MEAS1_MUX      (*(volatile unsigned int *)(SENS_BASE + 0x10))
#define SENS_ATTEN1         (*(volatile unsigned int *)(SENS_BASE + 0x14))
#define SENS_AMP_CTRL3      (*(volatile unsigned int *)(SENS_BASE + 0x20))
#define SENS_POWER_XPD_SAR  (*(volatile unsigned int *)(SENS_BASE + 0x3C))
#define SENS_SLAVE_ADDR4    (*(volatile unsigned int *)(SENS_BASE + 0x4C)) /* 含 SAR_DATE */
#define SENS_MEAS2_CTRL2    (*(volatile unsigned int *)(SENS_BASE + 0x2C))
#define SENS_SLAVE_ADDR1    (*(volatile unsigned int *)(SENS_BASE + 0x40)) /* meas_status[29:22] */
#define SENS_PERI_CLK_GATE  (*(volatile unsigned int *)(SENS_BASE + 0x104)) /* bit30 saradc_clk_en */
#define SENS_PERI_RESET     (*(volatile unsigned int *)(SENS_BASE + 0x108)) /* bit30 saradc_reset */

/* CPU 复位: RTC_CNTL 系统复位 */
#define RTC_CNTL_OPTIONS0    (*(volatile unsigned int *)0x60008000)
#define RTC_CNTL_OPTIONS0_KEY_M 0xFFu /* [31:24] */

/* APB_SARADC 块 (0x60040000) 数字侧 */
#define SARADC_BASE         0x60040000u
#define SARADC_CTRL         (*(volatile unsigned int *)(SARADC_BASE + 0x00))
#define SARADC_CTRL2        (*(volatile unsigned int *)(SARADC_BASE + 0x04))
#define SARADC_SAR1_STATUS  (*(volatile unsigned int *)(SARADC_BASE + 0x10))
#define SARADC_ARB_CTRL     (*(volatile unsigned int *)(SARADC_BASE + 0x38))

/* MEAS1_CTRL2 位域 */
#define MEAS1_EN_PAD_FORCE (1u << 31)
#define MEAS1_START_FORCE  (1u << 18)
#define MEAS1_START_SAR    (1u << 17)
#define MEAS1_DONE_SAR     (1u << 16)
#define MEAS1_DATA_MASK    0xFFFFu
/* MEAS1_CTRL1 amp 强制位域 */
#define AMP_SHORT_REF_GND_FORCE_M (3u << 30)
#define AMP_SHORT_REF_FORCE_M     (3u << 28)
#define AMP_RST_FB_FORCE_M        (3u << 26)
#define FORCE_XPD_AMP_M           (2u << 24) /* 2'b10 = 强制关 */
/* POWER_XPD_SAR */
#define SARCLK_EN           (1u << 31)
#define FORCE_XPD_SAR_M     (3u << 29)
/* MEAS1_MUX */
#define SAR1_DIG_FORCE      (1u << 31)

#define CH 5u /* GPIO6 = ADC1_CH5 */

static void uart_putc(char c)
{
    while (((UART0_STATUS >> 16) & 0x3FF) >= 127) {}
    UART0_FIFO = (unsigned int)c;
}
static void uart_puts(const char *s) { while (*s) uart_putc(*s++); }
static void put_hex(unsigned int v)
{
    uart_puts("0x");
    for (int i = 7; i >= 0; i--) {
        unsigned int d = (v >> (i * 4)) & 0xF;
        uart_putc(d < 10 ? ('0' + d) : ('A' + d - 10));
    }
}
static void put_dec(unsigned int v)
{
    char buf[11]; int n = 0;
    if (v == 0) { uart_putc('0'); return; }
    while (v) { buf[n++] = '0' + (v % 10); v /= 10; }
    while (n) uart_putc(buf[--n]);
}
static void delay_us(unsigned int us)
{
    /* CPU 160MHz, 保守按 10 cyc/us */
    volatile unsigned int n = us * 16u;
    while (n--) {}
}
static void delay_ms(unsigned int ms) { while (ms--) delay_us(1000); }

static void disable_wdts(void)
{
    RTC_CNTL_WDTWPROTECT = 0x50D83AA1u;
    RTC_CNTL_WDTCONFIG0 = 0;
    RTC_CNTL_WDTFEED = 1;
    RTC_CNTL_SWD_WPROT = 0x8F1D312Au;
    RTC_CNTL_SWD_CONF |= (1u << 31); /* SUPER_WDT_DISABLE */
    TIMG0_WDTWPROTECT = 0x50D83AA1u;
    TIMG0_WDTCONFIG0 = 0;
    TIMG0_INT_CLR = 0xFFFFFFFFu;
}

static unsigned int adc1_read(void)
{
    /* 等 RTC FSM 空闲 (IDF: 等 sar_slave_addr1.meas_status==0) */
    unsigned int w = 0;
    while ((SENS_SLAVE_ADDR1 >> 22) & 0xFFu) {
        if (++w > 100000u) break;
    }
    SENS_MEAS1_CTRL2 = (SENS_MEAS1_CTRL2 & ~MEAS1_START_SAR);
    delay_us(20);
    SENS_MEAS1_CTRL2 |= MEAS1_START_SAR;
    unsigned int t = 0;
    while (!(SENS_MEAS1_CTRL2 & MEAS1_DONE_SAR)) {
        if (++t > 100000u) return 0xFFFFFFFFu; /* 超时标记 */
        delay_us(1);
    }
    return SENS_MEAS1_CTRL2 & MEAS1_DATA_MASK;
}

void _start(void) __attribute__((section(".entry"), naked));
void _start(void)
{
    disable_wdts();

    uart_puts("\r\n[YL69] ADC1_CH5 probe start\r\n");

    /* 回读: 确认寄存器块活着 (SAR_DATE 非零 => 基址正确) */
    uart_puts("CLK_EN0="); put_hex(SYSTEM_PERIP_CLK_EN0); uart_puts("\r\n");
    uart_puts("ADDR4=");   put_hex(SENS_SLAVE_ADDR4);     uart_puts("\r\n");
    uart_puts("ARB=");     put_hex(SARADC_ARB_CTRL);       uart_puts("\r\n");

    /* ADC 数字时钟门控保险: SARADC clk_en (bit29) + rst 释放 */
    SYSTEM_PERIP_CLK_EN0 |= (1u << 29);

    /* ★关键: SENS 内部 SARADC 寄存器时钟门 (IDF adc_ll_set_power_manage SW_ON) */
    SENS_PERI_CLK_GATE |= (1u << 30);   /* saradc_clk_en = 1 */
    /* FSM 复位脉冲 (adc_ll_rtc_reset) */
    SENS_PERI_RESET |= (1u << 30);
    SENS_PERI_RESET &= ~(1u << 30);
    uart_puts("GATE="); put_hex(SENS_PERI_CLK_GATE); uart_puts("\r\n");

    /* 1. 关内部 amp (IDF adc_ll_amp_disable 等效) */
    SENS_MEAS1_CTRL1 |= AMP_SHORT_REF_GND_FORCE_M | AMP_SHORT_REF_FORCE_M
                      | AMP_RST_FB_FORCE_M | FORCE_XPD_AMP_M;

    /* 2. 模拟上电: force_xpd_sar=3 (常开), sarclk_en=1 */
    SENS_POWER_XPD_SAR |= FORCE_XPD_SAR_M | SARCLK_EN;

    /* 3. RTC 控制器软件接管 */
    SENS_MEAS1_MUX &= ~SAR1_DIG_FORCE;           /* RTC 路径 */
    SENS_MEAS1_CTRL2 |= MEAS1_START_FORCE | MEAS1_EN_PAD_FORCE;

    /* 4. 通道/衰减: CH5, 11dB (atten=3), 全量程 ~0-3.3V */
    SENS_ATTEN1 = (SENS_ATTEN1 & ~(3u << (CH * 2))) | (3u << (CH * 2));
    SENS_MEAS1_CTRL2 = (SENS_MEAS1_CTRL2 & ~(0xFFFu << 19)) | ((1u << CH) << 19);

    /* 5. 数字侧默认值 (IDF adc_hal_init 等效, 保险起见) */
    SARADC_CTRL |= (1u << 0);  /* start_force=1 */
    SARADC_CTRL &= ~(1u << 1); /* start=0 */
    SARADC_CTRL2 |= (1u << 11); /* timer_sel=1 */
    SARADC_CTRL2 &= ~(1u << 24); /* timer_en=0 (不用定时触发) */

    /* 配置回读 */
    uart_puts("C1=");  put_hex(SENS_MEAS1_CTRL1); uart_puts("\r\n");
    uart_puts("C2=");  put_hex(SENS_MEAS1_CTRL2); uart_puts("\r\n");
    uart_puts("MUX="); put_hex(SENS_MEAS1_MUX);   uart_puts("\r\n");
    uart_puts("ATT="); put_hex(SENS_ATTEN1);      uart_puts("\r\n");
    uart_puts("XPD="); put_hex(SENS_POWER_XPD_SAR); uart_puts("\r\n");
    uart_puts("AMP3="); put_hex(SENS_AMP_CTRL3);  uart_puts("\r\n");
    uart_puts("OK\r\n");

    /* 6. 无限循环采样: 干探头 ~高阻偏 3.3V, 湿探头明显下降 */
    for (int i = 0; ; i++) {
        unsigned int r = adc1_read();
        uart_puts("RAW=");
        if (r == 0xFFFFFFFFu) uart_puts("TIMEOUT"); else put_dec(r);
        uart_puts("\r\n");
        delay_ms(300);
    }
}
