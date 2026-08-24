#include "ccw_test.h"
#include "GlueSTD.h"

#include <stdlib.h>
#include <string.h>

int
main (void)
{
  const float left[4] = { 1.0f, 2.0f, 3.0f, 4.0f };
  const float right[4] = { 4.0f, 3.0f, 2.0f, 1.0f };
  float sum[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
  char *version;
  char *normalized;
  const char *error_message = "unchanged";

  ccw_simde_storeu (
      sum, ccw_simde_add_f32x4 (ccw_simde_loadu (left),
                                ccw_simde_loadu (right)));
  for (int index = 0; index < 4; index++)
    CCW_CHECK (sum[index] == 5.0f,
               "SIMDe bridge must preserve four-lane addition");

  version = ccw_utf8proc_version ();
  CCW_CHECK (version != NULL && version[0] != '\0',
             "utf8proc bridge must return an owned version string");
  free (version);

  normalized = ccw_utf8proc_NFC ((const uint8_t *)"bridge");
  CCW_CHECK (normalized != NULL && strcmp (normalized, "bridge") == 0,
             "utf8proc bridge must normalize ASCII deterministically");
  free (normalized);

  CCW_CHECK (ccw_dynalo_open (NULL, &error_message) == NULL,
             "Dynalo bridge must reject a null path");
  CCW_CHECK (error_message == NULL,
             "Dynalo bridge must clear the error out-parameter");

  return ccw_test_report ("vendored bridge");
}
