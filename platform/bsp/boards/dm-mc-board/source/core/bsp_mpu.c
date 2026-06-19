#include "bsp_mpu.h"
#include "stm32h7xx_hal.h"

void bsp_mpu_config(void)
{
    /* v1 首轮不启用 DMA/FDCAN，先保留 MPU 入口。
     * 后续打开 DMA/FDCAN 前，在这里加入 non-cacheable buffer 区域。
     */
    HAL_MPU_Disable();
    HAL_MPU_Enable(MPU_PRIVILEGED_DEFAULT);
}
