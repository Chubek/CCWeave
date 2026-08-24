#ifndef CCW_VENDORED_BRIDGE_H
#define CCW_VENDORED_BRIDGE_H

/* Typed C boundary for vendored libraries used by CCWeave kernels.
 *
 * Vendored implementation types never cross this boundary. Handles are
 * opaque, strings returned as owned copies are released with free(), and
 * fixed-width SIMD values cross by their byte representation only.
 */

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

  /* ---------- dynalo dynamic-library bridge ---------- */

  /* Error strings are borrowed from thread-local bridge storage and remain
   * valid until the next Dynalo bridge call on the same thread. */
  void *ccw_dynalo_open (const char *path, const char **error_message);
  void *ccw_dynalo_symbol (void *library, const char *name,
                           const char **error_message);
  void ccw_dynalo_close (void *library);

  /* ---------- SIMDe portable vector bridge ---------- */

  typedef struct
  {
    unsigned char bytes[16];
  } ccw_v128;

  typedef struct
  {
    unsigned char bytes[32];
  } ccw_v256;

  ccw_v128 ccw_simde_load (const void *p);
  ccw_v128 ccw_simde_loadu (const void *p);
  void ccw_simde_store (void *p, ccw_v128 v);
  void ccw_simde_storeu (void *p, ccw_v128 v);
  ccw_v256 ccw_simde_load256 (const void *p);
  ccw_v256 ccw_simde_loadu256 (const void *p);
  void ccw_simde_store256 (void *p, ccw_v256 v);
  void ccw_simde_storeu256 (void *p, ccw_v256 v);

  ccw_v128 ccw_simde_add_i32x4 (ccw_v128 a, ccw_v128 b);
  ccw_v128 ccw_simde_sub_i32x4 (ccw_v128 a, ccw_v128 b);
  ccw_v128 ccw_simde_mul_i32x4 (ccw_v128 a, ccw_v128 b);
  ccw_v128 ccw_simde_add_f32x4 (ccw_v128 a, ccw_v128 b);
  ccw_v128 ccw_simde_sub_f32x4 (ccw_v128 a, ccw_v128 b);
  ccw_v128 ccw_simde_mul_f32x4 (ccw_v128 a, ccw_v128 b);
  ccw_v128 ccw_simde_div_f32x4 (ccw_v128 a, ccw_v128 b);
  ccw_v256 ccw_simde_add_f32x8 (ccw_v256 a, ccw_v256 b);
  ccw_v256 ccw_simde_mul_f32x8 (ccw_v256 a, ccw_v256 b);
  ccw_v128 ccw_simde_shuffle (ccw_v128 a, const int indices[4]);
  ccw_v128 ccw_simde_select (ccw_v128 mask, ccw_v128 a, ccw_v128 b);
  float ccw_simde_hreduce_add_f32x4 (ccw_v128 a);
  double ccw_simde_hreduce_add_f64x2 (ccw_v128 a);

  /* ---------- utf8proc Unicode bridge ---------- */

  char *ccw_utf8proc_version (void);
  const char *ccw_utf8proc_errmsg (int64_t errcode);
  int64_t ccw_utf8proc_iterate (const uint8_t *str, int64_t strlen,
                                int32_t *codepoint_out);
  int64_t ccw_utf8proc_encode_char (int32_t codepoint, uint8_t *dst);
  bool ccw_utf8proc_codepoint_valid (int32_t codepoint);
  int ccw_utf8proc_charwidth (int32_t codepoint);
  int32_t ccw_utf8proc_tolower (int32_t c);
  int32_t ccw_utf8proc_toupper (int32_t c);
  int32_t ccw_utf8proc_totitle (int32_t c);
  int ccw_utf8proc_category (int32_t codepoint);
  char *ccw_utf8proc_NFD (const uint8_t *str);
  char *ccw_utf8proc_NFC (const uint8_t *str);
  char *ccw_utf8proc_NFKD (const uint8_t *str);
  char *ccw_utf8proc_NFKC (const uint8_t *str);
  char *ccw_utf8proc_NFKC_Casefold (const uint8_t *str);
  int64_t ccw_utf8proc_map (const uint8_t *str, int64_t len,
                            uint8_t **dst_out, int options);
  int64_t ccw_utf8proc_decompose_char (int32_t codepoint, int32_t *dst,
                                       int64_t bufsize, int options,
                                       int *last_boundclass);
  int64_t ccw_utf8proc_reencode (int32_t *buffer, int64_t length,
                                 int options);
  bool ccw_utf8proc_grapheme_break (int32_t c1, int32_t c2);

  /* ---------- ISL polyhedral bridge ---------- */

  typedef struct ccw_isl_ctx ccw_isl_ctx;
  typedef struct ccw_isl_uset ccw_isl_uset;
  typedef struct ccw_isl_umap ccw_isl_umap;
  typedef struct ccw_isl_schedule ccw_isl_schedule;

  ccw_isl_ctx *ccw_isl_ctx_new_pinned (void);
  void ccw_isl_ctx_free (ccw_isl_ctx *ctx);
  unsigned long ccw_isl_ctx_quota (const ccw_isl_ctx *ctx);
  ccw_isl_uset *ccw_isl_uset_parse (ccw_isl_ctx *ctx, const char *text);
  char *ccw_isl_uset_serialize (const ccw_isl_uset *uset);
  void ccw_isl_uset_free (ccw_isl_uset *uset);
  ccw_isl_umap *ccw_isl_umap_parse (ccw_isl_ctx *ctx, const char *text);
  char *ccw_isl_umap_serialize (const ccw_isl_umap *umap);
  void ccw_isl_umap_free (ccw_isl_umap *umap);
  ccw_isl_schedule *ccw_isl_schedule_parse (ccw_isl_ctx *ctx,
                                            const char *text);
  char *ccw_isl_schedule_serialize (const ccw_isl_schedule *schedule);
  void ccw_isl_schedule_free (ccw_isl_schedule *schedule);

#ifdef __cplusplus
}
#endif

#endif /* CCW_VENDORED_BRIDGE_H */
