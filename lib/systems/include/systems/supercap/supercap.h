#ifndef __OM_SUPERCAP_H__
#define __OM_SUPERCAP_H__

#ifdef __cplusplus
extern "C"
{
#endif

    typedef struct Supercap Supercap;

    Supercap* supercap_init(void* cfg);
    void supercap_update(Supercap* cap);
    void supercap_shutdown(Supercap* cap);

#ifdef __cplusplus
}
#endif

#endif
