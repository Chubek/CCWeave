#ifndef CCW_VENDORED_BRIDGE_HPP
#define CCW_VENDORED_BRIDGE_HPP

#include "GlueSTD.h"
#include "vendored-bridge.h"

extern "C"
{
  /* hipSYCL is an optional C++ implementation behind a C-callable surface.
   * Its accessor registration depends on the Glue executor ABI, so it lives
   * in this C++ extension header rather than the vendor-only C header. */

  typedef struct ccw_hipsycl_ctx ccw_hipsycl_ctx;

  ccw_hipsycl_ctx *ccw_hipsycl_create (void);
  void ccw_hipsycl_destroy (ccw_hipsycl_ctx *ctx);
  int ccw_hipsycl_device_count (const ccw_hipsycl_ctx *ctx);
  char *ccw_hipsycl_device_name (const ccw_hipsycl_ctx *ctx, int idx);

  ccw_status ccw_hipsycl_register_accessors (ccw_executor *ex,
                                              ccw_hipsycl_ctx *ctx);
  ccw_status ccw_hipsycl_batch_const_fold (ccw_hipsycl_ctx *ctx,
                                            const ccw_node *node_ids,
                                            int64_t *results, int count);
  ccw_status ccw_hipsycl_batch_inline_score (ccw_hipsycl_ctx *ctx,
                                              const ccw_node *node_ids,
                                              const int *arg_counts,
                                              int *scores, int count);
  ccw_status ccw_hipsycl_batch_graph_color (ccw_hipsycl_ctx *ctx,
                                             const int *edges,
                                             int edge_count, int node_count,
                                             int k, int *colors);
  ccw_status ccw_hipsycl_batch_pattern_decision (ccw_hipsycl_ctx *ctx,
                                                  const int *arm_counts,
                                                  int *decisions, int count);
}

#endif /* CCW_VENDORED_BRIDGE_HPP */
