#ifndef CCW_DYNALO_BRIDGE_H
#define CCW_DYNALO_BRIDGE_H

#ifdef __cplusplus
extern "C" {
#endif

void *ccw_dynalo_open(const char *path, const char **error_message);
void *ccw_dynalo_symbol(void *library, const char *name,
                        const char **error_message);
void ccw_dynalo_close(void *library);

#ifdef __cplusplus
}
#endif

#endif
