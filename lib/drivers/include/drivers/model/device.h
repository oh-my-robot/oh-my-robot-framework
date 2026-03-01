/**
 * @file   device.h
 * @brief  Device model interface declarations.
 */

#ifndef __OM_DEVICE_H__
#define __OM_DEVICE_H__

#include "atomic/atomic_simple.h"
#include "core/data_struct/corelist.h"
#include "core/om_def.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct DevInterface *DevInterface_t;
typedef struct Device *Device_t;

typedef struct DevInterface {
    OmRet_e (*init)(Device_t dev);
    OmRet_e (*open)(Device_t dev, uint32_t oparam);
    OmRet_e (*close)(Device_t dev);
    size_t (*read)(Device_t dev, void *ctrl_info, void *data, size_t len);
    size_t (*write)(Device_t dev, void *ctrl_info, void *data, size_t len);
    OmRet_e (*control)(Device_t dev, size_t cmd, void *args);
} DevInterface_s;

typedef struct DevAttr *DevAttr_t;
typedef struct DevAttr {
    char *name;
    om_atomic_uint_t oparams;
    om_atomic_uint_t regparams;
    om_atomic_uint_t refCount;
    om_atomic_uint_t cFlags;
    om_atomic_uint_t status;

    void *param;
    void (*readCallback)(Device_t dev, void *param, size_t paramsz);
    void (*writeCallback)(Device_t dev, void *param, size_t paramsz);
    void (*errCallback)(Device_t dev, uint32_t errcode, void *param, size_t paramsz);
} DevAttr_s;

typedef struct Device {
    DevInterface_t interface;
    DevAttr_s priv;
    void *handle;
    ListHead_s list;
} Device_s;

/* Device operations */
Device_t device_find(char *name);
OmRet_e device_init(Device_t dev);
OmRet_e device_open(Device_t dev, uint32_t oparam);
OmRet_e device_close(Device_t dev);
size_t device_read(Device_t dev, void *pos, void *data, size_t len);
size_t device_write(Device_t dev, void *pos, void *data, size_t len);
OmRet_e device_ctrl(Device_t dev, size_t cmd, void *args);
OmRet_e device_register(Device_t dev, char *name, uint32_t regparams);

/* Callback helpers */
static inline void device_set_read_cb(Device_t dev, void (*callback)(Device_t dev, void *params, size_t paramsz))
{
    if (!dev) {
        return;
    }
    dev->priv.readCallback = callback;
}

static inline void device_set_write_cb(Device_t dev, void (*callback)(Device_t dev, void *params, size_t paramsz))
{
    if (!dev) {
        return;
    }
    dev->priv.writeCallback = callback;
}

static inline void device_set_err_cb(Device_t dev, void (*callback)(Device_t dev, uint32_t errcode, void *params, size_t paramsz))
{
    if (!dev) {
        return;
    }
    dev->priv.errCallback = callback;
}

static inline void device_set_param(Device_t dev, void *param)
{
    if (!dev) {
        return;
    }
    dev->priv.param = param;
}

static inline void device_read_cb(Device_t dev, size_t paramsz)
{
    if (dev->priv.readCallback) {
        dev->priv.readCallback(dev, dev->priv.param, paramsz);
    }
}

static inline void device_write_cb(Device_t dev, size_t paramsz)
{
    if (dev->priv.writeCallback) {
        dev->priv.writeCallback(dev, dev->priv.param, paramsz);
    }
}

static inline void device_err_cb(Device_t dev, uint32_t errcode, size_t paramsz)
{
    if (dev->priv.errCallback) {
        dev->priv.errCallback(dev, errcode, dev->priv.param, paramsz);
    }
}

/* Status helpers */
static inline uint32_t device_get_oparams(Device_t dev)
{
    return (uint32_t)dev->priv.oparams;
}

static inline uint32_t device_get_cflags(Device_t dev)
{
    return (uint32_t)dev->priv.cFlags;
}

static inline void device_set_cflags(Device_t dev, uint32_t c_flags)
{
    dev->priv.status |= c_flags;
}

static inline void device_set_status(Device_t dev, uint32_t status)
{
    OM_FOR_REL(&dev->priv.status, status);
}

static inline void device_clr_status(Device_t dev, uint32_t status)
{
    OM_FAND_REL(&dev->priv.status, ~status);
}

static inline uint32_t device_check_status(Device_t dev, uint32_t status)
{
    return OM_LOAD_ACQ(&dev->priv.status) & status;
}

static inline uint32_t device_get_regparams(Device_t dev)
{
    return OM_LOAD_ACQ(&dev->priv.regparams);
}

static inline char *device_get_name(Device_t dev)
{
    return dev->priv.name;
}

#ifdef __cplusplus
}
#endif

#endif

