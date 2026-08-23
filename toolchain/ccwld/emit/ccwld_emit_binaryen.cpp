#include "../ccwld.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "binaryen-c.h"

extern "C" int
ccwld_emit_binaryen (const char *output, const char *entry,
                     ccwld_error *error)
{
  if (output == nullptr)
    {
      if (error != nullptr)
        {
          error->code = 2;
          std::snprintf (error->message, sizeof (error->message),
                         "Binaryen emitter requires an output path");
        }
      return 0;
    }

  BinaryenModuleRef module = BinaryenModuleCreate ();
  if (module == nullptr)
    return 0;

  const char *function_name = entry != nullptr ? entry : "_start";
  BinaryenExpressionRef body
      = BinaryenConst (module, BinaryenLiteralInt32 (0));
  BinaryenAddFunction (module, function_name, BinaryenTypeNone (),
                       BinaryenTypeInt32 (), nullptr, 0, body);
  BinaryenAddFunctionExport (module, function_name, function_name);
  BinaryenModuleValidate (module);

  BinaryenModuleAllocateAndWriteResult result
      = BinaryenModuleAllocateAndWrite (module, nullptr);
  if (result.binary == nullptr || result.binaryBytes == 0)
    {
      BinaryenModuleDispose (module);
      return 0;
    }

  FILE *file = std::fopen (output, "wb");
  if (file == nullptr
      || std::fwrite (result.binary, 1, result.binaryBytes, file)
             != result.binaryBytes)
    {
      if (file != nullptr)
        std::fclose (file);
      std::free (result.binary);
      BinaryenModuleDispose (module);
      if (error != nullptr)
        {
          error->code = 6;
          std::snprintf (error->message, sizeof (error->message),
                         "cannot write Binaryen output");
        }
      return 0;
    }
  std::fclose (file);
  std::free (result.binary);
  BinaryenModuleDispose (module);
  return 1;
}
