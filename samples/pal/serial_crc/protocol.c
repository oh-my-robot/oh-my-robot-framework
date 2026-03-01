/*
    测试程序，设计严重不安全，请勿正式使用
*/

#include "protocol.h"
#include "core/algorithm/protocol/crc.h"
#include <string.h>

/*
    后续改进方案，改进数据结构，构思一套适配于rb的check_num计算方案
    思路记录：protocol.h 数据结构中，缺少一个核心对象，该对象中应该存有一个rb，所有的打包解包都针对这个rb进行
*/

static OmRet_e test_header_append_check_num(test_header_t header)
{
    append_crc8_check_sum((uint8_t*)header, TEST_HEADER_SZ);
    return OM_OK;
}

static OmRet_e test_fmt_append_check_num(test_fmt_t fmt)
{
    append_crc16_check_sum((uint8_t*)fmt, TEST_FMT_SZ + fmt->header.datasz + CRC16_SZ);
    return OM_OK;
}

OmRet_e test_data_pack(test_protocol_t protocol, uint16_t cmd_id, uint8_t* pdata, uint16_t datasz)
{
    if (!protocol || !pdata || !datasz || datasz > TEST_MAX_DATASZ)
        return OM_ERROR_PARAM;

    test_fmt_t fmt = &protocol->packet.fmt;
    test_header_t header = &fmt->header;
    header->sof = TEST_SOF;
    header->seq = (header->seq != 0xffffffff) ? (header->seq + 1) : 1;
    header->datasz = datasz;
    test_header_append_check_num(header); // 附加帧头校验值

    fmt->cmd_id = cmd_id;
    memcpy(fmt->fxarry, pdata, datasz);
    test_fmt_append_check_num(fmt); // 附加整包校验值
    return OM_OK;
}

/*
    return 数据包长度
*/
size_t test_data_unpack(test_protocol_t protocol, uint16_t* p_cmd_id, uint8_t* databuf, size_t bufsz)
{
    uint8_t check;
    test_fmt_t fmt;
    test_header_t header;

    if (!protocol || !databuf || !bufsz || !p_cmd_id)
        return OM_ERROR_PARAM;

    fmt = &protocol->packet.fmt;
    header = &fmt->header;

    if (header->sof != TEST_SOF)
        return 0;

    check = verify_crc8_check_sum((uint8_t*)header, TEST_HEADER_SZ);
    if (!check || header->datasz > bufsz)
        return 0;

    check = verify_crc16_check_sum((uint8_t*)fmt, sizeof(protocol->packet));
    if (!check)
        return 0;

    memcpy(databuf, fmt->fxarry, header->datasz);

    return header->datasz;
}

OmRet_e test_device_init(test_protocol_t protocol, Device_t dev)
{
    test_fmt_t fmt = &protocol->packet.fmt;
    test_header_t header = &fmt->header;
    // ringbuf_t rb = &protocol->unpacker.rb;

    // ringbuf_init(rb, protocol->unpacker.buf, sizeof(protocol->packet),
    // (size_t)sizeof(protocol->unpacker.buf)/sizeof(protocol->packet));

    protocol->dev = dev;
    fmt->cmd_id = 0;
    header->sof = 0;
    header->seq = 0;
    header->crc8 = 0;
    header->datasz = 0;
    return OM_OK;
}
