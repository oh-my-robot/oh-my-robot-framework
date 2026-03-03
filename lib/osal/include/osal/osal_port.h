#ifndef OM_OSAL_PORT_H
#define OM_OSAL_PORT_H

/* OSAL端口枚举：用于在编译期选择具体实现 */
#define OSAL_PORT_FREERTOS 1
#define OSAL_PORT_POSIX 2

/* 用户可在编译选项中定义 OM_OSAL_PORT 来切换端口，通常由构建系统 profile 统一注入编译选项 */
#ifndef OM_OSAL_PORT
#define OM_OSAL_PORT OSAL_PORT_FREERTOS
#endif

/* 端口取值校验，避免误配置导致链接到错误实现 */
#if (OM_OSAL_PORT != OSAL_PORT_FREERTOS) && (OM_OSAL_PORT != OSAL_PORT_POSIX)
#error "OM_OSAL_PORT 取值非法，请使用 OSAL_PORT_FREERTOS 或 OSAL_PORT_POSIX"
#endif

#endif
