#include "../ccw-ccwmk.h"

#include <stdlib.h>
#include <string.h>

static char *
ccwmk_strdup (const char *s)
{
  if (!s)
    return NULL;
  size_t n = strlen (s) + 1;
  char *copy = (char *)malloc (n);
  if (copy)
    memcpy (copy, s, n);
  return copy;
}

static void
set_error (char **error_message, const char *message)
{
  if (!error_message)
    return;
  free (*error_message);
  *error_message = ccwmk_strdup (message);
}

static ccwmk_target_t *
find_target (ccwmk_graph_t *graph, const char *name)
{
  if (!graph || !name)
    return NULL;
  for (size_t i = 0; i < graph->target_count; i++)
    if (graph->targets[i].name && !strcmp (graph->targets[i].name, name))
      return &graph->targets[i];
  return NULL;
}

static int
ensure_target_capacity (ccwmk_graph_t *graph, size_t need)
{
  if (graph->target_capacity >= need)
    return 1;
  size_t next = graph->target_capacity ? graph->target_capacity * 2 : 8;
  while (next < need)
    next *= 2;
  void *p = realloc (graph->targets, next * sizeof (*graph->targets));
  if (!p)
    return 0;
  graph->targets = (ccwmk_target_t *)p;
  graph->target_capacity = next;
  return 1;
}

ccwmk_graph_t *
ccwmk_graph_new (void)
{
  return (ccwmk_graph_t *)calloc (1, sizeof (ccwmk_graph_t));
}

void
ccwmk_graph_free (ccwmk_graph_t *graph)
{
  if (!graph)
    return;
  free (graph->source_path);
  for (size_t i = 0; i < graph->target_count; i++)
    {
      free (graph->targets[i].kind);
      free (graph->targets[i].name);
      free (graph->targets[i].language);
      free (graph->targets[i].path);
    }
  free (graph->targets);
  ccwmk_registry_free (&graph->registry);
  free (graph);
}

int
ccwmk_graph_add_target (ccwmk_graph_t *graph, const char *kind,
                        const char *name, const char *language,
                        const char *path, size_t line, char **error_message)
{
  if (!graph || !name || !kind)
    {
      set_error (error_message, "ccwmk: invalid target");
      return 0;
    }
  ccwmk_target_t *target = find_target (graph, name);
  if (!target)
    {
      if (!ensure_target_capacity (graph, graph->target_count + 1))
        {
          set_error (error_message, "ccwmk: out of memory");
          return 0;
        }
      target = &graph->targets[graph->target_count++];
      memset (target, 0, sizeof (*target));
    }
  else
    {
      free (target->kind);
      free (target->language);
      free (target->path);
    }
  target->kind = ccwmk_strdup (kind);
  target->name = ccwmk_strdup (name);
  target->language = ccwmk_strdup (language);
  target->path = ccwmk_strdup (path);
  target->line = line;
  if (!target->kind || !target->name)
    {
      set_error (error_message, "ccwmk: out of memory");
      return 0;
    }
  return 1;
}

size_t
ccwmk_graph_target_count (const ccwmk_graph_t *graph)
{
  return graph ? graph->target_count : 0;
}

const ccwmk_target_t *
ccwmk_graph_target_at (const ccwmk_graph_t *graph, size_t index)
{
  if (!graph || index >= graph->target_count)
    return NULL;
  return &graph->targets[index];
}

const ccwmk_registry_t *
ccwmk_graph_registry (const ccwmk_graph_t *graph)
{
  return graph ? &graph->registry : NULL;
}

ccwmk_registry_t *
ccwmk_graph_registry_mut (ccwmk_graph_t *graph)
{
  return graph ? &graph->registry : NULL;
}

const char *
ccwmk_target_kind (const ccwmk_target_t *target) { return target ? target->kind : NULL; }
const char *
ccwmk_target_name (const ccwmk_target_t *target) { return target ? target->name : NULL; }
const char *
ccwmk_target_language (const ccwmk_target_t *target) { return target ? target->language : NULL; }
const char *
ccwmk_target_path (const ccwmk_target_t *target) { return target ? target->path : NULL; }
size_t
ccwmk_target_line (const ccwmk_target_t *target) { return target ? target->line : 0; }
