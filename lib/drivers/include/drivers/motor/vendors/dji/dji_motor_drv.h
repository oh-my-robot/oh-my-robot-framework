/**
 * @file dji_motor_drv.h
 * @brief RoboMaster DJI ӿ
 * @author Yuhao
 * @date 2025/12/22
 * @details
 * ģ C610/C620/GM6020  DJI Эṩ O(1) ַ
 * ̬͵ԪԼ set_output  bus_sync Ŀʱ
 */

#ifndef __DJI_MOTOR_DRIVER_H__
#define __DJI_MOTOR_DRIVER_H__

#include "dji_motor_conf.h"
#include "drivers/model/device.h"
#include "drivers/peripheral/can/pal_can_dev.h"
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* --- ԶƵò --- */
/** @brief  ID ӳʼֵ */
#define DJI_RX_ID_START DJI_MOTOR_RX_ID_MIN
/** @brief  ID ӳֵ */
#define DJI_RX_ID_END DJI_MOTOR_RX_ID_MAX
/** @brief ֱӳ */
#define DJI_MOTOR_RX_MAP_SIZE (DJI_RX_ID_END - DJI_RX_ID_START + 1)

/* --- Э鳣 --- */
/** @brief C6x0 ID 1~4֡ ID */
#define DJI_TX_ID_C6X0_1_4 0x200
/** @brief Ͽ֡ IDC6x0 ID 5~8 / GM6020 ѹģʽ ID 1~4 */
#define DJI_TX_ID_MIX_1_FF 0x1FF
/** @brief GM6020 ѹģʽID 5~7֡ ID */
#define DJI_TX_ID_GM6020_V_5_7 0x2FF
/** @brief GM6020 ģʽID 1~4֡ ID */
#define DJI_TX_ID_GM6020_C_1_4 0x1FE
/** @brief GM6020 ģʽID 5~7֡ ID */
#define DJI_TX_ID_GM6020_C_5_7 0x2FE

/** @brief ޴ */
#define DJI_ERR_NONE 0x00
/** @brief ڴش */
#define DJI_ERR_MEM 0x01
/** @brief ѹ쳣 */
#define DJI_ERR_VOLT 0x02
/** @brief λ쳣 */
#define DJI_ERR_PHASE 0x03
/** @brief 쳣 */
#define DJI_ERR_SENSOR 0x04
/** @brief Ԥ */
#define DJI_ERR_TEMP_HIGH 0x05
/** @brief ת */
#define DJI_ERR_STALL 0x06
/** @brief 궨쳣 */
#define DJI_ERR_CALIB 0x07
/** @brief  */
#define DJI_ERR_OVERHEAT 0x08

/**
 * @brief DJI 
 */
typedef enum {
    DJI_MOTOR_TYPE_C610 = 0, /**< C610ͶӦ M2006 */
    DJI_MOTOR_TYPE_C620,     /**< C620ͶӦ M3508 */
    DJI_MOTOR_TYPE_GM6020,   /**< GM6020̨ */
    DJI_MOTOR_TYPE_UNKNOWN   /**< δ֪/Ƿռλ */
} DJIMotorType;

/**
 * @brief DJI ģʽ
 */
typedef enum {
    DJI_CTRL_MODE_CURRENT = 0, /**< ģʽ */
    DJI_CTRL_MODE_VOLTAGE      /**< ѹģʽҪ GM6020 */
} DJIMotorCtrlMode;

/* ǰ */
typedef struct DJIMotorTxUnit DJIMotorTxUnit;
typedef struct DJIMotorDrv DJIMotorDrv;

/**
 * @brief ״̬ص
 * @param motor 
 * @param error_code ǰ루0 ʾָ
 */
typedef void (*DJIMotorErrorCallback)(DJIMotorDrv *motor, uint8_t error_code);

/**
 * @brief DJI ͵Ԫ1  TxUnit Ӧ 1 ֡ CAN Ʊģ
 */
struct DJIMotorTxUnit {
    ListHead list; /**< ߷ڵ */
    uint32_t canId;        /**< ÷͵Ԫ󶨵ı׼֡ ID */
    uint8_t txBuffer[8];   /**< 8 ֽڿƸأ 2 ֽһƵ */
    uint8_t isDirty;       /**< ǣ1 ʾи´ */
    uint8_t usageMask;     /**< λռλͼbit0~bit3 Ӧ 4 Ʋ */
};

/**
 * @brief DJI 
 */
struct DJIMotorDrv {
    /**
     * @brief ̬·Ϣעά
     */
    struct
    {
        uint16_t rxId;            /**< ֡ ID */
        uint8_t txBufIdx;         /**<  txBuffer еʼֽ±꣨0/2/4/6 */
        DJIMotorTxUnit *txUnit; /**< Ӧ͵Ԫ */
        DJIMotorType type;      /**<  */
    } link;

    /**
     * @brief ʱ⣨ͨ API ȡ
     */
    struct
    {
        int16_t angle;    /**< Ȧֵ0~8191 */
        int16_t velocity; /**< ת٣RPM */
        int16_t current;  /**< ԭʼ/ѹֵ */
        uint8_t temp;     /**< ¶ȣ϶ȣ */

        uint8_t errorCode;     /**< ǰ */
        uint8_t lastErrorCode; /**< һ֡루ڱش */

        int32_t totalAngle; /**< ȦۼƽǶȱֵ */
        uint16_t lastAngle; /**< һ֡ȦǶ */
        int32_t roundCount; /**< ۼȦ */
    } measure;

    int16_t targetOutput;            /**< ǰĿֵ */
    DJIMotorCtrlMode mode;         /**< ģʽ */
    float scale;                     /**< ֵĻ */
    DJIMotorErrorCallback errorCallback; /**< ״̬ص */
};

/**
 * @brief DJI ߹һ CAN ߶Ӧһʵ
 */
typedef struct
{
    Device* canDev;                                /**< ײ CAN 豸 */
    CanFilterHandle filterHandle;                 /**< ʼʱ䣩 */
    DJIMotorDrv *rxMap[DJI_MOTOR_RX_MAP_SIZE];   /**< O(1)  ID ӳ */
    ListHead txList;                        /**< ֡ */
} DJIMotorBus;

/* --- Core API --- */

/**
 * @brief ʼ DJI ߹
 * @param bus ߹ʵ
 * @param can_dev Ѵ򿪵 CAN 豸
 * @return `OM_OK` ʾɹֵʾʼʧԭ
 */
OmRet dji_motor_bus_init(DJIMotorBus *bus, Device* can_dev);

/**
 * @brief עָ߲շ·
 * @param bus ߹ʵ
 * @param motor ʵ
 * @param type 
 * @param id  ID 1~8÷ΧԼ
 * @param mode ģʽѹ
 * @return `OM_OK` ʾɹ`OM_ERR_CONFLICT` ʾ ID/λͻ
 *         `OM_ERROR_MEMORY` ʾ͵ԪغľֵʾǷ
 */
OmRet dji_motor_register(DJIMotorBus *bus, DJIMotorDrv *motor, DJIMotorType type, uint8_t id, DJIMotorCtrlMode mode);

/**
 * @brief õֵд棬 CAN
 * @param motor 
 * @param output Ŀֵ
 */
void dji_motor_set_output(DJIMotorDrv *motor, int16_t output);

/**
 * @brief ͬϵ֡
 * @param bus ߹ʵ
 */
void dji_motor_bus_sync(DJIMotorBus *bus);

/* --- Error Handling API --- */

/**
 * @brief ô״̬ص
 * @param motor 
 * @param callback صָ
 * @note صΪش仯ʱ
 */
static inline void dji_motor_config_error_callback(DJIMotorDrv *motor, DJIMotorErrorCallback callback)
{
    if (motor)
        motor->errorCallback = callback;
}

/**
 * @brief ״̬¼
 * @param motor 
 * @note Ӳδָһ֡λ
 */
static inline void dji_motor_clear_error(DJIMotorDrv *motor)
{
    if (motor) {
        motor->measure.errorCode = 0;
        motor->measure.lastErrorCode = 0;
    }
}

/* --- Data Access API --- */

/**
 * @brief ȡȦۼƽǶ
 * @param motor 
 * @return Ƕֵλȣ
 */
static inline float dji_motor_get_total_angle(DJIMotorDrv *motor)
{
    if (!motor)
        return 0.0f;
    return (float)motor->measure.totalAngle * (360.0f / 8192.0f);
}

/**
 * @brief ȡȦǶ
 * @param motor 
 * @return Ƕֵλȣ
 */
static inline float dji_motor_get_singgle_angle(DJIMotorDrv *motor)
{
    if (!motor)
        return 0.0f;
    return (float)motor->measure.angle * (360.0f / 8192.0f);
}

/**
 * @brief ȡǰת
 * @param motor 
 * @return ת٣λRPM
 */
static inline float dji_motor_get_velocity(DJIMotorDrv *motor)
{
    if (!motor)
        return 0.0f;
    return (float)motor->measure.velocity;
}

/**
 * @brief ȡǰʵŤص
 * @param motor 
 * @return ֵλA
 */
static inline float dji_motor_get_current(DJIMotorDrv *motor)
{
    if (!motor || motor->link.type >= DJI_MOTOR_TYPE_UNKNOWN)
        return 0.0f;
    return (float)motor->measure.current * motor->scale;
}

/**
 * @brief ȡǰ¶
 * @param motor 
 * @return ¶ȣλ϶ȣ
 */
static inline float dji_motor_get_temp(DJIMotorDrv *motor)
{
    if (!motor)
        return 0.0f;
    return (float)motor->measure.temp;
}

/**
 * @brief ȡǰ
 * @param motor 
 * @return ԭʼֵ
 */
static inline uint8_t dji_motor_get_error_code(DJIMotorDrv *motor)
{
    if (!motor)
        return 0;
    return motor->measure.errorCode;
}

/**
 * @brief ÷״̬ȦǶȣ
 * @param motor 
 * @note  lastAngleһִ֡
 */
static inline void dji_motor_reset_feedback(DJIMotorDrv *motor)
{
    if (motor) {
        motor->measure.totalAngle = 0;
        motor->measure.roundCount = 0;
        motor->measure.angle = 0;
    }
}

#ifdef __cplusplus
}
#endif

#endif /* __DJI_MOTOR_DRIVER_H__ */

