#ifndef CCW_SWAFF_INTERNAL_H
#define CCW_SWAFF_INTERNAL_H

#include "../ccw_swaff.h"

struct ccw_swaff_frontend {
    const char *name;
};

ccw_ir *ccw_swaff_lower_ocaml(const ccw_swaff_frontend *fe,
                              const char *source, size_t source_len,
                              const char *module_name, ccw_profile profile,
                              ccw_swaff_error_policy policy,
                              ccw_swaff_report *report,
                              char **error_message);

ccw_ir *ccw_swaff_lower_lua(const ccw_swaff_frontend *fe,
                            const char *source, size_t source_len,
                            const char *module_name, ccw_profile profile,
                            ccw_swaff_error_policy policy,
                            ccw_swaff_report *report,
                            char **error_message);

ccw_ir *ccw_swaff_lower_sml(const ccw_swaff_frontend *fe,
                            const char *source, size_t source_len,
                            const char *module_name, ccw_profile profile,
                            ccw_swaff_error_policy policy,
                            ccw_swaff_report *report,
                            char **error_message);

#endif
