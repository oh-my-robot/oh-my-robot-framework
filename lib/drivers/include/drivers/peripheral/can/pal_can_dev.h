#ifndef __HAL_CAN_CORE_H
#define __HAL_CAN_CORE_H

#ifdef __cplusplus
extern "C" {
#endif

#include "core/data_struct/bitmap.h"
#include "core/data_struct/corelist.h"
#include "drivers/model/device.h"
#include "osal/osal_timer.h"

/* CAN data length definitions */
typedef enum {
    CAN_DLC_0 = 0U, // 0 瀛楄妭
    CAN_DLC_1 = 1U,
    CAN_DLC_2,
    CAN_DLC_3,
    CAN_DLC_4,
    CAN_DLC_5,
    CAN_DLC_6,
    CAN_DLC_7,
    CAN_DLC_8,
    /* 浠ヤ笅浠呯敤浜?CAN FD*/
    CAN_DLC_12 = 9U,
    CAN_DLC_16,
    CAN_DLC_20,
    CAN_DLC_24,
    CAN_DLC_32,
    CAN_DLC_48,
    CAN_DLC_64,
} CanDlc_e;

/* CAN 鍚屾璺宠浆瀹藉害 */
typedef enum {
    CAN_SYNCJW_1TQ = 0U,
    CAN_SYNCJW_2TQ,
    CAN_SYNCJW_3TQ,
    CAN_SYNCJW_4TQ
} CanSjw_e;

typedef enum {
    CAN_TSEG1_1TQ = 0U,
    CAN_TSEG1_2TQ,
    CAN_TSEG1_3TQ,
    CAN_TSEG1_4TQ,
    CAN_TSEG1_5TQ,
    CAN_TSEG1_6TQ,
    CAN_TSEG1_7TQ,
    CAN_TSEG1_8TQ,
    CAN_TSEG1_9TQ,
    CAN_TSEG1_10TQ,
    CAN_TSEG1_11TQ,
    CAN_TSEG1_12TQ,
    CAN_TSEG1_13TQ,
    CAN_TSEG1_14TQ,
    CAN_TSEG1_15TQ,
    CAN_TSEG1_16TQ,
    CAN_TSEG1_MAX,
} CanBS1_e;

typedef enum {
    CAN_TSEG2_1TQ = 0U,
    CAN_TSEG2_2TQ,
    CAN_TSEG2_3TQ,
    CAN_TSEG2_4TQ,
    CAN_TSEG2_5TQ,
    CAN_TSEG2_6TQ,
    CAN_TSEG2_7TQ,
    CAN_TSEG2_8TQ,
    CAN_TSEG2_MAX,
} CanBS2_e;

typedef enum {
    CAN_BAUD_10K  = 50000U,
    CAN_BAUD_20K  = 250000U,
    CAN_BAUD_50K  = 500000U,
    CAN_BAUD_100K = 1000000U,
    CAN_BAUD_125K = 1250000U,
    CAN_BAUD_250K = 2500000U,
    CAN_BAUD_500K = 5000000U,
    CAN_BAUD_800K = 8000000U,
    CAN_BAUD_1M   = 10000000U,
} CanBaudRate_e;

/* CAN 宸ヤ綔妯″紡 */
typedef enum {
    CAN_WORK_NORMAL = 0U,     // 姝ｅ父妯″紡锛氬悜鎬荤嚎鍙戦€侊紝浠庢€荤嚎鎺ユ敹
    CAN_WORK_LOOPBACK,        // 鐜洖妯″紡锛氬悜鎬荤嚎鍜屾湰鏈哄彂閫侊紝涓嶆帴鍙楁€荤嚎淇″彿
    CAN_WORK_SILENT,          // 闈欓粯妯″紡锛氳嚜鍙戣嚜鏀讹紝涓嶅悜鎬荤嚎鍙戦€侊紝浣嗘帴鏀舵€荤嚎淇″彿
    CAN_WORK_SILENT_LOOPBACK, // 闈欓粯鐜洖妯″紡锛氳嚜鍙戦€佽嚜鏀讹紝涓嶅悜鎬荤嚎鍙戦€侊紝涓嶆帴鏀舵€荤嚎淇″彿
} CanWorkMode_e;

/* CAN node error status */
typedef enum CanNodeStatus {
    CAN_NODE_STATUS_ACTIVE,  // CAN 涓诲姩閿欒
    CAN_NODE_STATUS_PASSIVE, // CAN 琚姩閿欒
    CAN_NODE_STATUS_BUSOFF,  // CAN 绂荤嚎
} CanNodeErrStatus_e;

/* CAN filter mode */
typedef enum {
    CAN_FILTER_MODE_LIST, // 婊ゆ尝鍣ㄦā寮?鍒楄〃妯″紡
    CAN_FILTER_MODE_MASK, // 婊ゆ尝鍣ㄦā寮?鎺╃爜妯″紡
} CanFilterMode_e;

/* CAN ID鏍囪瘑*/
typedef enum {
    CAN_IDE_STD = 0U,
    CAN_IDE_EXT,
} CanIdType_e;

typedef enum {
    CAN_FILTER_ID_STD = 0U, // 浠呰繃婊ゆ爣鍑咺D
    CAN_FILTER_ID_EXT,      // 浠呰繃婊ゆ墿灞旾D
    CAN_FILTER_ID_STD_EXT,  // 杩囨护鏍囧噯ID鍜屾墿灞旾D
} CanFilterIdType_e;

/* CAN鎶ユ枃绫诲瀷 */
typedef enum {
    CAN_MSG_TYPE_DATA = 0U, // 鏁版嵁鎶ユ枃
    CAN_MSG_TYPE_REMOTE     // 杩滅▼鎶ユ枃
} CanMsgType_e;

/* CAN FD bitrate switch flag */
typedef enum {
    CANFD_MSG_BRS_OFF = 0U,
    CANFD_MSG_BRS_ON
} CanFdMsgBrs_e;

typedef enum {
    CAN_MSG_CLASS = 0U,
    CAN_MSG_FD,
} CanFdMsgFormat_e;

typedef enum {
    CANFD_MSG_ESI_ACITVE,
    CANFD_MSG_ESI_PASSIVE,
} CanFdESI_e;

/* CAN RX-side software error status */
typedef enum {
    CAN_ERR_NONE = 0U,
    CAN_ERR_FILTER_EMPTY, // 婊ゆ尝鍣ㄤ负绌?
    CAN_ERR_FILTER_BANK,  // 婊ゆ尝鍣ㄧ紪鍙烽敊璇?
    CAN_ERR_UNKNOWN,      // 鏈煡閿欒

    // FIFO鐩稿叧閿欒
    CAN_ERR_FIFO_EMPTY,
    CAN_ERR_SOFT_FIFO_OVERFLOW, // FIFO婧㈠嚭

} CanErrorCode_e;

/* CAN涓柇浜嬩欢  */
typedef enum {
    CAN_ISR_EVENT_INT_RX_DONE = 0U, // 鎺ユ敹涓柇
    CAN_ISR_EVENT_INT_TX_DONE,      // 鍙戦€佷腑鏂?
} CanIsrEvent_e;

typedef enum {
    CAN_ERR_EVENT_NONE,
    CAN_ERR_EVENT_RX_OVERFLOW      = 1U,      // 鎺ユ敹FIFO婧㈠嚭
    CAN_ERR_EVENT_TX_FAIL          = 1U << 1, // 鍙戦€佸け璐?
    CAN_ERR_EVENT_ARBITRATION_FAIL = 1U << 2, // 浠茶澶辫触
    CAN_ERR_EVENT_BUS_STATUS       = 1U << 3, // 鎬荤嚎鐘舵€佹敼鍙橈紝涓诲姩->琚姩->绂荤嚎

    // 閿欒璁℃暟杞Щ鍒扮‖浠跺眰鎵ц

    // CAN_ERR_EVENT_CRC_ERROR = 1U << 4,        // CRC閿欒
    // CAN_ERR_EVENT_FORMAT_ERROR = 1U << 5,     // 鏍煎紡閿欒
    // CAN_ERR_EVENT_STUFF_ERROR = 1U << 6,      // 浣嶅～鍏呴敊璇?
    // CAN_ERR_EVENT_BITDOMINANT_ERROR = 1U << 7, // 浣嶆樉鎬ч敊璇?
    // CAN_ERR_EVENT_BITRECESSIVE_ERROR = 1U << 8, // 浣嶉殣鎬ч敊璇?
    // CAN_ERR_EVENT_ACK_ERROR = 1U << 9,        // 纭閿欒
} CanErrEvent_e;

typedef uint16_t CanFilterHandle_t;

/**
 * @defgroup CAN_TX_MODE_DEF
 *  @brief CAN鍙戦€佹ā寮忓畾涔?
 *  @{
 */
#define CAN_TX_MODE_UNOVERWRTING (0U) // 涓嶈鍐欐ā寮忥紝鍗充笉瑕嗙洊鑰佹暟鎹?
#define CAN_TX_MODE_OVERWRITING  (1U) // 瑕嗗啓妯″紡锛屽嵆瑕嗙洊鑰佹暟鎹?
/**
 * @defgroup CAN_TX_MODE_DEF
 * @}
 */

/**
 * @defgroup CAN_FILTER_CFG_DEF
 * @brief CAN婊ゆ尝鍣ㄩ厤缃弬鏁板畾涔?
 * @{
 */
#define CAN_FILTER_REQUEST_INIT(_mode, _idType, _id, _mask, _rx_callback, _param)                                          \
    (CanFilterRequest_s)                                                                                                    \
    {                                                                                                                       \
        .workMode = _mode, .idType = _idType, .id = _id, .mask = _mask, .rxCallback = _rx_callback, .param = _param \
    }
/**
 * @defgroup CAN_FILTER_CFG_DEF
 * @}
 */

/**
 * @defgroup CAN_REG_DEF
 * @brief CAN鏀跺彂 娉ㄥ唽鍙傛暟瀹氫箟
 * @{
 */
#define CAN_REG_INT_TX        DEVICE_REG_INT_TX     // 涓柇鍙戦€?
#define CAN_REG_INT_RX        DEVICE_REG_INT_RX     // 涓柇鎺ユ敹
#define CAN_REG_IS_STANDALONG DEVICE_REG_STANDALONG // CAN 鐙崰璁惧
/**
 * @defgroup CAN_REG_DEF
 * @}
 */

/**
 * @brief CAN鎵撳紑鍙傛暟瀹氫箟
 * @{
 */
#define CAN_O_INT_TX DEVICE_O_INT_TX // 涓柇鍙戦€?
#define CAN_O_INT_RX DEVICE_O_INT_RX // 涓柇鎺ユ敹
                                     /**
                                      * @brief CAN鎵撳紑鍙傛暟瀹氫箟
                                      * @}
                                      */

/**
 * @defgroup CAN_CMD_DEF
 * @brief CAN鍛戒护瀹氫箟
 * @{
 */
#define CAN_CMD_CFG                  (0x00U) // 閰嶇疆CAN
#define CAN_CMD_SUSPEND              (0x01U) // 鏆傚仠CAN
#define CAN_CMD_RESUME               (0x02U) // 鎭㈠CAN
#define CAN_CMD_SET_IOTYPE           (0x03U) // 璁剧疆CAN IO绫诲瀷锛屼娇鑳戒腑鏂瓑
#define CAN_CMD_CLR_IOTYPE           (0x04U) // 娓呴櫎CAN IO绫诲瀷锛屽叧闂腑鏂瓑
#define CAN_CMD_CLOSE                (0x05U) // 鍏抽棴CAN
#define CAN_CMD_FLUSH                (0x06U) // 娓呯┖缂撳瓨
#define CAN_CMD_FILTER_ALLOC         (0x07U) // 鐢宠骞舵縺娲绘护娉㈠櫒
#define CAN_CMD_FILTER_FREE          (0x08U) // 閲婃斁婊ゆ尝鍣?
#define CAN_CMD_START                (0x09U) // 鍚姩CAN
#define CAN_CMD_GET_STATUS           (0x0AU) // 鑾峰彇 CAN 鐘舵€?
#define CAN_CMD_GET_CAPABILITY       (0x0BU) // 鑾峰彇CAN纭欢鑳藉姏
/**
 * @defgroup CAN_CMD_DEF
 * @}
 */

/**
 * @defgroup CAN_MSG_DSC_STATIC_INIT_DEF
 * @brief CAN甯ф弿杩扮鍒濆鍖栧畾涔?
 * @param id: CAN娑堟伅ID
 * @param idType: CAN娑堟伅ID绫诲瀷
 * @param len: CAN鏁版嵁闀垮害
 * @return CanMsgDsc_s
 * @{
 */
/**
 * @brief CAN鏁版嵁甯ф弿杩扮鍒濆鍖?
 */
#define CAN_DATA_MSG_DSC_INIT(_id, _idType, _len)                                   \
    (CanMsgDsc_s)                                                                   \
    {                                                                               \
        .id = _id, .idType = _idType, .msgType = CAN_MSG_TYPE_DATA, .dataLen = _len \
    }

/**
 * @brief CAN杩滅▼甯ф弿杩扮鍒濆鍖?
 */
#define CAN_REMOTE_MSG_DSC_INIT(_id, _idType, _len)                                   \
    (CanMsgDsc_s)                                                                     \
    {                                                                                 \
        .id = _id, .idType = _idType, .msgType = CAN_MSG_TYPE_REMOTE, .dataLen = _len \
    }
/**
 * @defgroup CAN_MSG_DSC_STATIC_INIT_DEF
 * @}
 */

typedef struct CanErrCounter *CanErrCounter_t;
typedef struct CanErrCounter {
    // 杞欢锛堥€昏緫/缁熻锛夌浉鍏?
    size_t rxMsgCount;        // CAN鎺ユ敹鎶ユ枃鏁伴噺
    size_t txMsgCount;        // CAN鍙戦€佹姤鏂囨暟閲?
    size_t rxSoftOverFlowCnt; // CAN鎺ユ敹杞欢FIFO婧㈠嚭璁℃暟鍣紝璁板綍浜嗚蒋浠禙IFO婧㈠嚭鐨勬鏁?
    size_t rxFailCnt;         // CAN鎺ユ敹澶辫触璁℃暟鍣紝鍖呮嫭鎵€鏈夎蒋浠跺拰纭欢鎺ユ敹澶辫触鐨勬€绘鏁?
    size_t
        txSoftOverFlowCnt; // CAN鍙戦€佽蒋浠禙IFO婧㈠嚭璁℃暟鍣紝鍦ㄨ鍐欐ā寮忎笅浠ｈ〃鐨勬槸琚鐩栫殑娑堟伅鏁伴噺锛屽湪闈炶鍐欐ā寮忎笅浠ｈ〃鐨勬槸瓒呭嚭FIFO瀹归噺鐨勬秷鎭暟閲?
    size_t txFailCnt;      // CAN鍙戦€佸け璐ヨ鏁板櫒锛屾墍鏈夊簲璇ュ彂閫佸嚭鍘讳絾鏄嵈娌″彂鍑哄幓鐨勬秷鎭紝閮戒細瑙﹀彂txFail閿欒
    // CAN 纭欢鐩稿叧
    size_t txArbitrationFailCnt; // CAN鍙戦€佷徊瑁佸け璐ヨ鏁板櫒
    size_t rxHwOverFlowCnt;      // CAN鎺ユ敹纭欢FIFO婧㈠嚭璁℃暟鍣?
    size_t txMailboxFullCnt;     // CAN鍙戦€佺‖浠堕偖绠辨弧璁℃暟鍣?
    // CAN 瑙勮寖涓殑閿欒锛岃鎯呰鍙傝€僀AN鍗忚鍏充簬閿欒澶勭悊鐨勮鏄?
    size_t rxErrCnt;           // CAN鎺ユ敹閿欒璁℃暟鍣紙REC锛?
    size_t txErrCnt;           // CAN鍙戦€侀敊璇鏁板櫒锛圱EC锛?
    size_t bitDominantErrCnt;  /**
                                * @brief CAN浣嶆樉鎬ч敊璇鏁板櫒 褰撹妭鐐硅瘯鍥惧彂閫佷竴涓殣鎬т綅(閫昏緫
                                * 1)锛屼絾鍦ㄦ€荤嚎涓婂洖璇诲埌鐨勫嵈涓烘樉鎬т綅(閫昏緫 0)鏃讹紝灏变細瑙﹀彂杩欎釜閿欒
                                * @note 浠茶娈靛拰搴旂瓟娈靛厑璁稿彂鐢熻鎯呭喌锛屼絾鏄湪鍏朵粬娈靛氨浼氳Е鍙戣閿欒
                                * @note 瑙﹀彂鏉′欢锛氬畠閫氬父浠ｈ〃鐫€鈥滄湁浜烘崳涔扁€濓紝濡傦細
                                *       1. ID鍐茬獊锛堟渶甯歌锛?2. 纭欢骞叉壈鎴栫煭璺?
                                *       3. 娉㈢壒鐜囦笉鍖归厤      4. 鏀跺彂鍣ㄦ崯鍧忔垨渚涚數涓嶇ǔ(璇锋鏌?V渚涚數)
                                */
    size_t bitRecessiveErrCnt; /**
                                * @brief CAN浣嶉殣鎬ч敊璇鏁板櫒 褰撹妭鐐硅瘯鍥惧悜鎬荤嚎鍙戦€佷竴涓樉鎬т綅(閫昏緫
                                * 0)锛屼絾鍦ㄥ洖璇绘€荤嚎鏃讹紝璇诲埌鐨勫嵈涓洪殣鎬т綅 (閫昏緫 1)鏃讹紝灏变細瑙﹀彂杩欎釜閿欒
                                * @note 閫氬父鍙戠敓鍦⊿tart Bit銆丄rbitration Field銆丆ontrol Field 涓?Data Field
                                * 涓紝瑙﹀彂璇ラ敊璇€氬父鏄‖浠舵湁闂锛屽:
                                *       1. 鐗╃悊杩炴帴鏂矾锛堟渶甯歌锛? 2. 鏀跺彂鍣ㄥ浜庘€滈潤榛樷€濇垨鈥滃緟鏈衡€濇ā寮?
                                *       3. 鏀跺彂鍣ㄤ緵鐢靛紓甯?璇锋鏌?V渚涚數)         4. 纭欢鐭矾
                                *       5. GPIO 寮曡剼閰嶇疆閿欒
                                */
    size_t formatErrCnt;       // CAN鏍煎紡閿欒璁℃暟鍣?
    size_t crcErrCnt;          // CAN CRC 閿欒璁℃暟鍣?
    size_t ackErrCnt;          // CAN 搴旂瓟閿欒璁℃暟鍣?
    size_t stuffErrCnt;        // CAN浣嶅～鍏呴敊璇鏁板櫒
} CanErrCounter_s;

/*
    CAN鐨勬瘡涓€涓猙it鏃堕棿琚粏鍒嗕负锛歋S娈碉紝Prop娈碉紝Phase1娈碉紝Phase2娈?
    鍏朵腑SS娈靛浐瀹氫负1浣嶏紝鍏朵綑娈靛彲缂栫▼
    鏇村CAN鐭ヨ瘑璇﹁https://www.canfd.net/canbasic.html
*/
typedef struct CanBitTime {
    CanBS1_e bs1;           // 浼犳挱娈?+ 鐩镐綅缂撳啿娈?
    CanBS2_e bs2;           // 鐩镐綅缂撳啿娈?
    CanSjw_e syncJumpWidth; // 鍚屾璺宠浆瀹藉害锛岃缃瘡娆″啀鍚屾鐨勬闀?
} CanBitTime_s;

/*
    瀵逛簬CAN鑰岃█锛岄€氬父baudrate >= 500k鏃讹紝閲囨牱浣嶈缃负0.8
    0 < baudrate <= 500k 鏃讹紝閲囨牱鐜囪缃负0.75
*/
typedef struct CanTimeCfg *CanTimeCfg_t;
typedef struct CanTimeCfg {
    CanBaudRate_e baudRate;
    uint16_t psc; // 棰勫垎棰戠郴鏁?
    CanBitTime_s bitTimeCfg;
} CanTimeCfg_s;

/*  CAN婊ゆ尝鍣ㄩ厤缃粨鏋勪綋 */
typedef struct CanFilterRequest *CanFilterRequest_t;
typedef struct CanFilterRequest {
    CanFilterMode_e workMode;
    CanFilterIdType_e idType;
    uint32_t id;
    uint32_t mask;
    void (*rxCallback)(Device_t dev, void *param, CanFilterHandle_t handle, size_t msg_count);
    void *param;
} CanFilterRequest_s;

typedef struct CanFilterAllocArg *CanFilterAllocArg_t;
typedef struct CanFilterAllocArg {
    CanFilterRequest_s request;
    CanFilterHandle_t handle;
} CanFilterAllocArg_s;

typedef struct CanHwFilterCfg *CanHwFilterCfg_t;
typedef struct CanHwFilterCfg {
    size_t bank;
    CanFilterMode_e workMode;
    CanFilterIdType_e idType;
    uint32_t id;
    uint32_t mask;
} CanHwFilterCfg_s;

/* CAN杩囨护鍣ㄧ粨鏋勪綋 */
typedef struct CanFilter *CanFilter_t;
typedef struct CanFilter {
    CanFilterRequest_s request;              // CAN杩囨护鍣ㄩ厤缃?
    ListHead_s msgMatchedList; // 鍖归厤璇ヨ繃婊ゅ櫒鐨勬姤鏂囬摼琛?
    uint32_t msgCount;               // 璇ユ护娉㈠櫒鎺ユ敹鍒扮殑娑堟伅鏁伴噺
    uint8_t isActived;               // 璇ユ护娉㈠櫒鏄惁婵€娲?
} CanFilter_s;

/**
 * @brief CAN鎶ユ枃鎻忚堪绗︾粨鏋勪綋
 * @note 璇ョ粨鏋勪綋鐢ㄤ簬鎻忚堪CAN鎶ユ枃鐨勫熀鏈俊鎭紝鍖呮嫭ID銆両D绫诲瀷銆佹姤鏂囩被鍨嬨€佹暟鎹暱搴︺€佹帴鏀舵椂闂存埑绛?
 * @note
 */
typedef struct CanMsgDsc *CanMsgDsc_t;
typedef struct CanMsgDsc {
    uint32_t id : 29;         // 甯D
    CanIdType_e idType : 1;   // ID鏍囪瘑
    CanMsgType_e msgType : 1; // 鎶ユ枃绫诲瀷
    uint32_t reserved : 1;    // Reserved bit, pads this packed group to 32 bits.
    uint32_t dataLen;         // Payload length, see @ref CanDlc_e.
    uint32_t timeStamp;       // 鎶ユ枃鎺ユ敹/鍙戦€佹椂闂存埑
} CanMsgDsc_s;

/* CAN user message, user-facing */
typedef struct CanUserMsg *CanUserMsg_t;
typedef struct CanUserMsg {
    CanMsgDsc_s dsc; /**
                      * @brief CAN鎶ユ枃鎻忚堪绗?
                      * @ref CAN_MSG_DSC_STATIC_INIT_DEF
                      */
    CanFilterHandle_t filterHandle;    // 杩囨护鍣ㄥ彞鏌?鍙戦€侀偖绠辩紪鍙凤紝-1 琛ㄧず鏈粦瀹氫换浣曟护娉㈠櫒鎴栧彂閫侀偖绠?
    /*
        鏁版嵁鎸囬拡锛屾牴鎹墍澶勫眰绾т笉鍚岋紝鏈変笉鍚岀殑鍚箟
        搴旂敤灞傦細
        1.
        CanUserMsg 浣滀负 read 鎺ュ彛鐨?buf 鍙傛暟鏃讹紝搴旀寚鍚戝簲鐢ㄥ眰缂撳瓨鍖猴紝鏁版嵁浠?RX FIFO 瀹瑰櫒鎷疯礉鍒?pUserBuf 涓€?
        2.
        CanUserMsg 浣滀负 write 鎺ュ彛鐨?data 鍙傛暟鏃讹紝搴旀寚鍚戝簲鐢ㄥ眰鏁版嵁婧愶紝鏁版嵁浠?pUserBuf 鎷疯礉鍒?TX FIFO 瀹瑰櫒涓€?
        妗嗘灦灞傦細
        1. CanUserMsg 浣滀负妗嗘灦灞?msgListBuffer 涓殑 UserMsg锛屽缁堟寚鍚戣嚜韬殑 container 瀛楁銆?
        纭欢搴曞眰锛?
        CanUserMsg 浣滀负纭欢鎺ユ敹鎶ユ枃鏃讹紝userBuf 鎸囧悜纭欢鎻愪緵鐨勬暟鎹湴鍧€锛屽湪 ISR 涓浆瀛樺埌 RX FIFO 瀹瑰櫒銆?
    */
    uint8_t *userBuf;
} CanUserMsg_s;

/* CAN 纭欢娑堟伅锛屼緵 CAN core/BSP 浣跨敤 */
typedef struct CanHwMsg *CanHwMsg_t;
typedef struct CanHwMsg {
    CanMsgDsc_s dsc;
    int16_t hwFilterBank;
    int8_t hwTxMailbox;
    uint8_t *data;
} CanHwMsg_s;

/* CAN message list structure */
typedef struct CanMsgList *CanMsgList_t;
typedef struct CanMsgList {
    CanUserMsg_s userMsg;             // 鐢ㄦ埛灞傛姤鏂?
    ListHead_s fifoListNode;    // Fifo閾捐〃鑺傜偣锛岄摼鎺xFifo鐨剈sed/free閾捐〃
    ListHead_s matchedListNode; // 鍖归厤閾捐〃鑺傜偣锛岄摼鎺ュ埌婊ゆ尝鍣ㄥ尮閰嶉摼琛ㄦ垨鏄彂閫侀偖绠辩殑fifoListNode
    void *owner;                      // 鎶ユ枃鎵€灞炶€咃紝婊ゆ尝鍣ㄦ垨鍙戦€侀偖绠?
    uint8_t container[0];             // 鏌旀€ф暟缁勶紝灏嗘鏋跺眰瀹為檯鍐呭瓨锛堝CanMsgList_s鍜孋anFdList_s锛夋槧灏勫埌container瀛楁
} CanMsgList_s;

/* CAN RX FIFO structure */
typedef struct CanMsgFifo *CanMsgFifo_t;
typedef struct CanMsgFifo {
    void *msgBuffer;           // 鎶ユ枃缂撳啿鍖?
    ListHead_s usedList; // 宸蹭娇鐢ㄧ殑鎶ユ枃鍒楄〃
    ListHead_s freeList; // 绌洪棽鐨勬姤鏂囧垪琛?
    uint32_t freeCount;        // 绌洪棽鐨勬姤鏂囨暟閲?
    uint8_t isOverwrite;       // FIFO 瑕嗗啓绛栫暐锛? 涓嶈鍐欙紝1 瑕嗗啓
} CanMsgFifo_s;

typedef struct CanMailbox *CanMailbox_t;
typedef struct CanMailbox {
    uint8_t isBusy;        // 鍙戦€侀偖绠辨槸鍚﹀凡琚娇鐢?
    CanMsgList_t pMsgList; // 鍙戦€侀偖绠变腑鐨勬秷鎭摼琛ㄩ」
    uint32_t bank;         // 鍙戦€侀偖绠辩紪鍙?
    ListHead_s list; // 鍙戦€侀偖绠遍摼琛ㄨ妭鐐?
} CanMailbox_s;

/* CAN 鍔熻兘閰嶇疆閫夐」 */
// TODO: 寰呭畬鍠?
typedef struct CanFunctionalCfg *CanFunctionalCfg_t;
typedef struct CanFunctionalCfg {
    uint16_t autoRetransmit : 1;    // 鑷姩閲嶄紶浣胯兘锛? 绂佺敤锛? 浣胯兘
    uint16_t txPriority : 1;        // 鍙戦€佷紭鍏堢骇锛? 鍩轰簬 ID锛? 鍩轰簬璇锋眰椤哄簭
    uint16_t rxFifoLockMode : 1;    // 鎺ユ敹閿佸畾妯″紡锛? 涓嶉攣瀹氾紝1 閿佸畾鏂版秷鎭?
    uint16_t timeTriggeredMode : 1; // 鏃堕棿瑙﹀彂閫氫俊妯″紡锛? 绂佺敤锛? 浣胯兘
    uint16_t autoWakeUp : 1;        // 鑷姩鍞ら啋锛? 绂佺敤锛? 浣胯兘
    uint16_t autoBusOff : 1;        // 鑷姩鎬荤嚎绂荤嚎锛? 绂佺敤锛? 浣胯兘
    uint16_t isRxOverwrite : 1;     // 鎺ユ敹 FIFO 瑕嗗啓绛栫暐锛? 涓嶈鍐欙紝1 瑕嗗啓
    uint16_t isTxOverwrite : 1;     // 鍙戦€?FIFO 瑕嗗啓绛栫暐锛? 涓嶈鍐欙紝1 瑕嗗啓
    uint16_t rsv : 9;               // 淇濈暀浣?
} CanFunctionalCfg_s;

typedef struct CanCfg *CanCfg_t;
typedef struct CanCfg {
    CanWorkMode_e workMode;           // 宸ヤ綔妯″紡
    CanTimeCfg_s normalTimeCfg;       // 娉㈢壒鐜囬厤缃?
    size_t filterNum;                 // CAN杩囨护鍣ㄤ釜鏁?
    size_t mailboxNum;                // CAN鍙戦€侀偖绠变釜鏁帮紝鍜岀‖浠跺彂閫侀偖绠变竴涓€瀵瑰簲
    size_t rxMsgListBufSize;          // CAN鎺ユ敹FIFO娑堟伅缂撳啿鍖哄ぇ灏忥紙鍗曚綅锛氭姤鏂囨暟閲忥級
    size_t txMsgListBufSize;          // CAN鍙戦€丗IFO娑堟伅缂撳啿鍖哄ぇ灏忥紙鍗曚綅锛氭姤鏂囨暟閲忥級
    uint32_t statusCheckTimeout;      // 妫€鏌ヨ秴鏃舵椂闂达紙鍗曚綅锛氭绉掞級
    CanFunctionalCfg_s functionalCfg; // 鍔熻兘閰嶇疆閫夐」
} CanCfg_s;

// CAN 榛樿閰嶇疆
#define CAN_DEFUALT_CFG                                                                                                \
    (CanCfg_s)                                                                                                         \
    {                                                                                                                  \
        .workMode = CAN_WORK_NORMAL, .rxMsgListBufSize = 32, .txMsgListBufSize = 32, .mailboxNum = 3, .filterNum = 28, \
        .functionalCfg =                                                                                               \
            {                                                                                                          \
                .autoRetransmit    = 0,                                                                                \
                .autoBusOff        = 0,                                                                                \
                .autoWakeUp        = 0,                                                                                \
                .rxFifoLockMode    = 0,                                                                                \
                .timeTriggeredMode = 0,                                                                                \
                .txPriority        = 0,                                                                                \
                .isRxOverwrite     = 0,                                                                                \
                .isTxOverwrite     = 0,                                                                                \
                .rsv               = 0,                                                                                \
            },                                                                                                         \
        .normalTimeCfg =                                                                                               \
            {                                                                                                          \
                .baudRate = CAN_BAUD_1M,                                                                               \
            },                                                                                                         \
        .statusCheckTimeout = 10,                                                                                      \
    }


typedef struct CanHwCapability *CanHwCapability_t;
typedef struct CanHwCapability {
    uint8_t hwBankCount;
    const uint8_t *hwBankList;
} CanHwCapability_s;

typedef struct CanFilterResMgr *CanFilterResMgr_t;
typedef struct CanFilterResMgr {
    uint16_t slotCount;             // 杩囨护鍣ㄦЫ浣嶆暟閲?
    uint16_t maxHwBank;             // 鏈€澶х‖浠惰繃婊ゅ櫒bank鏁伴噺
    AwBitmapAtomic_s slotUsedMap;   // 杩囨护鍣ㄦЫ浣嶄娇鐢ㄧ姸鎬佹槧灏勮〃
    AwBitmapAtomic_s hwBankUsedMap; // 纭欢杩囨护鍣╞ank浣跨敤鐘舵€佹槧灏勮〃
    uint8_t *hwBankList;            // 纭欢杩囨护鍣╞ank鍒楄〃
    int16_t *slotToHwBank;          // 杩囨护鍣ㄦЫ浣嶅埌纭欢杩囨护鍣╞ank鐨勬槧灏勮〃
} CanFilterResMgr_s;                // CAN杩囨护鍣ㄨ祫婧愮鐞嗙粨鏋勪綋
typedef struct HalCanHandler *HalCanHandler_t;

typedef struct CanAdapterInterface *CanAdapterInterface_t;
typedef struct CanAdapterInterface {
    /**
     * @brief 鍒嗛厤FIFO鐨勬秷鎭紦鍐插尯锛屽苟閾炬帴鍒癈AN鎺ユ敹FIFO鐨刦ree閾捐〃
     * @param msgNum 娑堟伅缂撳啿鍖哄ぇ灏忥紙鍗曚綅锛氭秷鎭暟閲忥級
     * @return void* 鎸囧悜鍒嗛厤鐨勬秷鎭紦鍐插尯鐨勬寚閽?
     * @retval NULL 澶辫触
     * @retval 闈濶ULL 鎸囧悜鍒嗛厤鐨勬秷鎭紦鍐插尯鐨勬寚閽?
     */
    void *(*msgbufferAlloc)(ListHead_s *free_list_head, uint32_t msg_num);
} CanAdapterInterface_s;

typedef struct CanHwInterface *CanHwInterface_t;
typedef struct CanHwInterface {
    /**
     * @brief 閰嶇疆CAN纭欢
     * @param can CAN璁惧鍙ユ焺
     * @param cfg CAN閰嶇疆缁撴瀯浣撴寚閽?
     * @return OmRet_e 閰嶇疆缁撴灉
     * @retval OM_OK 鎴愬姛
     * @retval AWLF_ERROR_PARAM 鍙傛暟閿欒
     * @note 鎴愬姛鏃讹紝閰嶇疆CAN纭欢涓烘寚瀹氱殑宸ヤ綔妯″紡銆佹尝鐗圭巼銆佽繃婊ゅ櫒銆佸彂閫侀偖绠便€佹帴鏀禙IFO绛夊弬鏁?
     */
    OmRet_e (*configure)(HalCanHandler_t can, CanCfg_t cfg);
    /**
     * @brief 鎺у埗CAN纭欢
     * @param can CAN璁惧鍙ユ焺
     * @param cmd 鎺у埗鍛戒护 @ref CAN_CMD_DEF
     * @param arg 鎺у埗鍙傛暟鎸囬拡
     * @retval OM_OK 鎴愬姛
     * @retval AWLF_ERROR_PARAM 鍙傛暟閿欒
     * @note 鎴愬姛鏃讹紝鏍规嵁cmd鎵ц瀵瑰簲鐨勬帶鍒舵搷浣滐紝濡傚惎鍔ㄣ€佸仠姝€佽缃弬鏁扮瓑
     */
    OmRet_e (*control)(HalCanHandler_t can, uint32_t cmd, void *arg);
    /**
     * @brief 鍙戦€佷竴甯?CAN 鎶ユ枃鍒扮‖浠堕偖绠?
     * @param can CAN璁惧鍙ユ焺
     * @param msg CAN纭欢娑堟伅缁撴瀯浣撴寚閽?
     * @return OmRet_e 鍙戦€佺粨鏋?
     * @retval OM_OK 鎴愬姛
     * @retval AWLF_ERROR_PARAM 鍙傛暟閿欒
     * @retval AWLF_ERR_OVERFLOW CAN鍙戦€侀偖绠卞凡婊?
     * @note 鎴愬姛鏃讹紝纭欢搴斿洖濉?`msg->hwTxMailbox`锛堝疄闄呭彂閫侀偖绠辩紪鍙凤級銆?
     */
    OmRet_e (*sendMsgMailbox)(HalCanHandler_t can, CanHwMsg_t msg);
    /**
     * @brief 鎸囧畾CAN鎺ユ敹FIFO锛屽皢鏁版嵁鎷疯礉杩沵sg->userBuf
     * @param can CAN璁惧鍙ユ焺
     * @param msg CAN鐢ㄦ埛娑堟伅缁撴瀯浣撴寚閽?
     * @param rxfifo_bank 鎺ユ敹FIFO缂栧彿
     * @return OmRet_e 鎺ユ敹缁撴灉
     * @retval OM_OK 鎴愬姛
     * @retval AWLF_ERROR_PARAM 鍙傛暟閿欒
     * @retval AWLF_ERROR_EMPTY CAN鎺ユ敹FIFO涓虹┖
     * @retval AWLF_ERR_OVERFLOW CAN鎺ユ敹FIFO涓虹┖
     *
     * @note rxfifo_bank 璇ュ€间负bsp浼犲叆鐨勬帴鏀禙IFO缂栧彿
     * @note 鑻ユ鏋?RX FIFO 涓虹┖涓斾笉閲囩敤瑕嗗啓绛栫暐锛屾鏋跺眰浼氬皢 msg 璁句负 NULL锛?
     *       BSP 灞傚簲璇诲彇骞朵涪寮冨綋鍓嶅抚浠ユ竻涓柇銆?
     *
     */
    OmRet_e (*recvMsg)(HalCanHandler_t can, CanHwMsg_t msg, int32_t rxfifo_bank);
} CanHwInterface_s;

/**
 * @brief CAN鐘舵€佺鐞嗙粨鏋勪綋
 */
typedef struct CanStatusManager *CanStatusManager_t;
typedef struct CanStatusManager {
    OsalTimer_t statusTimer;         // 鐘舵€佸畾鏃跺櫒鍙ユ焺
    CanNodeErrStatus_e nodeErrStatus; // CAN鑺傜偣閿欒鐘舵€?
    CanErrCounter_s errCounter;
} CanStatusManager_s;

/* CAN RX handler structure */
typedef struct CanRxHandler *CanRxHandler_t;
typedef struct CanRxHandler {
    CanFilter_t filterTable; // CAN杩囨护鍣ㄨ〃
    CanMsgFifo_s rxFifo;     // CAN鎺ユ敹FIFO
} CanRxHandler_s;

/* CAN鍙戦€佸鐞嗙粨鏋勪綋 */
typedef struct CanTxHandler *CanTxHandler_t;
typedef struct CanTxHandler {
    CanMailbox_t pMailboxes;      // 鍙戦€侀偖绠辨暟缁勬寚閽?
    CanMsgFifo_s txFifo;          // 鐩墠鍙湁涓€涓彂閫丗IFO
    ListHead_s mailboxList; // 鍙敤鐨勫彂閫侀偖绠遍摼琛?
} CanTxHandler_s;

/* CAN device handle structure */
// TODO: CAN 鏋舵瀯鏄吀鍨嬬殑鍗曠敓浜ц€呭娑堣垂鑰呮ā鍨嬶紝鍙湪鍚庣画閲嶆瀯涓烘棤閿侀槦鍒楁灦鏋?
// 閬靛惊make it work first, then make it right, finnally make it fast 鐨勫師鍒欙紝鏆傛椂鍏堥噰鐢ㄩ攣鏈哄埗
typedef struct HalCanHandler {
    Device_s parent;                        // 鐖惰澶?
    CanRxHandler_s rxHandler;               // CAN鎺ユ敹澶勭悊
    CanTxHandler_s txHandler;               // CAN鍙戦€佸鐞?
    CanStatusManager_s statusManager;       // CAN鐘舵€佺鐞嗭紝TODO: 鍔犲叆鐘舵€佺鐞嗙浉鍏虫満鍒?
    CanFilterResMgr_s filterResMgr;         // CAN 杩囨护鍣ㄨ祫婧愮鐞嗗櫒
    CanHwInterface_t hwInterface;           // CAN纭欢鎺ュ彛
    CanAdapterInterface_t adapterInterface; // CAN閫傞厤鍣ㄦ帴鍙ｏ紝灞忚斀CAN銆丆ANFD 鐨勫樊寮?
    CanCfg_s cfg;                           // 閰嶇疆缁撴瀯浣?
} HalCanHandler_s;

#define IS_CAN_FILTER_INVALID(pCan, handle)  ((handle) >= (pCan)->filterResMgr.slotCount)
#define IS_CAN_MAILBOX_INVALID(pCan, bank) ((bank) < 0 || (bank) >= (pCan)->cfg.mailboxNum)

/**
 * @brief 鑾峰彇缁忓吀CAN鐨勯€傞厤鍣ㄦ帴鍙?
 * @return CanAdapterInterface_t 缁忓吀CAN鐨勯€傞厤鍣ㄦ帴鍙ｆ寚閽?
 */
CanAdapterInterface_t hal_can_get_classic_adapter_interface(void);

/**
 * @brief 鑾峰彇CANFD鐨勯€傞厤鍣ㄦ帴鍙?
 * @return CanAdapterInterface_t CANFD鐨勯€傞厤鍣ㄦ帴鍙ｆ寚閽?
 */
CanAdapterInterface_t hal_can_get_canfd_adapter_interface(void);

/**
 * @brief CAN璁惧娉ㄥ唽锛堝吋瀹圭粡鍏?CAN 鍜?CANFD锛?
 * @param can CAN璁惧鍙ユ焺
 * @param name 璁惧鍚嶇О
 * @param handle 纭欢鍙ユ焺
 * @param regparams 娉ㄥ唽鍙傛暟 @ref CAN_REG_DEF
 * @return OmRet_e 娉ㄥ唽缁撴灉
 * @retval OM_OK 鎴愬姛
 * @retval AWLF_ERROR_PARAM 鍙傛暟閿欒
 * @retval AWLF_ERR_CONFLICT 璁惧鍚嶇О鍐茬獊
 */
OmRet_e hal_can_register(HalCanHandler_t can, char *name, void *handle, uint32_t regparams);

/**
 * @brief CAN涓柇澶勭悊鍑芥暟
 * @param can CAN璁惧鍙ユ焺
 * @param event 涓柇浜嬩欢
 * @param param 涓柇鍙傛暟
 * @return void
 * @note 涓柇鍙傛暟鐨勫惈涔夋牴鎹叿浣撶殑CAN浜嬩欢鑰屼笉鍚?
 * @note  1. CAN_ISR_EVENT_INT_RX_DONE: param 涓烘帴鏀跺埌鐨?RX FIFO 绱㈠紩
 * @note  2. CAN_ISR_EVENT_INT_TX_DONE: param涓哄彂閫侀偖绠辩储寮?
 * @note  3. CAN_ISR_EVENT_INT_ERROR:   param涓洪敊璇爜
 */
void hal_can_isr(HalCanHandler_t can, CanIsrEvent_e event, size_t param);

/**
 * @brief CAN閿欒涓柇澶勭悊鍑芥暟
 * @param can CAN璁惧鍙ユ焺
 * @param err_event 閿欒浜嬩欢
 */
void can_error_isr(HalCanHandler_t can, uint32_t err_event, size_t param);

#ifdef __cplusplus
}
#endif

#endif




