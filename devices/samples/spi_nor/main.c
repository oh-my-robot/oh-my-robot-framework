/**
 * @file main.c
 * @brief 外部 SPI NOR 真机验证（rm-a/F427：W25Q256JV on SPI4）
 *
 * 接线：PE12=SCK / PE5=MISO / PE6=MOSI / PE4=片选（低有效）。
 *
 * 这类"双 Flash 模块"（板载一颗 SPI NOR + 一颗 SPI NAND，共享 CLK/SI/SO、
 * 各自独立片选）有两个必查项，任一漏掉都会表现为"器件完全不应答"：
 *   - 器件的 /WP 与 /HOLD 必须上拉，否则写入被拒且症状酷似驱动故障；
 *   - **另一颗芯片的片选必须接死到非选中电平**。它悬空时可能被噪声选中，
 *     把共享的 SO 线拉低、与目标器件的 DO 抢总线，目标器件即使正常应答
 *     也一个字节都读不出来，且 SO 线在静态探测下会呈现"被钉在 0V"。
 *
 * 覆盖：
 *   G1 挂载与身份：SPI 从设备挂载（框架配置片选引脚）、器件注册、
 *      识别码读取、几何核对
 *   G2 低地址区擦写读环（< 16MB 与 > 16MB 各一次，后者只能用 4 字节地址访问）
 *   G3 跨页写：单次写跨越页边界，驱动按页拆段
 *   G4 让出：擦除期间 HIGH 优先级心跳不间断（后端等待器件就绪时必须让出 CPU）
 *
 * G0 系列是接线排查探针（引脚分类、位翻转、四线角色穷举），与 G1-G4 的业务
 * 验证分开：G0 用来定位"总线为什么不通"，G1-G4 用来证明"通了之后行为正确"。
 *
 * 安全性：外部器件为本验证专用（无其它消费者），故直接用其尾扇区作为验证区，
 * 不做片内 flash 那套 blank 保护。
 */

#include <string.h>

#include "core/om_init.h"
#include "devices/flash/spi_nor_w25q256jv.h"
#include "drivers/peripheral/flash/pal_flash_dev.h"
#include "drivers/peripheral/serial/log_serial_backend.h"
#include "drivers/peripheral/spi/pal_spi_dev.h"
#include "osal/osal_thread.h"
#include "osal/osal_time.h"
#include "services/log/log.h"

#include "bsp_serial.h" /* BSP_LOG_SERIAL_NAME：板级日志口 */

#if defined(STM32F427xx)
#include "stm32f4xx_hal.h" /* 探针：切 MISO 引脚的弱上下拉 */
#endif

OM_LOG_MODULE(log_spi_nor, OM_LOG_LEVEL_INFO);

static LogSerialBackend g_log_serial_backend;

#if OM_USE_LOG
/** @brief 串口日志后端接线（DRIVER 级注册板级日志口） */
static OmRet spi_nor_verify_log_port_init(void)
{
    return om_log_serial_backend_register(&g_log_serial_backend,
                                          device_find((char *)BSP_LOG_SERIAL_NAME), "serial",
                                          OM_LOG_LEVEL_INFO);
}
OM_INIT_DRIVER(spi_nor_verify_log_port_init);
#endif /* OM_USE_LOG */

/* 验证器件与板级挂载参数 */
#define NOR_NAME "nor0"
#define NOR_CS_CONTROLLER "gpioe"
#define NOR_CS_OFFSET 4u     /* PE4 */
#define NOR_MAX_HZ 20000000u /* SPI4 挂在 90MHz 的 APB2：框架取 <= 20MHz 的分频，实得 11.25MHz */

#define NOR_CAPACITY (32u * 1024u * 1024u)
#define NOR_SECTOR 4096u
#define NOR_PAGE 256u

#define NOR_LOW_ADDR 0x300000u                    /* 3MB：3 字节地址可达区 */
#define NOR_HIGH_ADDR (NOR_CAPACITY - NOR_SECTOR) /* 尾扇区：> 16MB，只能用 4 字节地址 */

/* 心跳周期：G4 的让出判据以它为尺子，两处必须同源 */
#define NOR_HB_PERIOD_MS 200u

static HalSpiDevice g_nor_spi;
static W25q256jvDev g_nor;
static FlashDev *g_flash;
static int g_pass;
static int g_fail;
static volatile uint32_t g_hbCount;

#define CHECK(cond, ...)                                                               \
    do                                                                                 \
    {                                                                                  \
        if (cond)                                                                      \
        {                                                                              \
            g_pass++;                                                                  \
            OM_LOG_INFO("  PASS: " __VA_ARGS__);                                       \
        }                                                                              \
        else                                                                           \
        {                                                                              \
            g_fail++;                                                                  \
            OM_LOG_ERROR("  FAIL: " __VA_ARGS__);                                      \
        }                                                                              \
        osal_sleep_ms(20); /* 输出节流：串口后端非阻塞提交（txFifo 满即静默截断）， */ \
        /* 爆发式输出会被通道吃掉——逐条让出，验证结果才完整 */                         \
    } while (0)

static void spi_nor_verify_heartbeat(void *arg)
{
    (void)arg;
    for (;;)
    {
        g_hbCount++;
        OM_LOG_INFO("hb %u t=%u", (unsigned)g_hbCount, (unsigned)osal_time_now_monotonic());
        osal_sleep_ms(NOR_HB_PERIOD_MS);
    }
}

/* ---- G0 总线取证：不擦不写，只读，把"总线通不通"与"擦除卡不卡"分开 ---- */

static void probe_bus(void)
{
    /* 判定性实验：同样长度、不同首字节的三次单段全双工。
     * 若三次收到的字节完全相同，说明这串数据与器件应答无关（接收通路自产）；
     * 若随命令变化，则器件确实在应答，问题在应答的解析。 */
    static const uint8_t first_bytes[3] = {0x9Fu, 0xFFu, 0x05u};
    static const char *const first_names[3] = {"9F jedec", "FF nocommand", "05 sr1"};
    for (unsigned i = 0u; i < 3u; i++)
    {
        uint8_t tx[8] = {first_bytes[i], 0xFFu, 0xFFu, 0xFFu, 0xFFu, 0xFFu, 0xFFu, 0xFFu};
        uint8_t rx[8];
        /* 哨兵：DMA 若一个字节都没写，这里会原样保留 0xA5A5... */
        memset(rx, 0xA5, sizeof(rx));
        SpiTransfer xfer = {.txBuf = tx, .rxBuf = rx, .len = sizeof(tx), .flags = 0u};
        SpiMessage msg = {.transfers = &xfer, .count = 1u};

        OmRet ret = spi_transfer(&g_nor_spi, &msg);
        uint32_t w0 = (uint32_t)rx[0] | ((uint32_t)rx[1] << 8) | ((uint32_t)rx[2] << 16) |
                      ((uint32_t)rx[3] << 24);
        uint32_t w1 = (uint32_t)rx[4] | ((uint32_t)rx[5] << 8) | ((uint32_t)rx[6] << 16) |
                      ((uint32_t)rx[7] << 24);
        /* 小端打包：w0 的最低字节即流里的第 0 字节 */
        OM_LOG_INFO("probe: raw %s ret=%d w0=%08X w1=%08X", first_names[i], (int)ret,
                    (unsigned)w0, (unsigned)w1);
    }

    /* 多次读识别码：每次打印原始三字节，死总线与活总线在这里就能分开 */
    for (unsigned attempt = 0u; attempt < 3u; attempt++)
    {
        uint32_t id = 0u;
        OmRet ret = w25q256jv_read_jedec_id(&g_nor_spi, &id);
        OM_LOG_INFO("probe %u: jedec ret=%d id=%06X", attempt, (int)ret, (unsigned)id);
    }

    /* 状态寄存器：忙/写使能位。悬空总线上读到的值恒定，活器件会给出确定值 */
    static const uint8_t status_cmds[3] = {0x05u, 0x35u, 0x15u}; /* SR1 / SR2 / SR3 */
    static const char *const status_names[3] = {"SR1", "SR2", "SR3"};
    for (unsigned i = 0u; i < 3u; i++)
    {
        uint8_t cmd = status_cmds[i];
        uint8_t val = 0u;
        OmRet ret = spi_write_then_read(&g_nor_spi, &cmd, 1u, &val, 1u);
        OM_LOG_INFO("probe: %s ret=%d val=%02X", status_names[i], (int)ret, (unsigned)val);
    }
}

/* ---- G0a 长度扫描 ----
 * 同一命令、不同读长：定位从第几字节起数据变成恒定图案。
 * 真实器件的应答随读长连续；若后段恒定，则是接收通路在补旧值。 */
static void probe_length_sweep(void)
{
    static const size_t lens[4] = {1u, 2u, 4u, 8u};

    for (unsigned i = 0u; i < 4u; i++)
    {
        size_t len = lens[i];
        uint8_t tx[9];
        uint8_t rx[9];
        memset(tx, 0xFF, sizeof(tx));
        tx[0] = 0x9Fu;
        memset(rx, 0xA5, sizeof(rx));

        SpiTransfer xfer = {.txBuf = tx, .rxBuf = rx, .len = len, .flags = 0u};
        SpiMessage msg = {.transfers = &xfer, .count = 1u};
        OmRet ret = spi_transfer(&g_nor_spi, &msg);

        uint32_t w0 = (uint32_t)rx[0] | ((uint32_t)rx[1] << 8) | ((uint32_t)rx[2] << 16) |
                      ((uint32_t)rx[3] << 24);
        uint32_t w1 = (uint32_t)rx[4] | ((uint32_t)rx[5] << 8) | ((uint32_t)rx[6] << 16) |
                      ((uint32_t)rx[7] << 24);
        OM_LOG_INFO("probe: len=%u ret=%d w0=%08X w1=%08X", (unsigned)len, (int)ret,
                    (unsigned)w0, (unsigned)w1);
    }
}

/* ---- G0d 长读对照 ----
 * 与短读用同一条 9F 命令，只把读长拉长：短读一直是同一周期图案，
 * 而 256 字节的 flash_read 却能正确往返。这一项把分界找出来。 */
static void probe_long_read(void)
{
    static const size_t lens[3] = {16u, 64u, 256u};

    for (unsigned i = 0u; i < 3u; i++)
    {
        size_t len = lens[i];
        uint8_t tx[256];
        uint8_t rx[256];
        memset(tx, 0xFF, len);
        tx[0] = 0x9Fu;
        memset(rx, 0xA5, len);

        SpiTransfer xfer = {.txBuf = tx, .rxBuf = rx, .len = len, .flags = 0u};
        SpiMessage msg = {.transfers = &xfer, .count = 1u};
        OmRet ret = spi_transfer(&g_nor_spi, &msg);

        uint32_t head = (uint32_t)rx[0] | ((uint32_t)rx[1] << 8) | ((uint32_t)rx[2] << 16) |
                        ((uint32_t)rx[3] << 24);
        uint32_t tail = (uint32_t)rx[len - 4u] | ((uint32_t)rx[len - 3u] << 8) |
                        ((uint32_t)rx[len - 2u] << 16) | ((uint32_t)rx[len - 1u] << 24);
        OM_LOG_INFO("probe: long len=%u ret=%d head=%08X tail=%08X", (unsigned)len, (int)ret,
                    (unsigned)head, (unsigned)tail);
    }
}

/* ---- G0h 传输进行中的引脚观测 ----
 * 同步传输会占住调用线程，没法一边传一边看。改用异步传输：传输跑在总线的
 * worker 线程（优先级高于本线程），本线程在传输进行中轮询引脚与外设状态，
 * 从而回答"片选到底有没有翻转、时钟到底有没有跑"。 */
static volatile int g_asyncDone;

static void probe_async_cb(void *param, SpiMessage *msg)
{
    (void)param;
    (void)msg;
    g_asyncDone = 1;
}

static void probe_wire_activity(void)
{
    static SpiTransfer xfer;
    static SpiMessage msg;
    static uint8_t tx[256];
    static uint8_t rx[256];

    memset(tx, 0xFF, sizeof(tx));
    tx[0] = 0x9Fu;
    memset(rx, 0xA5, sizeof(rx));

    xfer.txBuf = tx;
    xfer.rxBuf = rx;
    xfer.len = sizeof(tx);
    xfer.flags = 0u;
    xfer.speedHz = 0u;
    xfer.bitsPerWord = 0u;
    msg.transfers = &xfer;
    msg.count = 1u;
    msg.transferred = 0u;
    msg.status = OM_OK;

    g_asyncDone = 0;
    OmRet ret = spi_transfer_async(&g_nor_spi, &msg, probe_async_cb, NULL);
    if (ret != OM_OK)
    {
        OM_LOG_INFO("probe: async not accepted -> %d", (int)ret);
        return;
    }

    uint32_t samples = 0u;
    uint32_t cs_low = 0u;
    uint32_t bsy = 0u;
    uint32_t rxne = 0u;
    uint32_t sck_seen = 0u;
    uint32_t last_cr1 = SPI4->CR1;
    while (g_asyncDone == 0 && samples < 4000000u)
    {
        uint32_t idr = GPIOE->IDR;
        uint32_t sr = SPI4->SR;
        if ((idr & 0x0010u) == 0u)
        {
            cs_low++;
        }
        if ((sr & 0x0080u) != 0u) /* BSY */
        {
            bsy++;
        }
        if ((sr & 0x0001u) != 0u) /* RXNE */
        {
            rxne++;
        }
        sck_seen += (uint32_t)((last_cr1 ^ SPI4->CR1) & 0x0004u);
        last_cr1 = SPI4->CR1;
        samples++;
    }

    uint32_t w = (uint32_t)rx[0] | ((uint32_t)rx[1] << 8) | ((uint32_t)rx[2] << 16) |
                 ((uint32_t)rx[3] << 24);
    OM_LOG_INFO("probe: async samples=%u cs_low=%u bsy=%u rxne=%u", (unsigned)samples,
                (unsigned)cs_low, (unsigned)bsy, (unsigned)rxne);
    OM_LOG_INFO("probe: async done=%u status=%d first=%08X", (unsigned)g_asyncDone,
                (int)msg.status, (unsigned)w);
}

/* ---- G0f 外设与 DMA 寄存器快照 ----
 * 只读不写：读 SPI_SR 不会清标志（清 OVR 需要"先读 DR 再读 SR"），
 * 故快照本身不改变被测状态。 */
static void probe_regs(const char *tag)
{
#if defined(STM32F427xx)
    OM_LOG_INFO("regs[%s] SR=%04X CR1=%04X CR2=%04X", tag, (unsigned)SPI4->SR,
                (unsigned)SPI4->CR1, (unsigned)SPI4->CR2);
    OM_LOG_INFO("regs[%s] rx NDTR=%u CR=%08X", tag, (unsigned)DMA2_Stream3->NDTR,
                (unsigned)DMA2_Stream3->CR);
    OM_LOG_INFO("regs[%s] tx NDTR=%u CR=%08X", tag, (unsigned)DMA2_Stream4->NDTR,
                (unsigned)DMA2_Stream4->CR);
    OM_LOG_INFO("regs[%s] dma LISR=%08X HISR=%08X", tag, (unsigned)DMA2->LISR,
                (unsigned)DMA2->HISR);
    /* 片选引脚：MODER 的 bit9:8 应为 01（输出），ODR 的 bit4 在空闲时应为 1 */
    OM_LOG_INFO("regs[%s] gpioe MODER=%08X ODR=%04X IDR=%04X", tag,
                (unsigned)GPIOE->MODER, (unsigned)GPIOE->ODR, (unsigned)GPIOE->IDR);
#else
    (void)tag;
#endif
}

/** 一次短读，附带传输前后的寄存器快照 */
static void probe_short_read_with_regs(const char *tag)
{
    uint8_t cmd = 0x9Fu;
    uint8_t id[4];
    memset(id, 0xA5, sizeof(id));

    probe_regs(tag);
    (void)spi_write_then_read(&g_nor_spi, &cmd, 1u, id, sizeof(id));
    uint32_t w = (uint32_t)id[0] | ((uint32_t)id[1] << 8) | ((uint32_t)id[2] << 16) |
                 ((uint32_t)id[3] << 24);
    OM_LOG_INFO("probe: %s -> %08X", tag, (unsigned)w);
    probe_regs(tag);
}

/* ---- G0e 翻转点定位 ----
 * 反复做同一次短读并记时：坏状态在何时自愈，是时间还是传输次数。 */
static void probe_convergence(void)
{
    /* 循环内不打印：既不丢行，也把"日志通道的 DMA 活动"从被测窗口里剔除。
     * 结果先攒在 RAM，循环结束后再汇总输出。 */
    enum
    {
        N = 48u
    };
    static uint32_t results[N];
    static uint32_t stamps[N];

    for (unsigned round = 0u; round < N; round++)
    {
        uint8_t cmd = 0x9Fu;
        uint8_t id[4];
        memset(id, 0xA5, sizeof(id));
        stamps[round] = osal_time_now_monotonic();
        (void)spi_write_then_read(&g_nor_spi, &cmd, 1u, id, sizeof(id));
        results[round] = (uint32_t)id[0] | ((uint32_t)id[1] << 8) | ((uint32_t)id[2] << 16) |
                         ((uint32_t)id[3] << 24);
    }

    unsigned first_good = N;
    for (unsigned round = 0u; round < N; round++)
    {
        if (results[round] == 0x001940EFu)
        {
            first_good = round;
            break;
        }
    }

    for (unsigned round = 0u; round < N; round++)
    {
        OM_LOG_INFO("probe: r=%u t=%u -> %08X", round, (unsigned)stamps[round],
                    (unsigned)results[round]);
        if (round >= 10u && round != first_good && round + 1u != N)
        {
            round = (first_good != N && first_good > 10u) ? (first_good - 1u) : (N - 2u);
        }
        osal_sleep_ms(6);
    }
    OM_LOG_INFO("probe: first good round = %u of %u", first_good, (unsigned)N);

    /* 静默一段再访问一次：恢复是永久的，还是会衰减回去 */
    osal_sleep_ms(3000);
    uint8_t cmd = 0x9Fu;
    uint8_t id[4];
    memset(id, 0xA5, sizeof(id));
    (void)spi_write_then_read(&g_nor_spi, &cmd, 1u, id, sizeof(id));
    uint32_t w = (uint32_t)id[0] | ((uint32_t)id[1] << 8) | ((uint32_t)id[2] << 16) |
                 ((uint32_t)id[3] << 24);
    OM_LOG_INFO("probe: after 3s idle -> %08X t=%u", (unsigned)w,
                (unsigned)osal_time_now_monotonic());
}

/* ---- G0c 回环自测 ----
 * 发一段特征图案并打印收到的：把 MOSI 与 MISO 短接（可先断开器件），
 * 若收到的与发出的逐字节相同，则 MCU 侧（外设/DMA/引脚/框架）全通，
 * 剩下的问题必在器件或其接线。 */
static void probe_loopback(void)
{
    uint8_t tx[8] = {0xA5u, 0x5Au, 0x3Cu, 0xC3u, 0x0Fu, 0xF0u, 0xAAu, 0x55u};
    uint8_t rx[8];
    memset(rx, 0x00, sizeof(rx));

    SpiTransfer xfer = {.txBuf = tx, .rxBuf = rx, .len = sizeof(tx), .flags = 0u};
    SpiMessage msg = {.transfers = &xfer, .count = 1u};
    OmRet ret = spi_transfer(&g_nor_spi, &msg);

    uint32_t w0 = (uint32_t)rx[0] | ((uint32_t)rx[1] << 8) | ((uint32_t)rx[2] << 16) |
                  ((uint32_t)rx[3] << 24);
    uint32_t w1 = (uint32_t)rx[4] | ((uint32_t)rx[5] << 8) | ((uint32_t)rx[6] << 16) |
                  ((uint32_t)rx[7] << 24);
    /* 短接时 w0 应为 C33C5AA5、w1 应为 55AAF00F（小端打包） */
    OM_LOG_INFO("probe: loopback ret=%d w0=%08X w1=%08X", (int)ret, (unsigned)w0,
                (unsigned)w1);
}

/* ---- G0k 引脚互连矩阵 ----
 * 依次把每根信号线切成输出并翻转，同时读其余三根：同一条网络上的线会同步翻转，
 * 不同网络不会。这是不用万用表的通断实测——直接给出四根线之间的真实拓扑。 */
static void probe_pin_matrix(void)
{
#if defined(STM32F427xx)
    static const uint16_t pins[4] = {GPIO_PIN_4, GPIO_PIN_5, GPIO_PIN_6, GPIO_PIN_12};
    static const char *const names[4] = {"CS/PE4", "MISO/PE5", "MOSI/PE6", "SCK/PE12"};

    for (unsigned i = 0u; i < 4u; i++)
    {
        GPIO_InitTypeDef g = {0};
        g.Mode = GPIO_MODE_INPUT;
        g.Speed = GPIO_SPEED_FREQ_VERY_HIGH;

        g.Pin = pins[i];
        g.Pull = GPIO_PULLUP;
        HAL_GPIO_Init(GPIOE, &g);
        osal_sleep_ms(3);
        uint32_t up = (GPIOE->IDR & pins[i]) ? 1u : 0u;

        g.Pull = GPIO_PULLDOWN;
        HAL_GPIO_Init(GPIOE, &g);
        osal_sleep_ms(3);
        uint32_t down = (GPIOE->IDR & pins[i]) ? 1u : 0u;

        const char *kind;
        if (up == 0u && down == 0u)
        {
            kind = "HARD0=driven-low";
        }
        else if (up == 1u && down == 1u)
        {
            kind = "HARD1=driven-high";
        }
        else
        {
            /* 弱拉能拉动 = 该节点没有强驱动。悬空线与"器件的输入脚"在此无法区分，
             * 别把它读成"接到了器件的输入脚"。 */
            kind = "HIGHZ=float-or-chip-in";
        }
        OM_LOG_INFO("probe: %s up=%u down=%u -> %s", names[i], (unsigned)up, (unsigned)down,
                    kind);
    }

    /* 还原 BSP 的 AF 配置 */
    GPIO_InitTypeDef g = {0};
    g.Mode = GPIO_MODE_AF_PP;
    g.Pull = GPIO_NOPULL;
    g.Alternate = GPIO_AF5_SPI4;
    g.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
    g.Pin = GPIO_PIN_5 | GPIO_PIN_6 | GPIO_PIN_12;
    HAL_GPIO_Init(GPIOE, &g);
    g.Mode = GPIO_MODE_OUTPUT_PP;
    g.Pull = GPIO_NOPULL;
    g.Pin = GPIO_PIN_4;
    HAL_GPIO_Init(GPIOE, &g);
    GPIOE->BSRR = GPIO_PIN_4; /* 片选释放 */
#endif
}

/* ---- G0j 位翻转对照 ----
 * 完全绕开 SPI 外设与 DMA，用 GPIO 手动产生 SCK/CS 并逐位收发。
 * 若位翻转能读到 EF 40 19 而 SPI 外设读不到，则问题在 MCU 侧（外设/DMA）；
 * 若两者都读不到，则问题在器件或接线。 */
#if defined(STM32F427xx)
#define BB_SCK_PIN GPIO_PIN_12 /* PE12 */
#define BB_MOSI_PIN GPIO_PIN_6 /* PE6  */
#define BB_MISO_PIN GPIO_PIN_5 /* PE5  */
#define BB_CS_PIN GPIO_PIN_4   /* PE4  */

static void bb_delay(void)
{
    for (volatile uint32_t i = 0u; i < 60u; i++)
    {
        __NOP();
    }
}

/** 模式 0：空闲低，上升沿采样。MSB 先出 */
static uint8_t bb_xfer(uint8_t out)
{
    uint8_t in = 0u;
    for (uint8_t bit = 0u; bit < 8u; bit++)
    {
        if (out & 0x80u)
        {
            GPIOE->BSRR = BB_MOSI_PIN;
        }
        else
        {
            GPIOE->BSRR = (uint32_t)BB_MOSI_PIN << 16;
        }
        out = (uint8_t)(out << 1);
        bb_delay();
        GPIOE->BSRR = BB_SCK_PIN; /* 上升沿 */
        bb_delay();
        in = (uint8_t)((in << 1) | ((GPIOE->IDR & BB_MISO_PIN) ? 1u : 0u));
        GPIOE->BSRR = (uint32_t)BB_SCK_PIN << 16; /* 下降沿 */
        bb_delay();
    }
    return in;
}

static void probe_bitbang(void)
{
    GPIO_InitTypeDef g = {0};

    /* SCK / MOSI / CS 输出，MISO 输入——全部推挽、无上下拉 */
    g.Mode = GPIO_MODE_OUTPUT_PP;
    g.Pull = GPIO_NOPULL;
    g.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
    g.Pin = BB_SCK_PIN | BB_MOSI_PIN | BB_CS_PIN;
    HAL_GPIO_Init(GPIOE, &g);

    g.Mode = GPIO_MODE_INPUT;
    g.Pin = BB_MISO_PIN;
    HAL_GPIO_Init(GPIOE, &g);

    /* 空闲：SCK 低、片选高 */
    GPIOE->BSRR = (uint32_t)BB_SCK_PIN << 16;
    GPIOE->BSRR = BB_CS_PIN;
    osal_sleep_ms(1);

    GPIOE->BSRR = (uint32_t)BB_CS_PIN << 16; /* 片选拉低 */
    bb_delay();
    (void)bb_xfer(0x9Fu);
    uint8_t b0 = bb_xfer(0xFFu);
    uint8_t b1 = bb_xfer(0xFFu);
    uint8_t b2 = bb_xfer(0xFFu);
    GPIOE->BSRR = BB_CS_PIN; /* 片选拉高 */
    osal_sleep_ms(1);

    /* 还原 BSP 的 AF 配置 */
    g.Mode = GPIO_MODE_AF_PP;
    g.Pull = GPIO_NOPULL;
    g.Alternate = GPIO_AF5_SPI4;
    g.Pin = BB_SCK_PIN | BB_MOSI_PIN | BB_MISO_PIN;
    HAL_GPIO_Init(GPIOE, &g);

    OM_LOG_INFO("probe: bitbang jedec = %02X %02X %02X", (unsigned)b0, (unsigned)b1,
                (unsigned)b2);
}

/* ---- G0n 角色置换扫描 ----
 * 模块上有两颗芯片（NOR 与 NAND），丝印同时给出 SI/SO 与 IO0/IO1 两套命名：哪套
 * 属于哪颗、以及板上四根线是否真如丝印所示，固件侧无从查证。这一扫不再依赖丝印：
 * 把四根线按 (CS, CLK, DI, DO) 的全部 24 种角色分配各试一次 0x9F 读 ID，模式 0 与
 * 模式 3 各扫一遍，共 48 组。若线序或角色与丝印不符，命中项会直接给出真实映射。
 *
 * 全部输出线用开漏 + 内部上拉：若某根线其实是器件的输出脚，开漏只会把它拉低，
 * 不与器件的推挽输出对顶，因此穷举是安全的；采样率约 40kHz，远低于器件上限。
 *
 * 判读：EF 40 19 = 找到真实角色分配；C2 xx = 线上那颗是 SPI NAND；
 *       全部落空 = 四根线里至少有一根不在 W25Q 上（例如 CLK 接到另一颗芯片）。 */

#define ROLE_ALL_PINS (GPIO_PIN_4 | GPIO_PIN_5 | GPIO_PIN_6 | GPIO_PIN_12)

static const uint16_t g_role_pins[4] = {GPIO_PIN_4, GPIO_PIN_5, GPIO_PIN_6, GPIO_PIN_12};
static const char *const g_role_names[4] = {"PE4", "PE5", "PE6", "PE12"};

static void bb_put(uint16_t pin, bool high)
{
    GPIOE->BSRR = high ? (uint32_t)pin : ((uint32_t)pin << 16);
}

/** 开漏线上靠内部上拉回高，边沿按十微秒量级给足（远慢于器件上限） */
static void bb_delay_slow(void)
{
    for (volatile uint32_t i = 0u; i < 400u; i++)
    {
        __NOP();
    }
}

/** 单字节，MSB 先出。模式 0 与模式 3 都在上升沿采样，只是空闲电平不同。 */
static uint8_t bb_xfer_od(uint16_t clk_pin, uint16_t di_pin, uint16_t do_pin, bool idle_high,
                          uint8_t out)
{
    uint8_t in = 0u;
    for (uint8_t b = 0u; b < 8u; b++)
    {
        bb_put(clk_pin, idle_high); /* 时钟回空闲电平 */
        bb_delay_slow();
        bb_put(di_pin, (out & 0x80u) != 0u);
        bb_delay_slow();
        out = (uint8_t)(out << 1u);

        bb_put(clk_pin, idle_high ? false : true); /* 前导沿：模式 0 上升 / 模式 3 下降 */
        bb_delay_slow();
        bb_put(clk_pin, true); /* 采样沿：两种模式都是上升沿 */
        bb_delay_slow();
        in = (uint8_t)((in << 1u) | ((GPIOE->IDR & do_pin) ? 1u : 0u));
    }
    return in;
}

static void probe_role_sweep(void)
{
#if defined(STM32F427xx)
    static const char *const mode_names[2] = {"mode0", "mode3"};
    uint32_t tried = 0u;
    uint32_t hits = 0u;

    for (unsigned mi = 0u; mi < 2u; mi++)
    {
        const bool idle_high = (mi == 1u);

        for (unsigned ci = 0u; ci < 4u; ci++)
        {
            for (unsigned ki = 0u; ki < 4u; ki++)
            {
                for (unsigned ii = 0u; ii < 4u; ii++)
                {
                    for (unsigned oi = 0u; oi < 4u; oi++)
                    {
                        if (ci == ki || ci == ii || ci == oi || ki == ii || ki == oi ||
                            ii == oi)
                        {
                            continue;
                        }

                        const uint16_t cs_pin = g_role_pins[ci];
                        const uint16_t clk_pin = g_role_pins[ki];
                        const uint16_t di_pin = g_role_pins[ii];
                        const uint16_t do_pin = g_role_pins[oi];

                        /* CS/CLK/DI 开漏输出（上拉为释放态），DO 高阻输入 */
                        GPIO_InitTypeDef g = {0};
                        g.Mode = GPIO_MODE_OUTPUT_OD;
                        g.Pull = GPIO_PULLUP;
                        g.Speed = GPIO_SPEED_FREQ_LOW;
                        g.Pin = ROLE_ALL_PINS;
                        HAL_GPIO_Init(GPIOE, &g);
                        GPIOE->BSRR = ROLE_ALL_PINS; /* 全部释放 */

                        g.Mode = GPIO_MODE_INPUT;
                        g.Pull = GPIO_NOPULL;
                        g.Pin = do_pin;
                        HAL_GPIO_Init(GPIOE, &g);

                        bb_put(clk_pin, idle_high);
                        bb_put(cs_pin, true); /* 片选释放 */
                        bb_delay_slow();

                        bb_put(cs_pin, false); /* 片选断言 */
                        bb_delay_slow();
                        (void)bb_xfer_od(clk_pin, di_pin, do_pin, idle_high, 0x9Fu);
                        uint8_t id0 = bb_xfer_od(clk_pin, di_pin, do_pin, idle_high, 0xFFu);
                        uint8_t id1 = bb_xfer_od(clk_pin, di_pin, do_pin, idle_high, 0xFFu);
                        uint8_t id2 = bb_xfer_od(clk_pin, di_pin, do_pin, idle_high, 0xFFu);
                        bb_put(cs_pin, true);
                        bb_delay_slow();

                        tried++;

                        /* 只报有信息量的结果：全 0 与全 F 都是悬空线的常态 */
                        const bool all_zero = (id0 == 0u && id1 == 0u && id2 == 0u);
                        const bool all_ff = (id0 == 0xFFu && id1 == 0xFFu && id2 == 0xFFu);
                        const bool is_nor = (id0 == 0xEFu && id1 == 0x40u);
                        if (is_nor)
                        {
                            hits++;
                        }
                        if (is_nor || (!all_zero && !all_ff))
                        {
                            OM_LOG_INFO("role: %s cs=%s clk=%s di=%s do=%s -> %02X %02X %02X",
                                        mode_names[mi], g_role_names[ci], g_role_names[ki],
                                        g_role_names[ii], g_role_names[oi], (unsigned)id0,
                                        (unsigned)id1, (unsigned)id2);
                        }
                    }
                }
            }
        }
    }

    OM_LOG_INFO("role: sweep done, %u combinations tried, %u jedec hits", (unsigned)tried,
                (unsigned)hits);

    /* 还原 BSP 的 AF 配置 */
    GPIO_InitTypeDef g = {0};
    g.Mode = GPIO_MODE_AF_PP;
    g.Pull = GPIO_NOPULL;
    g.Alternate = GPIO_AF5_SPI4;
    g.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
    g.Pin = GPIO_PIN_5 | GPIO_PIN_6 | GPIO_PIN_12;
    HAL_GPIO_Init(GPIOE, &g);
    g.Mode = GPIO_MODE_OUTPUT_PP;
    g.Pull = GPIO_NOPULL;
    g.Alternate = 0u;
    g.Pin = GPIO_PIN_4;
    HAL_GPIO_Init(GPIOE, &g);
    GPIOE->BSRR = GPIO_PIN_4; /* 片选释放 */
#endif
}
#endif /* STM32F427xx */

/* ---- G0i 供电探测（无万用表时的等效手段）----
 * 把 MISO 临时切成带上拉的普通输入：该脚连着器件的 DO。器件若有电，其输出
 * 级与保护二极管都不导通，上拉把线拉到 3.3V（读 1）；器件若没电，引脚经保护
 * 二极管钳到芯片内部 VCC（≈0V），上拉拉不起来（读 0）。
 * 这只是"有没有电"的定性判定，不是电压表。 */
static void probe_supply(void)
{
#if defined(STM32F427xx)
    GPIO_InitTypeDef g = {0};
    g.Mode = GPIO_MODE_INPUT;
    g.Speed = GPIO_SPEED_FREQ_VERY_HIGH;

    /* 两处都测：DO（PE5，模块可能自带上拉）与 DI（PE6，模块上通常无上拉）。
     * DI 的读数才是可信的钳位判据。 */
    static const uint16_t pins[2] = {GPIO_PIN_5, GPIO_PIN_6};
    for (unsigned i = 0u; i < 2u; i++)
    {
        g.Pin = pins[i];
        g.Pull = GPIO_PULLUP;
        HAL_GPIO_Init(GPIOE, &g);
        osal_sleep_ms(5);
        uint32_t up = (GPIOE->IDR & pins[i]) ? 1u : 0u;

        g.Pull = GPIO_PULLDOWN;
        HAL_GPIO_Init(GPIOE, &g);
        osal_sleep_ms(5);
        uint32_t down = (GPIOE->IDR & pins[i]) ? 1u : 0u;

        /* 上拉 1 / 下拉 0 = 该脚悬空；两者相同 = 被外部钳住
         * （器件未供电时其保护二极管会把脚钳在 VCC≈0V，上拉也拉不起来） */
        OM_LOG_INFO("probe: supply pin%u up=%u down=%u", i == 0u ? 5u : 6u, (unsigned)up,
                    (unsigned)down);
    }

    /* 还原 BSP 的 AF 配置 */
    g.Mode = GPIO_MODE_AF_PP;
    g.Pull = GPIO_NOPULL;
    g.Alternate = GPIO_AF5_SPI4;
    g.Pin = GPIO_PIN_5 | GPIO_PIN_6 | GPIO_PIN_12;
    HAL_GPIO_Init(GPIOE, &g);
#endif
}

/* ---- G0b MISO 悬空判定 ----
 * 给 MISO 引脚切弱上拉/下拉：悬空的输入会随弱拉变，被驱动的输入不会。
 * 这是不需改接线的判定——若两次读数都随拉向改变，说明没有器件在驱动 MISO。 */
static void probe_miso_pull(void)
{
#if defined(STM32F427xx)
    GPIO_InitTypeDef g = {0};
    g.Pin = GPIO_PIN_5;
    g.Mode = GPIO_MODE_AF_PP;
    g.Alternate = GPIO_AF5_SPI4;
    g.Speed = GPIO_SPEED_FREQ_VERY_HIGH;

    static const uint32_t pulls[2] = {GPIO_PULLUP, GPIO_PULLDOWN};
    static const char *const names[2] = {"pull-up", "pull-down"};

    for (unsigned i = 0u; i < 2u; i++)
    {
        g.Pull = pulls[i];
        HAL_GPIO_Init(GPIOE, &g);
        osal_sleep_ms(2);

        uint8_t cmd = 0x9Fu;
        uint8_t id[4] = {0u};
        (void)spi_write_then_read(&g_nor_spi, &cmd, 1u, id, sizeof(id));

        uint32_t w = (uint32_t)id[0] | ((uint32_t)id[1] << 8) | ((uint32_t)id[2] << 16) |
                     ((uint32_t)id[3] << 24);
        OM_LOG_INFO("probe: miso %s -> %08X", names[i], (unsigned)w);
    }

    g.Pull = GPIO_NOPULL; /* 还原 BSP 的配置 */
    HAL_GPIO_Init(GPIOE, &g);
#endif
}

/* ---- G1 挂载与身份 ---- */

static bool verify_attach_and_identity(void)
{
    const SpiDeviceCfg cfg = {
        .csSpec = {.controller = NOR_CS_CONTROLLER, .offset = NOR_CS_OFFSET, .flags = 0u},
        .mode = SPI_MODE_0,
        .maxHz = NOR_MAX_HZ,
        .dataWidth = SPI_DATA_WIDTH_8,
        .bitOrder = SPI_MSB_FIRST,
        .transferOverheadMs = 5u,
    };

    OmRet ret = spi_device_attach(0u, &g_nor_spi, "nor_spi", &cfg);
    CHECK(ret == OM_OK, "spi_device_attach (cs %s.%u) -> %d", NOR_CS_CONTROLLER,
          (unsigned)NOR_CS_OFFSET, (int)ret);
    if (ret != OM_OK)
    {
        return false;
    }

    /* 静默等待再看首次访问是否仍然失败：
     * 仍然失败 = 与"器件/介质需要时间"无关，是访问序列本身的问题。 */
    OM_LOG_INFO("--- G0g silent delay before first access ---");
    osal_sleep_ms(3000);

    /* 挂载后第一件事就是这一段纯重复短读：中间不插任何其它访问 */
    OM_LOG_INFO("--- G0e convergence (clean, nothing else in between) ---");
    probe_convergence();

    /* 早窗口的外设/引脚快照 */
    OM_LOG_INFO("--- G0f regs in the early window ---");
    probe_short_read_with_regs("early");

    /* 早窗口：传输进行中的引脚活动 */
    OM_LOG_INFO("--- G0h wire activity (early) ---");
    probe_wire_activity();

    /* 早窗口：器件到底有没有电 */
    OM_LOG_INFO("--- G0i supply detect (early) ---");
    probe_supply();

    /* 早窗口：位翻转对照（绕开 SPI 外设与 DMA） */
    OM_LOG_INFO("--- G0j bit-banged jedec (early) ---");
#if defined(STM32F427xx)
    probe_bitbang();
#endif

    /* 四根信号线的真实拓扑 */
    OM_LOG_INFO("--- G0k pin interconnection matrix ---");
    probe_pin_matrix();

    /* 不依赖丝印：穷举四根线的 (CS, CLK, DI, DO) 角色分配 */
    OM_LOG_INFO("--- G0n role sweep (24 permutations x mode 0/3) ---");
    probe_role_sweep();

    /* 第一里程碑：不写不擦，一步证明"总线 → 片选 → 器件"整条链路 */
    uint32_t id = 0u;
    ret = w25q256jv_read_jedec_id(&g_nor_spi, &id);
    CHECK(ret == OM_OK, "read jedec id -> %d", (int)ret);
    OM_LOG_INFO("  jedec id = %06X (expect EF4019)", (unsigned)id);
    CHECK(id == 0xEF4019u, "jedec id matches W25Q256JV");

    ret = w25q256jv_register(&g_nor, NOR_NAME, &g_nor_spi);
    CHECK(ret == OM_OK, "w25q256jv_register -> %d", (int)ret);

    g_flash = flash_find(NOR_NAME);
    CHECK(g_flash != NULL, "flash_find(\"%s\")", NOR_NAME);

    const FlashGeometry *g = flash_geometry(g_flash);
    CHECK(g != NULL && g->capacity == NOR_CAPACITY, "capacity = 32MB");
    CHECK(g->sectorSize == NOR_SECTOR && g->pageSize == NOR_PAGE, "4KB sector / 256B page");
    CHECK(g->erasedValue == 0xFFu, "erased value = 0xFF");

    return g_flash != NULL;
}

/* ---- G2 擦写读环（低地址与高地址各一次） ---- */

static void roundtrip_at(uint32_t base, const char *tag)
{
    uint8_t out[NOR_PAGE];
    uint8_t in[NOR_PAGE];

    for (size_t i = 0; i < sizeof(out); i++)
    {
        out[i] = (uint8_t)(base >> 16) + (uint8_t)i;
    }
    memset(in, 0, sizeof(in));

    OM_LOG_INFO("  [%s] step: erase", tag);
    OmRet ret = flash_erase(g_flash, base, NOR_SECTOR);
    CHECK(ret == OM_OK, "[%s] erase 4KB sector @0x%06X -> %d", tag, (unsigned)base, (int)ret);

    OM_LOG_INFO("  [%s] step: program", tag);
    ret = flash_write(g_flash, base, out, sizeof(out));
    CHECK(ret == OM_OK, "[%s] program %u bytes -> %d", tag, (unsigned)sizeof(out), (int)ret);

    OM_LOG_INFO("  [%s] step: read back", tag);
    ret = flash_read(g_flash, base, in, sizeof(in));
    CHECK(ret == OM_OK, "[%s] read back -> %d", tag, (int)ret);
    CHECK(memcmp(out, in, sizeof(out)) == 0, "[%s] content matches", tag);

    /* 擦后须回到擦后值：这也检验了擦除真的生效，而非"写进去看着对" */
    OM_LOG_INFO("  [%s] step: re-erase", tag);
    ret = flash_erase(g_flash, base, NOR_SECTOR);
    CHECK(ret == OM_OK, "[%s] re-erase -> %d", tag, (int)ret);
    memset(in, 0x5A, sizeof(in));
    ret = flash_read(g_flash, base, in, sizeof(in));
    bool all_ff = (ret == OM_OK);
    for (size_t i = 0; all_ff && i < sizeof(in); i++)
    {
        all_ff = (in[i] == 0xFFu);
    }
    CHECK(all_ff, "[%s] sector reads 0xFF after erase", tag);
}

/* ---- G3 跨页写 ---- */

static void verify_cross_page(void)
{
    const uint32_t base = NOR_LOW_ADDR + NOR_SECTOR;
    const uint32_t off = NOR_PAGE - 16u; /* 起于页内偏移 240 */
    uint8_t out[3u * NOR_PAGE];
    uint8_t in[3u * NOR_PAGE];

    for (size_t i = 0; i < sizeof(out); i++)
    {
        out[i] = (uint8_t)(0xA5u ^ (uint8_t)i);
    }
    memset(in, 0, sizeof(in));

    CHECK(flash_erase(g_flash, base, NOR_SECTOR) == OM_OK, "prepare sector for cross-page write");

    /* 300 字节、起于页内 240：跨 3 个页边界，驱动必须自行拆段 */
    OmRet ret = flash_write(g_flash, base + off, out, 300u);
    CHECK(ret == OM_OK, "cross-page program 300 bytes -> %d", (int)ret);

    ret = flash_read(g_flash, base + off, in, 300u);
    CHECK(ret == OM_OK && memcmp(out, in, 300u) == 0,
          "cross-page content matches (no page wrap)");
}

/* ---- G4 让出 ----
 * 等待器件就绪时后端必须让出 CPU（w25q_wait_ready 每次轮询之间 osal_sleep_ms(1)）。
 * 判据要立得住，窗口就必须远长于心跳周期：窗口短于一个周期时，"没有新心跳"既可能
 * 是没让出、也可能只是还没到点，那样的断言不成立。
 * 故一次擦 32 个扇区（128KB，约 1.5s），再按经过时间反推应有几条心跳。 */
static void verify_yield_during_erase(void)
{
    uint32_t before = g_hbCount;
    uint32_t t0 = osal_time_now_monotonic();

    OmRet ret = flash_erase(g_flash, NOR_LOW_ADDR + 2u * NOR_SECTOR, NOR_SECTOR * 32u);

    uint32_t elapsed = osal_time_now_monotonic() - t0;
    uint32_t after = g_hbCount;
    uint32_t expect = elapsed / NOR_HB_PERIOD_MS;

    CHECK(ret == OM_OK, "erase 32 sectors during heartbeat window -> %d", (int)ret);
    CHECK(elapsed > 2u * NOR_HB_PERIOD_MS, "erase window outlasts two heartbeat periods (%ums)",
          (unsigned)elapsed);
    CHECK(after > before, "heartbeat advanced during erase (%u -> %u)", (unsigned)before,
          (unsigned)after);
    /* ±1 条是相位差：心跳相位与窗口边界不必对齐。窗口越长，这一项越不敏感 */
    CHECK(after - before + 1u >= expect,
          "heartbeat kept ticking through the whole wait (%u ticks in %ums, %u due)",
          (unsigned)(after - before), (unsigned)elapsed, (unsigned)expect);
}

static void spi_nor_verify_thread(void *arg)
{
    (void)arg;
    OM_LOG_INFO("=== spi nor verify start ===");

    /* 心跳线程：HIGH 优先级，全程打点。擦除窗口内它必须照常推进——
     * 这是后端"等待器件就绪时让出 CPU"契约的真机证据。 */
    OsalThread *hthread = NULL;
    OsalThreadAttr hattr = {"nor_hb", 2048u, OSAL_PRIO_HIGH_BASE};
    (void)osal_thread_create(&hthread, &hattr, spi_nor_verify_heartbeat, NULL);
    osal_sleep_ms(50);

    if (verify_attach_and_identity())
    {
        osal_sleep_ms(150);
        OM_LOG_INFO("--- G0 bus probe ---");
        probe_bus();
        osal_sleep_ms(150);

        OM_LOG_INFO("--- G0b miso float check ---");
        probe_miso_pull();
        osal_sleep_ms(150);

        OM_LOG_INFO("--- G0a length sweep ---");
        probe_length_sweep();
        osal_sleep_ms(150);

        OM_LOG_INFO("--- G0c loopback (shorted MOSI-MISO echoes back) ---");
        probe_loopback();
        osal_sleep_ms(150);

        OM_LOG_INFO("--- G0d long read ---");
        probe_long_read();
        osal_sleep_ms(150);

        /* 长读之后重跑同一组短读：与首轮相同 = 长度决定；变干净 = 时序/历史决定 */
        OM_LOG_INFO("--- G0a' length sweep (after long reads) ---");
        probe_length_sweep();
        osal_sleep_ms(150);

        OM_LOG_INFO("--- G0e convergence ---");
        probe_convergence();
        osal_sleep_ms(150);

        /* 同一快照放到晚窗口（应当正常）作对照 */
        OM_LOG_INFO("--- G0f regs in the late window ---");
        probe_short_read_with_regs("late");
        osal_sleep_ms(150);

        OM_LOG_INFO("--- G2 low address (3-byte reachable) ---");
        roundtrip_at(NOR_LOW_ADDR, "low");
        osal_sleep_ms(150);

        OM_LOG_INFO("--- G2 high address (4-byte address only) ---");
        roundtrip_at(NOR_HIGH_ADDR, "high");
        osal_sleep_ms(150);

        OM_LOG_INFO("--- G3 cross-page write ---");
        verify_cross_page();
        osal_sleep_ms(150);

        OM_LOG_INFO("--- G4 yield during erase ---");
        verify_yield_during_erase();
    }

    OM_LOG_INFO("=== spi nor verify: %d passed, %d failed ===", g_pass, g_fail);
    for (;;)
    {
        osal_sleep_ms(60000); /* FreeRTOS 任务不得返回：挂起 */
    }
}

static OmRet spi_nor_verify_main(void)
{
    OsalThread *vthread = NULL;
    OsalThreadAttr vattr = {"nor_vfy", 3072u, OSAL_PRIO_NORMAL_BASE};

    (void)osal_thread_create(&vthread, &vattr, spi_nor_verify_thread, NULL);
    return OM_OK;
}
OM_INIT_APPLICATION(spi_nor_verify_main);
