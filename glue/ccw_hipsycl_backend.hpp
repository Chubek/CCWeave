/* =========================================================================
 * ccw_hipsycl_backend.hpp — GPU-accelerated Glue backend using hipSYCL
 *
 * Provides SYCL-backed implementations of key Glue accessor operations
 * for GPU-accelerated compilation.  The backend offloads data-parallel
 * work to CUDA/HIP/OpenMP devices via the hipSYCL runtime.
 *
 * §3.2 conformance: this backend is a host extension; kernels MUST
 * feature-test with (glue-has? ...) before relying on any of its
 * registered accessors.
 * ========================================================================= */

#ifndef CCW_HIPSYCL_BACKEND_HPP
#define CCW_HIPSYCL_BACKEND_HPP

#include "GlueSTD.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ---------- lifecycle ---------- */

/* Creates a hipSYCL backend context.  Returns NULL if no SYCL device
 * is available (the host then falls back to sequential execution). */
typedef struct ccw_hipsycl_ctx ccw_hipsycl_ctx;

ccw_hipsycl_ctx *ccw_hipsycl_create(void);
void             ccw_hipsycl_destroy(ccw_hipsycl_ctx *ctx);

/* Returns the number of available SYCL devices, or 0 if none. */
int ccw_hipsycl_device_count(const ccw_hipsycl_ctx *ctx);

/* Returns a human-readable device name (caller frees). */
char *ccw_hipsycl_device_name(const ccw_hipsycl_ctx *ctx, int idx);

/* ---------- GPU-accelerated operations ---------- */

/* Registers all hipSYCL-backed accessors into the executor.
 * These are host extensions — kernels must feature-test before use. */
ccw_status ccw_hipsycl_register_accessors(ccw_executor *ex,
                                          ccw_hipsycl_ctx *ctx);

/* ---------- batch operations exposed to the host ---------- */

/* Batch-evaluates constant expressions across many instructions.
 * `node_ids` is an array of length `count`; results are written
 * back to `results` (one int64_t per node).  Returns CCW_OK or
 * CCW_ERR_OOM. */
ccw_status ccw_hipsycl_batch_const_fold(ccw_hipsycl_ctx *ctx,
                                        const ccw_node *node_ids,
                                        int64_t *results,
                                        int count);

/* Batch-computes inlining heuristic scores for call sites.
 * `node_ids` is an array of call instructions; `arg_counts` is
 * their operand counts; `scores` receives the heuristic (higher
 * = more benefit from inlining). */
ccw_status ccw_hipsycl_batch_inline_score(ccw_hipsycl_ctx *ctx,
                                          const ccw_node *node_ids,
                                          const int *arg_counts,
                                          int *scores,
                                          int count);

/* Batch-colors an interference graph for register allocation.
 * `edges` is a flat adjacency list with `edge_count` pairs;
 * `k` is the number of physical registers; `colors` receives
 * one colour per node (0..k-1, or -1 if uncolourable). */
ccw_status ccw_hipsycl_batch_graph_color(ccw_hipsycl_ctx *ctx,
                                         const int *edges,
                                         int edge_count,
                                         int node_count,
                                         int k,
                                         int *colors);

/* Batch-constructs pattern-match decision trees.
 * Each match has `arm_count` arms; `decision` receives a tree
 * index (0 = linear, 1 = binary) for each match. */
ccw_status ccw_hipsycl_batch_pattern_decision(ccw_hipsycl_ctx *ctx,
                                              const int *arm_counts,
                                              int *decisions,
                                              int count);

#ifdef __cplusplus
}
#endif

#endif /* CCW_HIPSYCL_BACKEND_HPP */
