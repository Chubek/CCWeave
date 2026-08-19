#ifndef CCW_SML_BASIS_NATIVE_H
#define CCW_SML_BASIS_NATIVE_H
#include "sml_parthia.h"
#ifdef __cplusplus
extern "C" {
#endif
long long ccw_sml_basis_abs(long long value);
long long ccw_sml_basis_min(long long a, long long b);
long long ccw_sml_basis_max(long long a, long long b);
const ccw_sml_extension *ccw_sml_parthia_extension_init(void);
#ifdef __cplusplus
}
#endif
#endif
