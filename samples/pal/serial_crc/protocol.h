/*
    测试程序，设计严重不安全，请勿正式使用
*/

#ifndef TEST_PROTOCOL_H
#define TEST_PROTOCOL_H

#include "core/om_def.h"
#include "core/data_struct/ringbuffer.h"
#include "drivers/model/device.h"

#define TEST_HEADER_SZ (sizeof(test_header_s))
#define TEST_FMT_SZ (sizeof(test_fmt_s))
#define TEST_PACKET_SZ (sizeof(test_packet_s))
#define TEST_SOF (0xA5)
#define TEST_MAX_DATASZ (460)

typedef struct test_protocol* test_protocol_t;
typedef struct test_header* test_header_t;
typedef struct test_fmt* test_fmt_t;

typedef struct __om_packed test_header
{
    uint8_t sof;
    uint16_t datasz;
    uint32_t seq; // 0 - 255
    uint8_t crc8; // 帧头校验
} test_header_s;

typedef struct __om_packed test_fmt
{
    test_header_s header;
    uint16_t cmd_id;
    uint8_t fxarry[0]; // 柔性数组，不占用空间，本测试程序中，其大小由datasz决定，其内存来源是用户传入的数据
                       // （说实话这个方案相当不错，但是目前没有时间思考更安全的方案，后续改进，做一套适配于环形缓存区的方案）
} test_fmt_s;

typedef struct __om_packed test_packet
{
    test_fmt_s fmt;
    uint8_t data[460];
    uint16_t crc16;
} test_packet_s;

typedef struct test_protocol
{
    Device_t dev;         // 设备句柄
    test_packet_s packet; // 发送数据包
    // struct
    // {
    //     ringbuf_s     rb;        // 解包缓存
    //     uint8_t       buf[1024]; // 480*2 = 960 最多缓存2包
    // } unpacker;
} test_protocol_s;

OmRet_e test_device_init(test_protocol_t protocol, Device_t dev);
OmRet_e test_data_pack(test_protocol_t protocol, uint16_t cmd_id, uint8_t* pdata, uint16_t datasz);
size_t test_data_unpack(test_protocol_t protocol, uint16_t* p_cmd_id, uint8_t* databuf, size_t bufsz);

#endif
