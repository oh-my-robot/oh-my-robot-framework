#ifndef __BSP_CAN_H__
#define __BSP_CAN_H__

#ifdef __cplusplus
extern "C" {
#endif

#include "drivers/peripheral/can/pal_can_dev.h"

#include "stm32f4xx_hal.h"

#define USE_CAN1
#define USE_CAN2

#ifdef USE_CAN1
#define CAN1_REG_PARAMS (CAN_REG_INT_TX | CAN_REG_INT_RX)
#define CAN1_CLOCK      (42000000) // 该值在不同主控上可能不同
#endif

#ifdef USE_CAN2
#define CAN2_REG_PARAMS (CAN_REG_INT_TX | CAN_REG_INT_RX)
#define CAN2_CLOCK      (42000000) // 该值在不同主控上可能不同
#endif

typedef enum {
#ifdef USE_CAN1
    BSP_CAN1_IDX,
#endif
#ifdef USE_CAN2
    BSP_CAN2_IDX
#endif
} CanIdx_e;

typedef struct BspCan *BspCan_t;
typedef struct BspCan {
    CAN_HandleTypeDef handle;
    HalCanHandler parent;
    char *name;
    uint32_t regparams;
} BspCan_s;

extern BspCan_s gBspCan[];

#define BSP_CAN_STATIC_INIT(INSTANCE, NAME, REGPARAMS)                           \
    (BspCan_s)                                                                   \
    {                                                                            \
        .handle.Instance = (INSTANCE), .name = (NAME), .regparams = (REGPARAMS), \
    }
void bsp_can_register(void);

#ifdef __cplusplus
}
#endif

#endif
