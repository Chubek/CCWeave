#include "ccwld.h"

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

#include <LIEF/ELF.hpp>

namespace {

void
set_error (ccwld_error *error, int code, const char *message)
{
  if (error == nullptr)
    return;
  error->code = code;
  std::snprintf (error->message, sizeof (error->message), "%s", message);
}

bool
is_elf (const char *format)
{
  return format == nullptr || std::string (format) == "elf";
}

}

extern "C" int
ccwld_emit_lief (const char *input, const char *output, const char *kind,
                 const char *format, const char *entry, const char *note,
                 ccwld_error *error)
{
  if (input == nullptr || output == nullptr || kind == nullptr
      || !is_elf (format))
    {
      set_error (error, 2, "LIEF emitter requires an ELF input and output");
      return 0;
    }

  std::unique_ptr<LIEF::ELF::Binary> binary_ptr;
  try
    {
      binary_ptr = LIEF::ELF::Parser::parse (input);
    }
  catch (...)
    {
      set_error (error, 6, "LIEF failed to parse input object");
      return 0;
    }
  if (!binary_ptr)
    {
      set_error (error, 6, "LIEF failed to parse input object");
      return 0;
    }
  auto &binary = *binary_ptr;

  auto &header = binary.header ();
  const std::string output_kind (kind);
  if (output_kind == "reloc")
    header.file_type (LIEF::ELF::Header::FILE_TYPE::REL);
  else if (output_kind == "dso" || output_kind == "pie")
    header.file_type (LIEF::ELF::Header::FILE_TYPE::DYN);
  else if (output_kind == "exe")
    header.file_type (LIEF::ELF::Header::FILE_TYPE::EXEC);
  else
    {
      set_error (error, 2, "unsupported ELF output kind");
      return 0;
    }

  if (entry != nullptr)
    {
      for (auto &symbol : binary.symtab_symbols ())
        {
          if (symbol.name () == entry)
            {
              header.entrypoint (symbol.value ());
              break;
            }
        }
    }

  if (note != nullptr)
    {
      LIEF::ELF::Section producer (".note.ccw",
                                   LIEF::ELF::Section::TYPE::NOTE);
      producer.flags (0);
      producer.content (std::vector<uint8_t> (note, note + std::strlen (note)));
      binary.add (producer, false);
    }

  try
    {
      LIEF::ELF::Builder builder (binary);
      builder.build ();
      builder.write (output);
    }
  catch (...)
    {
      set_error (error, 6, "LIEF failed to emit ELF output");
      return 0;
    }
  return 1;
}
