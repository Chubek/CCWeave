#include "ccw_test.h"
#include "ccw_isl.h"

#include <stdlib.h>
#include <string.h>

int main(void)
{
    ccw_isl_ctx *ctx = ccw_isl_ctx_new_pinned();
    CCW_CHECK(ctx != NULL, "pinned ISL context must be constructible");
    if (!ctx)
        return ccw_test_failures ? 1 : 0;
    CCW_CHECK(ccw_isl_ctx_quota(ctx) == 100000UL,
              "ISL operation quota must be pinned");

    ccw_isl_uset *set = ccw_isl_uset_parse(ctx, "{ S[i] : 0 <= i < 4 }");
    CCW_CHECK(set != NULL, "affine union-set must parse");
    char *set_text = ccw_isl_uset_serialize(set);
    CCW_CHECK(set_text != NULL && strstr(set_text, "S[i]") != NULL,
              "union-set must serialize canonically");
    free(set_text);
    ccw_isl_uset_free(set);

    ccw_isl_umap *map = ccw_isl_umap_parse(ctx, "{ S[i] -> A[i] }");
    CCW_CHECK(map != NULL, "affine union-map must parse");
    char *map_text = ccw_isl_umap_serialize(map);
    CCW_CHECK(map_text != NULL && strstr(map_text, "S[i]") != NULL,
              "union-map must serialize canonically");
    free(map_text);
    ccw_isl_umap_free(map);

    ccw_isl_schedule *schedule =
        ccw_isl_schedule_parse(ctx, "{ domain: \"{ S[i] : 0 <= i <= 3 }\" }");
    CCW_CHECK(schedule != NULL, "schedule text must parse");
    char *schedule_text = ccw_isl_schedule_serialize(schedule);
    CCW_CHECK(schedule_text != NULL, "schedule must serialize canonically");
    free(schedule_text);
    ccw_isl_schedule_free(schedule);

    ccw_isl_ctx_free(ctx);
    return ccw_test_report("isl");
}
