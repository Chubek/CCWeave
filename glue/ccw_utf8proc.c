/* ccw_utf8proc.c — Glue wrappers around the vendored utf8proc library.
 *
 * Each function is a thin adapter that maps the utf8proc C types to the
 * stable Glue ABI types declared in GlueSTD.h:
 *   - utf8proc_uint8_t*   → const uint8_t* / uint8_t*
 *   - utf8proc_int32_t    → int32_t
 *   - utf8proc_ssize_t    → int64_t (wider, safe for all platforms)
 *   - utf8proc_bool       → bool
 *
 * Ownership rules (§GlueSTD.h):
 *   - Functions returning char* malloc the result; caller frees.
 *   - Input strings are never retained by the callee.
 */

#include "GlueSTD.h"
#include "../third_party/utf8proc/utf8proc.h"

#include <stdlib.h>
#include <string.h>

/* ─── version / error messages ─────────────────────────────────────── */

char *
ccw_utf8proc_version (void)
{
  const char *v = utf8proc_version ();
  if (!v)
    return NULL;
  size_t len = strlen (v);
  char *copy = (char *)malloc (len + 1);
  if (copy)
    memcpy (copy, v, len + 1);
  return copy;
}

const char *
ccw_utf8proc_errmsg (int64_t errcode)
{
  return utf8proc_errmsg ((utf8proc_ssize_t)errcode);
}

/* ─── codepoint encode / decode ────────────────────────────────────── */

int64_t
ccw_utf8proc_iterate (const uint8_t *str, int64_t strlen,
                      int32_t *codepoint_out)
{
  return (int64_t)utf8proc_iterate ((const utf8proc_uint8_t *)str,
                                    (utf8proc_ssize_t)strlen,
                                    (utf8proc_int32_t *)codepoint_out);
}

int64_t
ccw_utf8proc_encode_char (int32_t codepoint, uint8_t *dst)
{
  return (int64_t)utf8proc_encode_char ((utf8proc_int32_t)codepoint,
                                        (utf8proc_uint8_t *)dst);
}

bool
ccw_utf8proc_codepoint_valid (int32_t codepoint)
{
  return (bool)utf8proc_codepoint_valid ((utf8proc_int32_t)codepoint);
}

/* ─── character width ──────────────────────────────────────────────── */

int
ccw_utf8proc_charwidth (int32_t codepoint)
{
  return utf8proc_charwidth ((utf8proc_int32_t)codepoint);
}

/* ─── case conversion ──────────────────────────────────────────────── */

int32_t
ccw_utf8proc_tolower (int32_t c)
{
  return (int32_t)utf8proc_tolower ((utf8proc_int32_t)c);
}

int32_t
ccw_utf8proc_toupper (int32_t c)
{
  return (int32_t)utf8proc_toupper ((utf8proc_int32_t)c);
}

int32_t
ccw_utf8proc_totitle (int32_t c)
{
  return (int32_t)utf8proc_totitle ((utf8proc_int32_t)c);
}

/* ─── category ─────────────────────────────────────────────────────── */

int
ccw_utf8proc_category (int32_t codepoint)
{
  return (int)utf8proc_category ((utf8proc_int32_t)codepoint);
}

/* ─── normalization shortcuts ──────────────────────────────────────── */

char *
ccw_utf8proc_NFD (const uint8_t *str)
{
  return (char *)utf8proc_NFD ((const utf8proc_uint8_t *)str);
}

char *
ccw_utf8proc_NFC (const uint8_t *str)
{
  return (char *)utf8proc_NFC ((const utf8proc_uint8_t *)str);
}

char *
ccw_utf8proc_NFKD (const uint8_t *str)
{
  return (char *)utf8proc_NFKD ((const utf8proc_uint8_t *)str);
}

char *
ccw_utf8proc_NFKC (const uint8_t *str)
{
  return (char *)utf8proc_NFKC ((const utf8proc_uint8_t *)str);
}

char *
ccw_utf8proc_NFKC_Casefold (const uint8_t *str)
{
  return (char *)utf8proc_NFKC_Casefold ((const utf8proc_uint8_t *)str);
}

/* ─── generic map ──────────────────────────────────────────────────── */

int64_t
ccw_utf8proc_map (const uint8_t *str, int64_t len, uint8_t **dst_out,
                  int options)
{
  return (int64_t)utf8proc_map ((const utf8proc_uint8_t *)str,
                                (utf8proc_ssize_t)len,
                                (utf8proc_uint8_t **)dst_out,
                                (utf8proc_option_t)options);
}

/* ─── decompose a single codepoint ─────────────────────────────────── */

int64_t
ccw_utf8proc_decompose_char (int32_t codepoint, int32_t *dst,
                             int64_t bufsize, int options,
                             int *last_boundclass)
{
  return (int64_t)utf8proc_decompose_char ((utf8proc_int32_t)codepoint,
                                           (utf8proc_int32_t *)dst,
                                           (utf8proc_ssize_t)bufsize,
                                           (utf8proc_option_t)options,
                                           last_boundclass);
}

/* ─── reencode codepoint buffer to UTF-8 ───────────────────────────── */

int64_t
ccw_utf8proc_reencode (int32_t *buffer, int64_t length, int options)
{
  return (int64_t)utf8proc_reencode ((utf8proc_int32_t *)buffer,
                                     (utf8proc_ssize_t)length,
                                     (utf8proc_option_t)options);
}

/* ─── grapheme break ───────────────────────────────────────────────── */

bool
ccw_utf8proc_grapheme_break (int32_t c1, int32_t c2)
{
  return (bool)utf8proc_grapheme_break ((utf8proc_int32_t)c1,
                                        (utf8proc_int32_t)c2);
}
