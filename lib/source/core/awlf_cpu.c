#include "core/om_cpu.h"

static OmCpu om_cpu = {
    .cpuName = OM_CPU_NAME,
    .omVersion = FRAMEWORK_VERSION,
};

char *om_get_cpu_name(void)
{
    return om_cpu.cpuName;
}

char *om_get_firmware_version(void)
{
    return om_cpu.omVersion;
}

Cputime om_cpu_get_time_s(void)
{
    return om_cpu.interface->getCpuTimeS();
}

CputimeMs om_cpu_get_time_ms(void)
{
    return om_cpu.interface->getCpuTimeMs();
}

CputimeUs om_cpu_get_time_us(void)
{
    return om_cpu.interface->getCpuTimeUs();
}

Cputime om_cpu_get_delta_time_s(CputimeCnt *last_time_cnt)
{
    return om_cpu.interface->getDeltaCpuTimeS(last_time_cnt);
}

void om_cpu_errhandler(char *file, uint32_t line, uint8_t level, char *msg)
{
    // TODO: 根据level打印不同的日志
    if (level >= OM_LOG_LEVEL_MAX)
        level = OM_LOG_LEVEL_ERROR;
    if (level == OM_LOG_LEVEL_FATAL)
        om_cpu.interface->errhandler();
}

void om_cpu_reset(void)
{
    // TODO: LOG
    om_cpu.interface->reset();
}

void om_cpu_delay_ms(float ms)
{
    om_cpu.interface->delayMs(ms);
}

void om_cpu_register(uint32_t cpu_freq_m_hz, OmBoardInterface *interface)
{
    while (!interface)
    {
    };
    om_cpu.cpuName = OM_CPU_NAME;

    om_cpu.omVersion = FRAMEWORK_VERSION;
    om_cpu.cpuFreqMHz = cpu_freq_m_hz;
    om_cpu.cpuFreqHz = cpu_freq_m_hz * 1000000U;
    om_cpu.interface = interface;
}

void om_core_init(void)
{
    while (om_cpu.cpuFreqMHz <= 0 || om_cpu.cpuFreqHz <= 0)
    {
    }; // TODO: assert
    while (om_cpu.interface == NULL)
    {
    }; // TODO: assert
}
