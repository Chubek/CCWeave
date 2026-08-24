#include "vendored-bridge.h"

#include <isl/ctx.h>
#include <isl/options.h>
#include <isl/schedule.h>
#include <isl/union_map.h>
#include <isl/union_set.h>

#include <stdlib.h>

#define CCW_ISL_MAX_OPERATIONS 100000UL

struct ccw_isl_ctx
{
  isl_ctx *raw;
};

struct ccw_isl_uset
{
  isl_union_set *raw;
};

struct ccw_isl_umap
{
  isl_union_map *raw;
};

struct ccw_isl_schedule
{
  isl_schedule *raw;
};

ccw_isl_ctx *
ccw_isl_ctx_new_pinned (void)
{
  ccw_isl_ctx *ctx = (ccw_isl_ctx *)calloc (1, sizeof (*ctx));
  if (!ctx)
    return NULL;

  ctx->raw = isl_ctx_alloc ();
  if (!ctx->raw)
    {
      free (ctx);
      return NULL;
    }

  (void)isl_options_set_schedule_algorithm (ctx->raw,
                                            ISL_SCHEDULE_ALGORITHM_ISL);
  (void)isl_options_set_schedule_max_coefficient (ctx->raw, 1000000);
  (void)isl_options_set_schedule_max_constant_term (ctx->raw, 1000000);
  (void)isl_options_set_schedule_maximize_band_depth (ctx->raw, 1);
  (void)isl_options_set_schedule_maximize_coincidence (ctx->raw, 1);
  (void)isl_options_set_schedule_outer_coincidence (ctx->raw, 1);
  (void)isl_options_set_schedule_split_scaled (ctx->raw, 1);
  (void)isl_options_set_schedule_treat_coalescing (ctx->raw, 1);
  (void)isl_options_set_schedule_separate_components (ctx->raw, 0);
  (void)isl_options_set_schedule_serialize_sccs (ctx->raw, 1);
  (void)isl_options_set_schedule_whole_component (ctx->raw, 0);
  (void)isl_options_set_schedule_carry_self_first (ctx->raw, 1);
  (void)isl_options_set_on_error (ctx->raw, ISL_ON_ERROR_CONTINUE);
  isl_ctx_set_max_operations (ctx->raw, CCW_ISL_MAX_OPERATIONS);
  return ctx;
}

void
ccw_isl_ctx_free (ccw_isl_ctx *ctx)
{
  if (!ctx)
    return;
  isl_ctx_free (ctx->raw);
  free (ctx);
}

unsigned long
ccw_isl_ctx_quota (const ccw_isl_ctx *ctx)
{
  return ctx && ctx->raw ? isl_ctx_get_max_operations (ctx->raw) : 0;
}

ccw_isl_uset *
ccw_isl_uset_parse (ccw_isl_ctx *ctx, const char *text)
{
  ccw_isl_uset *result;
  if (!ctx || !ctx->raw || !text)
    return NULL;
  result = (ccw_isl_uset *)calloc (1, sizeof (*result));
  if (!result)
    return NULL;
  result->raw = isl_union_set_read_from_str (ctx->raw, text);
  if (!result->raw)
    {
      free (result);
      return NULL;
    }
  return result;
}

char *
ccw_isl_uset_serialize (const ccw_isl_uset *uset)
{
  return uset && uset->raw ? isl_union_set_to_str (uset->raw) : NULL;
}

void
ccw_isl_uset_free (ccw_isl_uset *uset)
{
  if (!uset)
    return;
  isl_union_set_free (uset->raw);
  free (uset);
}

ccw_isl_umap *
ccw_isl_umap_parse (ccw_isl_ctx *ctx, const char *text)
{
  ccw_isl_umap *result;
  if (!ctx || !ctx->raw || !text)
    return NULL;
  result = (ccw_isl_umap *)calloc (1, sizeof (*result));
  if (!result)
    return NULL;
  result->raw = isl_union_map_read_from_str (ctx->raw, text);
  if (!result->raw)
    {
      free (result);
      return NULL;
    }
  return result;
}

char *
ccw_isl_umap_serialize (const ccw_isl_umap *umap)
{
  return umap && umap->raw ? isl_union_map_to_str (umap->raw) : NULL;
}

void
ccw_isl_umap_free (ccw_isl_umap *umap)
{
  if (!umap)
    return;
  isl_union_map_free (umap->raw);
  free (umap);
}

ccw_isl_schedule *
ccw_isl_schedule_parse (ccw_isl_ctx *ctx, const char *text)
{
  ccw_isl_schedule *result;
  if (!ctx || !ctx->raw || !text)
    return NULL;
  result = (ccw_isl_schedule *)calloc (1, sizeof (*result));
  if (!result)
    return NULL;
  result->raw = isl_schedule_read_from_str (ctx->raw, text);
  if (!result->raw)
    {
      free (result);
      return NULL;
    }
  return result;
}

char *
ccw_isl_schedule_serialize (const ccw_isl_schedule *schedule)
{
  return schedule && schedule->raw ? isl_schedule_to_str (schedule->raw)
                                   : NULL;
}

void
ccw_isl_schedule_free (ccw_isl_schedule *schedule)
{
  if (!schedule)
    return;
  isl_schedule_free (schedule->raw);
  free (schedule);
}
