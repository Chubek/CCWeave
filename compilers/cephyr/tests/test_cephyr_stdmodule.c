#include "ccw_test.h"
#include "cephyr_stdmodule.h"

#include <string.h>

int
main (void)
{
  const cephyr_module *bundle = cephyr_stdmodule_bundle ();
  const cephyr_module *gnu = cephyr_stdmodule_gnu_attributes ();
  const cephyr_module *lto = cephyr_stdmodule_lto ();

  CCW_CHECK (bundle == gnu, "the standard bundle must expose GNU attributes");
  CCW_CHECK (cephyr_stdmodule_bundle () == bundle,
             "the standard bundle must be a stable singleton");

  const cephyr_module_info *info = cephyr_module_get_info (gnu);
  CCW_CHECK (info != NULL, "GNU attribute module must expose metadata");
  if (info != NULL)
    {
      CCW_CHECK_STREQ (info->name, "gnu-attributes");
      CCW_CHECK_STREQ (info->version, "0.1.0");
    }

  CCW_CHECK (cephyr_module_attr_handler_count (gnu) == 14,
             "v0.1 GNU attribute bundle must expose all handlers");
  bool saw_noreturn = false;
  for (int i = 0; i < cephyr_module_attr_handler_count (gnu); i++)
    {
      const char *name = cephyr_module_attr_name (gnu, i);
      cephyr_attr_handler_fn handler = cephyr_module_attr_handler (gnu, i);
      CCW_CHECK (name != NULL && handler != NULL,
                 "every attribute entry must have a name and handler");
      if (name != NULL && strcmp (name, "noreturn") == 0)
        {
          saw_noreturn = true;
          CCW_CHECK (handler (NULL, name, NULL),
                     "noreturn handler must accept a recognized attribute");
        }
    }
  CCW_CHECK (saw_noreturn, "GNU attribute bundle must include noreturn");

  CCW_CHECK (cephyr_module_fragment_count (gnu) == 0,
             "GNU attributes must not add pipeline fragments");
  CCW_CHECK (cephyr_module_fragment_count (lto) == 1,
             "LTO module must contribute one fragment");
  const cephyr_module_fragment *fragment = cephyr_module_fragment_ref (lto, 0);
  CCW_CHECK (fragment != NULL && fragment->fragment != NULL
                 && strstr (fragment->fragment, "lto.prepare") != NULL,
             "LTO fragment must probe its kernel capability");

  return ccw_test_report ("cephyr-stdmodule");
}
