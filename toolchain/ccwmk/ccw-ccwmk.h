#ifndef CCW_CCWMK_H
#define CCW_CCWMK_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define CCWMK_VERSION "0.1.0"
#define CCWMK_PLUGIN_ABI_VERSION 1u

typedef enum
{
  CCWMK_OK = 0,
  CCWMK_ERROR = 1
} ccwmk_status;

typedef struct ccwmk_target_t
{
  char *kind;
  char *name;
  char *language;
  char *path;
  size_t line;
} ccwmk_target_t;

typedef struct ccwmk_scanner_t ccwmk_scanner_t;

typedef struct ccwmk_edge_sink_t
{
  void *ctx;
  int (*add_edge) (void *ctx, const char *from, const char *to,
                   const char *kind, size_t line, char **error_message);
} ccwmk_edge_sink_t;

struct ccwmk_scanner_t
{
  const char *language;
  int (*scan) (const char *path, ccwmk_edge_sink_t *sink);
};

typedef struct ccwmk_registry_t
{
  ccwmk_scanner_t *scanners;
  size_t scanner_count;
  size_t scanner_capacity;
} ccwmk_registry_t;

typedef struct ccwmk_graph_t
{
  char *source_path;
  ccwmk_target_t *targets;
  size_t target_count;
  size_t target_capacity;
  ccwmk_registry_t registry;
} ccwmk_graph_t;

ccwmk_graph_t *ccwmk_graph_new (void);
void ccwmk_graph_free (ccwmk_graph_t *graph);
int ccwmk_graph_add_target (ccwmk_graph_t *graph, const char *kind,
                            const char *name, const char *language,
                            const char *path, size_t line,
                            char **error_message);
size_t ccwmk_graph_target_count (const ccwmk_graph_t *graph);
const ccwmk_target_t *ccwmk_graph_target_at (const ccwmk_graph_t *graph,
                                            size_t index);
const ccwmk_registry_t *ccwmk_graph_registry (const ccwmk_graph_t *graph);
ccwmk_registry_t *ccwmk_graph_registry_mut (ccwmk_graph_t *graph);

const char *ccwmk_target_kind (const ccwmk_target_t *target);
const char *ccwmk_target_name (const ccwmk_target_t *target);
const char *ccwmk_target_language (const ccwmk_target_t *target);
const char *ccwmk_target_path (const ccwmk_target_t *target);
size_t ccwmk_target_line (const ccwmk_target_t *target);

ccwmk_registry_t *ccwmk_registry_new (void);
void ccwmk_registry_free (ccwmk_registry_t *registry);
int ccwmk_registry_register_scanner (ccwmk_registry_t *registry,
                                     const ccwmk_scanner_t *scanner,
                                     char **error_message);
const ccwmk_scanner_t *ccwmk_registry_find_scanner (
    const ccwmk_registry_t *registry, const char *language);

int ccwmk_load (const char *path, ccwmk_graph_t **out_graph,
                char **error_message);
int ccwmk_build (ccwmk_graph_t *graph, char **error_message);

int ccwmk_plugin_init (ccwmk_registry_t *registry);

#ifdef __cplusplus
}
#endif

#endif

