// Motion task: the LIS2DH12 INT1 edge (EXTI0, also a Stop 2 wake-up source) wakes this task,
// which reads INT1_SRC over SPI2 (releasing the latch) and tells the app task.
#include "app.h"
#include "board.h"
#include "gpio.h"
#include "lis2dh12.h"
#include "rcc.h"
#include "spi.h"

static StaticTask_t tcb;
static StackType_t stack[256];
static TaskHandle_t self;
static bool present;

static const lis2dh12_wake_cfg_t wake_cfg = {
    .odr = LIS2DH12_ODR_10HZ, .fs = LIS2DH12_FS_2G, .threshold_mg = 80U, .duration_ms = 0U};

static int acc_xfer(uint8_t reg, bool read, uint8_t *rx, const uint8_t *tx, size_t len)
{
    uint8_t hdr = lis2dh12_spi_header(reg, read, len);
    gpio_write(ACC_CS_PORT, ACC_CS_PIN, false);
    spi_status_t st = spi_transfer8(ACC_SPI, &hdr, NULL, 1U);
    if (st == SPI_OK) {
        st = spi_transfer8(ACC_SPI, tx, rx, len);
    }
    gpio_write(ACC_CS_PORT, ACC_CS_PIN, true);
    return (st == SPI_OK) ? 0 : -1;
}

static int acc_read(void *ctx, uint8_t reg, uint8_t *data, size_t len)
{
    (void)ctx;
    return acc_xfer(reg, true, data, NULL, len);
}

static int acc_write(void *ctx, uint8_t reg, const uint8_t *data, size_t len)
{
    (void)ctx;
    return acc_xfer(reg, false, NULL, data, len);
}

static const lis2dh12_bus_t bus = {acc_read, acc_write, NULL};

void EXTI0_IRQHandler(void)
{
    EXTI->PR1 = EXTI_PR1_PIF0;
    BaseType_t woken = pdFALSE;
    vTaskNotifyGiveFromISR(self, &woken);
    portYIELD_FROM_ISR(woken);
}

static void hw_init(void)
{
    RCC->APB1ENR1 |= RCC_APB1ENR1_SPI2EN;
    (void)RCC->APB1ENR1;
    gpio_clock_enable(ACC_SPI_PORT);
    gpio_set_af(ACC_SPI_PORT, ACC_SCK_PIN, ACC_SPI_AF);
    gpio_set_af(ACC_SPI_PORT, ACC_MISO_PIN, ACC_SPI_AF);
    gpio_set_af(ACC_SPI_PORT, ACC_MOSI_PIN, ACC_SPI_AF);
    gpio_write(ACC_CS_PORT, ACC_CS_PIN, true);
    gpio_set_mode(ACC_CS_PORT, ACC_CS_PIN, GPIO_MODE_OUTPUT);
    const spi_config_t spi_cfg = {
        .pclk_hz = RCC_PCLK1_HZ, .max_hz = 5000000U, .mode = 3U, .data_bits = 8U, .lsb_first = false};
    (void)spi_init(ACC_SPI, &spi_cfg, NULL);

    // INT1 (active high, latched) -> PA0 -> EXTI0 rising edge
    gpio_clock_enable(ACC_INT_PORT);
    gpio_set_mode(ACC_INT_PORT, ACC_INT_PIN, GPIO_MODE_INPUT);
    gpio_set_pull(ACC_INT_PORT, ACC_INT_PIN, GPIO_PULL_DOWN);
    RCC->APB2ENR |= RCC_APB2ENR_SYSCFGEN;
    (void)RCC->APB2ENR;
    SYSCFG->EXTICR[0] = (SYSCFG->EXTICR[0] & ~SYSCFG_EXTICR1_EXTI0) | SYSCFG_EXTICR1_EXTI0_PA;
    EXTI->RTSR1 |= EXTI_RTSR1_RT0;
    EXTI->PR1 = EXTI_PR1_PIF0;
    EXTI->IMR1 |= EXTI_IMR1_IM0;
    NVIC_SetPriority(EXTI0_IRQn, 6U);
    NVIC_EnableIRQ(EXTI0_IRQn);
}

static void motion_task(void *arg)
{
    (void)arg;
    present = (lis2dh12_probe(&bus) == LIS2DH12_OK) && (lis2dh12_config_wake(&bus, &wake_cfg) == LIS2DH12_OK);
    console_print("motion: LIS2DH12 %s\r\n", present ? "ready (wake-on-motion, 80 mg)" : "not found");
    if (!present) {
        vTaskSuspend(NULL);  // reports keep working on the heartbeat alone
    }
    for (;;) {
        (void)ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        bool moved = false;
        if ((lis2dh12_ack_wake(&bus, &moved) == LIS2DH12_OK) && moved) {
            (void)xTaskNotify(app_task_handle(), EV_MOTION, eSetBits);
        }
    }
}

bool motion_present(void)
{
    return present;
}

void motion_start(void)
{
    self = xTaskCreateStatic(motion_task, "motion", sizeof stack / sizeof stack[0], NULL, PRIO_MOTION, stack, &tcb);
    hw_init();
}
