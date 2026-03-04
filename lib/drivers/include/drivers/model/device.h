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

typedef struct DevInterface  DevInterface;
typedef struct Device  Device;

typedef struct DevInterface {
    OmRet (*init)(Device* dev);
    OmRet (*open)(Device* dev, uint32_t oparam);
    OmRet (*close)(Device* dev);
    size_t (*read)(Device* dev, void *ctrl_info, void *data, size_t len);
    size_t (*write)(Device* dev, void *ctrl_info, void *data, size_t len);
    OmRet (*control)(Device* dev, size_t cmd, void *args);
} DevInterface;

typedef struct DevAttr  DevAttr;
typedef struct DevAttr {
    char *name;
    OmAtomicUint oparams;
    OmAtomicUint regparams;
    OmAtomicUint refCount;
    OmAtomicUint cFlags;
    OmAtomicUint status;

    void *param;
    void (*readCallback)(Device* dev, void *param, size_t paramsz);
    void (*writeCallback)(Device* dev, void *param, size_t paramsz);
    void (*errCallback)(Device* dev, uint32_t errcode, void *param, size_t paramsz);
} DevAttr;

typedef struct Device {
    DevInterface* interface;
    DevAttr priv;
    void *handle;
    ListHead list;
} Device;

/* Device operations */
Device* device_find(char *name);
OmRet device_init(Device* dev);
OmRet device_open(Device* dev, uint32_t oparam);
OmRet device_close(Device* dev);
size_t device_read(Device* dev, void *pos, void *data, size_t len);
size_t device_write(Device* dev, void *pos, void *data, size_t len);
OmRet device_ctrl(Device* dev, size_t cmd, void *args);
OmRet device_register(Device* dev, char *name, uint32_t regparams);

/* Callback helpers */
static inline void device_set_read_cb(Device* dev, void (*callback)(Device* dev, void *params, size_t paramsz))
{
    if (!dev) {
        return;
    }
    dev->priv.readCallback = callback;
}

static inline void device_set_write_cb(Device* dev, void (*callback)(Device* dev, void *params, size_t paramsz))
{
    if (!dev) {
        return;
    }
    dev->priv.writeCallback = callback;
}

static inline void device_set_err_cb(Device* dev, void (*callback)(Device* dev, uint32_t errcode, void *params, size_t paramsz))
{
    if (!dev) {
        return;
    }
    dev->priv.errCallback = callback;
}

static inline void device_set_param(Device* dev, void *param)
{
    if (!dev) {
        return;
    }
    dev->priv.param = param;
}

static inline void device_read_cb(Device* dev, size_t paramsz)
{
    if (dev->priv.readCallback) {
        dev->priv.readCallback(dev, dev->priv.param, paramsz);
    }
}

static inline void device_write_cb(Device* dev, size_t paramsz)
{
    if (dev->priv.writeCallback) {
        dev->priv.writeCallback(dev, dev->priv.param, paramsz);
    }
}

static inline void device_err_cb(Device* dev, uint32_t errcode, size_t paramsz)
{
    if (dev->priv.errCallback) {
        dev->priv.errCallback(dev, errcode, dev->priv.param, paramsz);
    }
}

/* Status helpers */
static inline uint32_t device_get_oparams(Device* dev)
{
    return (uint32_t)dev->priv.oparams;
}

static inline uint32_t device_get_cflags(Device* dev)
{
    return (uint32_t)dev->priv.cFlags;
}

static inline void device_set_cflags(Device* dev, uint32_t c_flags)
{
    dev->priv.status |= c_flags;
}

static inline void device_set_status(Device* dev, uint32_t status)
{
    OM_FOR_REL(&dev->priv.status, status);
}

static inline void device_clr_status(Device* dev, uint32_t status)
{
    OM_FAND_REL(&dev->priv.status, ~status);
}

static inline uint32_t device_check_status(Device* dev, uint32_t status)
{
    return OM_LOAD_ACQ(&dev->priv.status) & status;
}

static inline uint32_t device_get_regparams(Device* dev)
{
    return OM_LOAD_ACQ(&dev->priv.regparams);
}

static inline char *device_get_name(Device* dev)
{
    return dev->priv.name;
}

#ifdef __cplusplus
}
#endif

#endif

