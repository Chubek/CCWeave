#include "sml_basis_native.h"

long long ccw_sml_basis_abs(long long value) { return value < 0 ? -value : value; }
long long ccw_sml_basis_min(long long a, long long b) { return a < b ? a : b; }
long long ccw_sml_basis_max(long long a, long long b) { return a > b ? a : b; }

static int basis_dispatch(const ccw_sml_value *args, size_t nargs,
                          ccw_sml_value *results, size_t nresults, void *user)
{
    const char *operation = (const char *)user;
    if (!operation || nargs != 2 || nresults != 1 ||
        args[0].kind != CCW_SML_INT || args[1].kind != CCW_SML_INT)
        return 1;
    results[0].kind = CCW_SML_INT;
    if (operation[0] == 'm' && operation[1] == 'i')
        results[0].integer = ccw_sml_basis_min(args[0].integer, args[1].integer);
    else
        results[0].integer = ccw_sml_basis_max(args[0].integer, args[1].integer);
    return 0;
}

static const ccw_sml_extension extension = {"SMLBasis", basis_dispatch, "min"};
const ccw_sml_extension *ccw_sml_parthia_extension_init(void) { return &extension; }
