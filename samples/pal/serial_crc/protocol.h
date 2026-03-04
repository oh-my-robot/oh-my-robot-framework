/*
    测试程序，设计严重不安全，请勿正式使用
*/

#ifndef TEST_PROTOCOL_H
#define TEST_PROTOCOL_H

#include "core/om_def.h"
#include "core/data_struct/ringbuffer.h"
#include "drivers/model/device.h"

#define TEST_HEADER_SZ (sizeof(TestHeader))
#define TEST_FMT_SZ (sizeof(TestFmt))
#define TEST_PACKET_SZ (sizeof(TestPacket))
#define TEST_SOF (0xA5)
#define TEST_MAX_DATASZ (460)

typedef struct TestProtocol TestProtocol;
typedef struct TestHeader TestHeader;
typedef struct TestFmt TestFmt;

typedef struct TestHeader
{
    uint8_t sof;
    uint16_t dataSize;
    uint32_t seq; // 0 - 255
    uint8_t crc8; // 帧头校验
} OM_PACKED TestHeader;

typedef struct TestFmt
{
    TestHeader header;
    uint16_t cmdId;
    uint8_t flexArray[0]; // 柔性数组，不占用空间，本测试程序中，其大小由datasz决定，其内存来源是用户传入的数据
                       // （说实话这个方案相当不错，但是目前没有时间思考更安全的方案，后续改进，做一套适配于环形缓存区的方案）
} OM_PACKED TestFmt;

typedef struct TestPacket
{
    TestFmt fmt;
    uint8_t data[460];
    uint16_t crc16;
} OM_PACKED TestPacket;

typedef struct TestProtocol
{
    Device* dev;       // 设备句柄
    TestPacket packet; // 发送数据包
    // struct
    // {
    //     ringbuf_s     rb;        // 解包缓存
    //     uint8_t       buf[1024]; // 480*2 = 960 最多缓存2包
    // } unpacker;
} TestProtocol;

OmRet test_device_init(TestProtocol* protocol, Device* dev);
OmRet test_data_pack(TestProtocol* protocol, uint16_t cmd_id, uint8_t* pdata, uint16_t datasz);
size_t test_data_unpack(TestProtocol* protocol, uint16_t* p_cmd_id, uint8_t* databuf, size_t bufsz);

#endif
