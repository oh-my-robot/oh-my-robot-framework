#include "drivers/model/device.h"
#include "osal/osal_core.h"
#include <string.h>

static LIST_HEAD(g_dev_list);

Device_t device_find(char *name)
{
    Device_t dev_pos = NULL;
    if (!name)
        return NULL;

    osal_irq_lock_task();
    LIST_FOR_EACH_ENTRY(dev_pos, &g_dev_list, list)
    {
        if (strcmp(dev_pos->priv.name, name) == 0)
        {
            osal_irq_unlock_task();
            return dev_pos;
        }
    }
    osal_irq_unlock_task();
    return NULL;
}

OmRet_e device_register(Device_t dev, char *name, uint32_t regparams)
{
    if (!dev || !name)
        return OM_ERROR_PARAM;

    osal_irq_lock_task();
    if (device_find(name))
    {
        /* 如果设备名冲突，直接返回 */
        // TODO: log
        osal_irq_unlock_task();
        return OM_ERR_CONFLICT;
    }
    init_list_head(&dev->list);
    list_add_tail(&dev->list, &g_dev_list);
    osal_irq_unlock_task();

    dev->priv.name = name;
    dev->priv.regparams = regparams;
    dev->priv.refCount = 0;
    dev->priv.cFlags = 0;
    dev->priv.oparams = 0;
    dev->priv.readCallback = NULL;
    dev->priv.writeCallback = NULL;
    dev->priv.errCallback = NULL;

    return OM_OK;
}

OmRet_e device_init(Device_t dev)
{
    OmRet_e ret = OM_OK;
    if (!dev || !dev->interface)
        return OM_ERROR_PARAM;
    if (dev->interface->init)
    {
        if (!device_check_status(dev, DEV_STATUS_INITED))
        {
            ret = dev->interface->init(dev);
            if (ret != OM_OK)
            {
                // TODO:  LOG ERR
            }
            else
            {
                device_set_status(dev, DEV_STATUS_INITED);
            }
        }
    }
    else
    {
        ret = OM_ERROR;
        // TODO: LOG ERR
    }
    return ret;
}

OmRet_e device_open(Device_t dev, uint32_t oparams)
{
    if (!dev || !dev->interface)
        return OM_ERROR_PARAM;
    OmRet_e ret = OM_ERROR;
    if (!device_check_status(dev, DEV_STATUS_INITED))
    {
        if (dev->interface->init)
            ret = device_init(dev);
        if (ret != OM_OK)
        {
            // TODO: LOG ERR
            return ret;
        }
    }

    // TODO: 区分仅能被Open一次的设备和能被多次Open的设备
    if (!device_check_status(dev, DEV_STATUS_OPENED))
    {
        if (dev->interface->open)
            ret = dev->interface->open(dev, oparams);
        if (ret != OM_OK)
        {
            // TODO: LOG ERR
            return ret;
        }
        dev->priv.refCount++;
        device_set_status(dev, DEV_STATUS_OPENED);
        // Dev->__priv.oparams |= oparams;  // oparams由具体实现赋值
    }

    return OM_OK;
}

size_t device_read(Device_t dev, void *pos, void *data, size_t len)
{
    if (!dev || !dev->interface || !data || len == 0)
        return 0;
    if (!device_check_status(dev, DEV_STATUS_OPENED) || device_check_status(dev, DEV_STATUS_SUSPEND))
    {
        return 0;
    }
    if (dev->interface->read)
        return dev->interface->read(dev, pos, data, len);
    return 0;
}

size_t device_write(Device_t dev, void *pos, void *data, size_t len)
{
    if (!dev || !dev->interface || !data || len == 0)
        return 0;
    if (!device_check_status(dev, DEV_STATUS_OPENED) || device_check_status(dev, DEV_STATUS_SUSPEND))
    {
        return 0;
    }
    if (dev->interface->write)
        return dev->interface->write(dev, pos, data, len);
    return 0;
}

OmRet_e device_ctrl(Device_t dev, size_t cmd, void *args)
{
    if (!dev || !dev->interface)
        return OM_ERROR_PARAM;
    if (!device_check_status(dev, DEV_STATUS_OPENED))
    {
        return OM_ERROR;
    }
    if (dev->interface->control)
        return dev->interface->control(dev, cmd, args);
    return OM_ERROR;
}

OmRet_e device_close(Device_t dev)
{
    OmRet_e ret = OM_OK;
    if (!dev || !dev->interface || !dev->interface->close)
        return OM_ERROR_PARAM;
    if (!device_check_status(dev, DEV_STATUS_OPENED))
    {
        // @TODO: log
        return OM_ERROR;
    }
    if (--dev->priv.refCount == 0)
    {
        ret = dev->interface->close(dev);
        if (ret == OM_OK)
        {
            dev->priv.status = DEV_STATUS_CLOSED;
        }
        else
        {
            // @TODO: log err
        }
    }
    return ret;
}
