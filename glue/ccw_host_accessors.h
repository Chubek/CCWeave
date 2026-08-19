/* Host accessor layer: registers the Core Accessor Set from GlueSTD.h
 * into an executor. The host owns node resolution; kernels only ever
 * see 64-bit ids. */

#ifndef CCW_HOST_ACCESSORS_H
#define CCW_HOST_ACCESSORS_H

#include "GlueSTD.h"

#ifdef __cplusplus
extern "C"
{
#endif

  /* Registers every accessor of the Core Accessor Set. MUST be called
   * before any kernel is loaded. */
  ccw_status ccw_host_register_core_accessors (ccw_executor *ex);

  /* Optional edit interposition: the host may log or reject structural
   * edits. Return false to reject; the kernel then sees a Scheme error. */
  typedef enum
  {
    CCW_EDIT_REPLACE,
    CCW_EDIT_INSERT_BEFORE,
    CCW_EDIT_DELETE,
    CCW_EDIT_BLOCK_DELETE
  } ccw_edit_kind;

  typedef bool (*ccw_edit_hook) (void *user_data, ccw_ir *ir,
                                 ccw_edit_kind kind, ccw_node target,
                                 ccw_node incoming);

  void ccw_host_set_edit_hook (ccw_edit_hook hook, void *user_data);

#ifdef __cplusplus
}
#endif
#endif /* CCW_HOST_ACCESSORS_H */
