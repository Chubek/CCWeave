#include "vendored-bridge.h"

#include <dynalo/dynalo.hpp>

#include <exception>
#include <string>

static thread_local std::string ccw_dynalo_error;

extern "C" void *
ccw_dynalo_open (const char *path, const char **error_message)
{
  if (error_message)
    *error_message = nullptr;
  if (!path)
    return nullptr;

  try
    {
      dynalo::native::handle handle = dynalo::open (path);
      return reinterpret_cast<void *> (handle);
    }
  catch (const std::exception &error)
    {
      ccw_dynalo_error = error.what ();
      if (error_message)
        *error_message = ccw_dynalo_error.c_str ();
      return nullptr;
    }
}

extern "C" void *
ccw_dynalo_symbol (void *library, const char *name,
                   const char **error_message)
{
  if (error_message)
    *error_message = nullptr;
  if (!library || !name)
    return nullptr;

  try
    {
      using symbol_fn = void (void);
      symbol_fn *symbol = dynalo::get_function<symbol_fn> (
          reinterpret_cast<dynalo::native::handle> (library), name);
      return reinterpret_cast<void *> (symbol);
    }
  catch (const std::exception &error)
    {
      ccw_dynalo_error = error.what ();
      if (error_message)
        *error_message = ccw_dynalo_error.c_str ();
      return nullptr;
    }
}

extern "C" void
ccw_dynalo_close (void *library)
{
  if (!library)
    return;
  try
    {
      dynalo::close (reinterpret_cast<dynalo::native::handle> (library));
    }
  catch (...)
    {
      /* Destruction cannot report a C++ exception through the C ABI. */
    }
}
