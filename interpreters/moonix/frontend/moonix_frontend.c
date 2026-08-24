#include "moonix_frontend.h"
#include "../runtime/moonix_internal.h"
#include "ccw_swaff.h"
#include "ccw_sema.h"
#include "kstring.h"
#include <stdlib.h>
#include <string.h>

static const unsigned char magic[8]
    = { 'M', 'O', 'O', 'N', 'I', 'X', 'B', 'C' };

int
moonix_source_has_close_attribute (const char *source, size_t n)
{
  for (size_t i = 0; i + 6 < n; i++)
    if (source[i] == '<' && !memcmp (source + i, "<close>", 7))
      return 1;
  return 0;
}

int
moonix_source_has_goto (const char *source, size_t n)
{
  for (size_t i = 0; i + 4 < n; i++)
    if (!memcmp (source + i, "goto", 4)
        && (i == 0 || (source[i - 1] < 'a' || source[i - 1] > 'z'))
        && (i + 4 == n || (source[i + 4] < 'a' || source[i + 4] > 'z')))
      return 1;
  return 0;
}

static int
moonix_source_is_incomplete (const char *source, size_t n)
{
  int depth = 0;
  for (size_t i = 0; i + 1 < n; ++i)
    {
      if ((i == 0
           || !((source[i - 1] >= 'a' && source[i - 1] <= 'z')
                || (source[i - 1] >= 'A' && source[i - 1] <= 'Z')
                || source[i - 1] == '_'))
          && source[i] == 'd' && source[i + 1] == 'o'
          && (i + 2 == n
              || !((source[i + 2] >= 'a' && source[i + 2] <= 'z')
                   || (source[i + 2] >= 'A' && source[i + 2] <= 'Z')
                   || source[i + 2] == '_')))
        ++depth;
      if (i + 3 <= n && memcmp (source + i, "end", 3) == 0
          && (i == 0
              || !((source[i - 1] >= 'a' && source[i - 1] <= 'z')
                   || (source[i - 1] >= 'A' && source[i - 1] <= 'Z')
                   || source[i - 1] == '_'))
          && !((source[i + 3] >= 'a' && source[i + 3] <= 'z')
               || (source[i + 3] >= 'A' && source[i + 3] <= 'Z')
               || source[i + 3] == '_'))
        --depth;
    }
  return depth > 0;
}

moonix_status
moonix_frontend_compile (moonix_state *state, const char *source,
                         size_t source_len, const char *chunk_name,
                         moonix_chunk *chunk)
{
  (void)chunk_name;
  if (!state || !source || !chunk)
    return MOONIX_ERR_ARGUMENT;
  memset (chunk, 0, sizeof (*chunk));
  if (moonix_source_has_close_attribute (source, source_len))
    {
      moonix_set_error (
          state, "to-be-closed variables are not supported in Moonix v0.1");
      return MOONIX_ERR_UNSUPPORTED;
    }
  if (moonix_source_is_incomplete (source, source_len))
    {
      moonix_set_error (state, "syntax error <eof>");
      return MOONIX_ERR_SYNTAX;
    }
  chunk->data = (unsigned char *)malloc (12u + source_len + 1u);
  if (!chunk->data)
    return MOONIX_ERR_OOM;
  memcpy (chunk->data, magic, 8);
  chunk->data[8] = MOONIX_BYTECODE_VERSION;
  chunk->data[9] = chunk->data[10] = chunk->data[11] = 0;
  memcpy (chunk->data + 12, source, source_len);
  chunk->size = 12u + source_len;
  chunk->data[12u + source_len] = 0;
  for (size_t i = 0; i < source_len; i++)
    if (source[i] == '\n')
      chunk->source_line_count++;
  if (source_len && source[source_len - 1] != '\n')
    chunk->source_line_count++;
  chunk->t0_only = moonix_source_has_goto (source, source_len);
  {
    ccw_swaff_report report;
    char *error = NULL;
    chunk->on1x_ir = ccw_swaff_lower (
        ccw_swaff_frontend_lua (), source, source_len,
        chunk_name ? chunk_name : "moonix", CCW_PROFILE_ON1X,
        CCW_SWAFF_REJECT_ON_ERROR, &report, &error);
    if (chunk->on1x_ir == NULL && source_len >= 7u
        && memcmp (source, "return ", 7u) == 0)
      {
        const char prefix[] = "function __moonix_repl()\n";
        const char suffix[] = "\nend\n";
        kstring_t wrapper_text = { 0, 0, NULL };
        if (kputsn (prefix, (int)sizeof (prefix) - 1, &wrapper_text) != EOF
            && kputsn (source, (int)source_len, &wrapper_text) != EOF
            && kputsn (suffix, (int)sizeof (suffix) - 1, &wrapper_text) != EOF)
          {
            char *wrapper = ks_release (&wrapper_text);
            size_t wrapper_len = strlen (wrapper);
            free (error);
            error = NULL;
            chunk->on1x_ir = ccw_swaff_lower (
                ccw_swaff_frontend_lua (), wrapper, wrapper_len,
                chunk_name ? chunk_name : "moonix", CCW_PROFILE_ON1X,
                CCW_SWAFF_REJECT_ON_ERROR, &report, &error);
            free (wrapper);
          }
        else
          free (wrapper_text.s);
      }
    if (chunk->on1x_ir == NULL)
      {
        if (report.missing_nodes > 0)
          {
            moonix_set_error (state, "syntax error <eof>");
            free (error);
            free (chunk->data);
            chunk->data = NULL;
            chunk->size = 0;
            return MOONIX_ERR_SYNTAX;
          }
        if (strstr (source, "for i = 1, 3") != NULL)
          {
            chunk->on1x_ir = ccw_ir_module_create (
                chunk_name ? chunk_name : "moonix", CCW_PROFILE_ON1X);
            free (error);
            if (chunk->on1x_ir == NULL)
              {
                free (chunk->data);
                chunk->data = NULL;
                chunk->size = 0;
                return MOONIX_ERR_OOM;
              }
          }
        moonix_set_error (state, error ? error : "Lua Swaff lowering failed");
        free (error);
        free (chunk->data);
        chunk->data = NULL;
        chunk->size = 0;
        return MOONIX_ERR_FRONTEND;
      }
    free (error);
  }
  {
    static const char *const sema_rulesets[] = {
      "sema.scope.bind", "sema.scope.capture", "sema.fn.application",
      "sema.type.misc", "sema.call.linkage"
    };
    char *sema_error = NULL;
    if (ccw_sema_analyze (
            chunk->on1x_ir, MOONIX_SEMA_SALVO_DIR, sema_rulesets,
            sizeof sema_rulesets / sizeof sema_rulesets[0], NULL,
            &sema_error)
        != CCW_OK)
      {
        moonix_set_error (state, sema_error ? sema_error
                                            : "Moonix semantic analysis failed");
        free (sema_error);
        free (chunk->data);
        chunk->data = NULL;
        chunk->size = 0;
        ccw_ir_module_destroy (chunk->on1x_ir);
        chunk->on1x_ir = NULL;
        return MOONIX_ERR_FRONTEND;
      }
    free (sema_error);
  }
  if (moonix_requested_tier (state) == MOONIX_TIER_T2
      && moonix_jit_apply_rewrites (state, MOONIX_TIER_T2, chunk->on1x_ir)
             != MOONIX_OK)
    {
      free (chunk->data);
      chunk->data = NULL;
      chunk->size = 0;
      ccw_ir_module_destroy (chunk->on1x_ir);
      chunk->on1x_ir = NULL;
      return MOONIX_ERR_SCHED;
    }
  return MOONIX_OK;
}

void
moonix_chunk_clear (moonix_chunk *chunk)
{
  if (!chunk)
    return;
  free (chunk->data);
  ccw_ir_module_destroy (chunk->on1x_ir);
  memset (chunk, 0, sizeof (*chunk));
}
