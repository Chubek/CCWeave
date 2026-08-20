/**
 * @file etk_registry.h
 * @brief Caller-owned extension descriptors and activation registry.
 * @stability experimental
 * @depends ExtensionTk::types, ExtensionTk::version, InteropTk::alloc
 */
#ifndef ETK_REGISTRY_H
#define ETK_REGISTRY_H
#include "etk_version.h"
#define ETK_REGISTRY_MAX_EXTENSIONS 64
typedef struct etk_extension { const char *id; etk_version version;
                                const char *const *dependencies; size_t dependency_count;
                                etk_status (*activate)(void *); void (*deactivate)(void *);
                                void *ctx; etk_bool active; } etk_extension;
typedef struct etk_registry { etk_extension entries[ETK_REGISTRY_MAX_EXTENSIONS];
                               size_t count; } etk_registry;
ETK_DEF void etk_registry_init(etk_registry *r);
ETK_DEF etk_status etk_registry_register(etk_registry *r, const etk_extension *e);
ETK_DEF etk_status etk_registry_activate(etk_registry *r);
ETK_DEF etk_extension *etk_registry_find(etk_registry *r, const char *id);
ETK_DEF size_t etk_registry_count(const etk_registry *r);
ETK_DEF void etk_registry_deactivate_all(etk_registry *r);
#ifdef ETK_REGISTRY_IMPLEMENTATION
#include <string.h>
ETK_DEF void etk_registry_init(etk_registry *r){if(r)memset(r,0,sizeof(*r));}
ETK_DEF etk_status etk_registry_register(etk_registry *r,const etk_extension *e)
{
    if (!r || !e || !e->id || r->count >= ETK_REGISTRY_MAX_EXTENSIONS)
        return ETK_EINVAL;
    if (etk_registry_find(r, e->id)) return ETK_EFAIL;
    r->entries[r->count++] = *e;
    return ETK_OK;
}
ETK_DEF etk_extension *etk_registry_find(etk_registry *r,const char *id)
{size_t i;if(!r||!id)return NULL;for(i=0;i<r->count;++i)if(strcmp(r->entries[i].id,id)==0)return &r->entries[i];return NULL;}
ETK_DEF size_t etk_registry_count(const etk_registry *r){return r?r->count:0;}
ETK_DEF etk_status etk_registry_activate(etk_registry *r)
{
    size_t i;
    if (!r) return ETK_EINVAL;
    for (i = 0; i < r->count; ++i) {
        etk_extension *e = &r->entries[i];
        if (e->activate && e->activate(e->ctx) != ETK_OK) return ETK_EFAIL;
        e->active = ETK_TRUE;
    }
    return ETK_OK;
}
ETK_DEF void etk_registry_deactivate_all(etk_registry *r)
{size_t i;if(!r)return;for(i=r->count;i>0;--i)if(r->entries[i-1].active){if(r->entries[i-1].deactivate)r->entries[i-1].deactivate(r->entries[i-1].ctx);r->entries[i-1].active=ETK_FALSE;}}
#endif
#endif
