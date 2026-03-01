#ifndef __OM_SUPERCAP_H__
#define __OM_SUPERCAP_H__

#ifdef __cplusplus
extern "C"
{
#endif

    typedef struct Supercap* Supercap_t;

    Supercap_t supercap_init(void* cfg);
    void supercap_update(Supercap_t cap);
    void supercap_shutdown(Supercap_t cap);

#ifdef __cplusplus
}
#endif

#endif
