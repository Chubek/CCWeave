#ifndef CCWLD_LTO_H
#define CCWLD_LTO_H
#include <stddef.h>
#include <stdint.h>
#define CCWLD_LTO_ABI_VERSION 1u
typedef struct ccwld_lto_ctx ccwld_lto_ctx;
typedef struct {
  uint32_t abi_version;
  const char *pipeline;
  unsigned jobs;
  const char *cache_dir;
} ccwld_lto_config;
ccwld_lto_ctx *ccwld_lto_begin(const ccwld_lto_config *);
int ccwld_lto_add_module(ccwld_lto_ctx *, const void *, size_t, const char *);
int ccwld_lto_run(ccwld_lto_ctx *,
                  void (*)(void *, const void *, size_t, const char *), void *);
void ccwld_lto_end(ccwld_lto_ctx *);
const char *ccwld_lto_last_error(ccwld_lto_ctx *);
#endif
