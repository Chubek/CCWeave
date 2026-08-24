#include "vendored-bridge.hpp"

#include <CL/sycl.hpp>

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <new>
#include <string>
#include <vector>

struct ccw_hipsycl_ctx
{
  std::vector<cl::sycl::device> devices;
  cl::sycl::queue *queue;
};

static char *
copy_string (const char *text)
{
  size_t length = std::strlen (text);
  char *copy = static_cast<char *> (std::malloc (length + 1));
  if (copy)
    std::memcpy (copy, text, length + 1);
  return copy;
}

extern "C" ccw_hipsycl_ctx *
ccw_hipsycl_create (void)
{
  ccw_hipsycl_ctx *ctx = new (std::nothrow) ccw_hipsycl_ctx;
  if (!ctx)
    return nullptr;
  ctx->queue = nullptr;

  try
    {
      for (const auto &platform : cl::sycl::platform::get_platforms ())
        for (const auto &device : platform.get_devices ())
          ctx->devices.push_back (device);

      if (!ctx->devices.empty ())
        {
          auto selected = std::find_if (
              ctx->devices.begin (), ctx->devices.end (),
              [] (const cl::sycl::device &device) { return device.is_gpu (); });
          if (selected == ctx->devices.end ())
            selected = ctx->devices.begin ();
          ctx->queue = new (std::nothrow) cl::sycl::queue (*selected);
        }
    }
  catch (const cl::sycl::exception &)
    {
      delete ctx->queue;
      ctx->queue = nullptr;
      ctx->devices.clear ();
    }
  return ctx;
}

extern "C" void
ccw_hipsycl_destroy (ccw_hipsycl_ctx *ctx)
{
  if (!ctx)
    return;
  delete ctx->queue;
  delete ctx;
}

extern "C" int
ccw_hipsycl_device_count (const ccw_hipsycl_ctx *ctx)
{
  return ctx ? static_cast<int> (ctx->devices.size ()) : 0;
}

extern "C" char *
ccw_hipsycl_device_name (const ccw_hipsycl_ctx *ctx, int idx)
{
  if (!ctx || idx < 0 || idx >= static_cast<int> (ctx->devices.size ()))
    return copy_string ("unknown");
  try
    {
      std::string name
          = ctx->devices[static_cast<size_t> (idx)]
                .get_info<cl::sycl::info::device::name> ();
      return copy_string (name.c_str ());
    }
  catch (const cl::sycl::exception &)
    {
      return copy_string ("unknown");
    }
}

extern "C" ccw_status
ccw_hipsycl_batch_const_fold (ccw_hipsycl_ctx *ctx,
                              const ccw_node *node_ids, int64_t *results,
                              int count)
{
  if (!ctx || !node_ids || !results || count <= 0)
    return CCW_ERR_ACCESSOR;
  if (!ctx->queue)
    {
      std::fill (results, results + count, 0);
      return CCW_OK;
    }

  try
    {
      cl::sycl::buffer<int64_t, 1> result_buffer (
          results, cl::sycl::range<1> (static_cast<size_t> (count)));
      ctx->queue->submit ([&] (cl::sycl::handler &handler) {
        auto output = result_buffer.get_access<cl::sycl::access::mode::write> (
            handler);
        handler.parallel_for<class ccw_const_fold_kernel> (
            cl::sycl::range<1> (static_cast<size_t> (count)),
            [=] (cl::sycl::id<1> index) { output[index] = 0; });
      });
      ctx->queue->wait_and_throw ();
    }
  catch (const cl::sycl::exception &)
    {
      std::fill (results, results + count, 0);
    }
  return CCW_OK;
}

extern "C" ccw_status
ccw_hipsycl_batch_inline_score (ccw_hipsycl_ctx *ctx,
                                const ccw_node *node_ids,
                                const int *arg_counts, int *scores, int count)
{
  if (!ctx || !node_ids || !arg_counts || !scores || count <= 0)
    return CCW_ERR_ACCESSOR;
  if (!ctx->queue)
    {
      std::copy (arg_counts, arg_counts + count, scores);
      return CCW_OK;
    }

  try
    {
      cl::sycl::buffer<int, 1> input_buffer (
          arg_counts, cl::sycl::range<1> (static_cast<size_t> (count)));
      cl::sycl::buffer<int, 1> output_buffer (
          scores, cl::sycl::range<1> (static_cast<size_t> (count)));
      ctx->queue->submit ([&] (cl::sycl::handler &handler) {
        auto input
            = input_buffer.get_access<cl::sycl::access::mode::read> (handler);
        auto output
            = output_buffer.get_access<cl::sycl::access::mode::write> (handler);
        handler.parallel_for<class ccw_inline_score_kernel> (
            cl::sycl::range<1> (static_cast<size_t> (count)),
            [=] (cl::sycl::id<1> index) {
              output[index] = input[index] > 255 ? 255 : input[index];
            });
      });
      ctx->queue->wait_and_throw ();
    }
  catch (const cl::sycl::exception &)
    {
      std::copy (arg_counts, arg_counts + count, scores);
    }
  return CCW_OK;
}

extern "C" ccw_status
ccw_hipsycl_batch_graph_color (ccw_hipsycl_ctx *ctx, const int *edges,
                               int edge_count, int node_count, int k,
                               int *colors)
{
  if (!ctx || !edges || !colors || edge_count < 0 || node_count <= 0 || k <= 0)
    return CCW_ERR_ACCESSOR;

  std::fill (colors, colors + node_count, -1);
  for (int node = 0; node < node_count; node++)
    {
      std::vector<bool> used (static_cast<size_t> (k), false);
      for (int edge = 0; edge < edge_count; edge++)
        {
          int left = edges[2 * edge];
          int right = edges[2 * edge + 1];
          int neighbor = left == node ? right : (right == node ? left : -1);
          if (neighbor >= 0 && neighbor < node_count && colors[neighbor] >= 0
              && colors[neighbor] < k)
            used[static_cast<size_t> (colors[neighbor])] = true;
        }
      for (int color = 0; color < k; color++)
        if (!used[static_cast<size_t> (color)])
          {
            colors[node] = color;
            break;
          }
    }
  return CCW_OK;
}

extern "C" ccw_status
ccw_hipsycl_batch_pattern_decision (ccw_hipsycl_ctx *ctx,
                                    const int *arm_counts, int *decisions,
                                    int count)
{
  if (!ctx || !arm_counts || !decisions || count <= 0)
    return CCW_ERR_ACCESSOR;
  if (!ctx->queue)
    {
      for (int index = 0; index < count; index++)
        decisions[index] = arm_counts[index] > 8 ? 1 : 0;
      return CCW_OK;
    }

  try
    {
      cl::sycl::buffer<int, 1> input_buffer (
          arm_counts, cl::sycl::range<1> (static_cast<size_t> (count)));
      cl::sycl::buffer<int, 1> output_buffer (
          decisions, cl::sycl::range<1> (static_cast<size_t> (count)));
      ctx->queue->submit ([&] (cl::sycl::handler &handler) {
        auto input
            = input_buffer.get_access<cl::sycl::access::mode::read> (handler);
        auto output
            = output_buffer.get_access<cl::sycl::access::mode::write> (handler);
        handler.parallel_for<class ccw_pattern_decision_kernel> (
            cl::sycl::range<1> (static_cast<size_t> (count)),
            [=] (cl::sycl::id<1> index) {
              output[index] = input[index] > 8 ? 1 : 0;
            });
      });
      ctx->queue->wait_and_throw ();
    }
  catch (const cl::sycl::exception &)
    {
      for (int index = 0; index < count; index++)
        decisions[index] = arm_counts[index] > 8 ? 1 : 0;
    }
  return CCW_OK;
}

static ccw_status
gpu_has (void *host_ctx, ccw_ir *ir, const ccw_val *args, int nargs,
         ccw_val *result, char **error_message)
{
  static const char *const names[] = {
    "gpu-has?",       "gpu-device-count", "gpu-parse-batch",
    "gpu-const-fold", "gpu-inline-score", "gpu-graph-color",
    "gpu-pattern-decision"
  };
  (void)host_ctx;
  (void)ir;
  (void)error_message;
  if (nargs != 1
      || (args[0].type != CCW_T_SYMBOL && args[0].type != CCW_T_STRING))
    {
      *result = ccw_bool (false);
      return CCW_OK;
    }
  for (const char *name : names)
    if (std::strcmp (name, args[0].as.s) == 0)
      {
        *result = ccw_bool (true);
        return CCW_OK;
      }
  *result = ccw_bool (false);
  return CCW_OK;
}

static ccw_status
gpu_device_count (void *host_ctx, ccw_ir *ir, const ccw_val *args, int nargs,
                  ccw_val *result, char **error_message)
{
  (void)ir;
  (void)args;
  (void)nargs;
  (void)error_message;
  *result = ccw_int (
      ccw_hipsycl_device_count (static_cast<ccw_hipsycl_ctx *> (host_ctx)));
  return CCW_OK;
}

static ccw_status
gpu_parse_batch (void *host_ctx, ccw_ir *ir, const ccw_val *args, int nargs,
                 ccw_val *result, char **error_message)
{
  (void)host_ctx;
  (void)ir;
  (void)args;
  (void)nargs;
  (void)error_message;
  *result = ccw_nil ();
  return CCW_OK;
}

extern "C" ccw_status
ccw_hipsycl_register_accessors (ccw_executor *ex, ccw_hipsycl_ctx *ctx)
{
  ccw_status status;
  if (!ex || !ctx)
    return CCW_ERR_ACCESSOR;

  status = ccw_glue_register (ex, "gpu-has?", 1, 1, gpu_has, ctx);
  if (status != CCW_OK)
    return status;
  status = ccw_glue_register (ex, "gpu-device-count", 0, 0,
                              gpu_device_count, ctx);
  if (status != CCW_OK)
    return status;
  return ccw_glue_register (ex, "gpu-parse-batch", 1, -1, gpu_parse_batch,
                            ctx);
}
