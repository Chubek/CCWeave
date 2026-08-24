/* Moonix tier-plan manager — MOONIX §6.
 *
 * Sched presently exposes sealing/revalidation and Oeuph execution, but no
 * host API for executing kernel nodes into a native code buffer.  We still
 * make plan admission strict and deterministic; unavailable native emission
 * degrades execution to T0 as required by §3. */

#include "kstring.h"
#include "moonix_internal.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct
{
  unsigned id;
  unsigned kind;
  char name[128];
  char rest[512];
} plan_node;

static int
edge_present (const char *text, unsigned from, unsigned to)
{
  char needle[64];
  snprintf (needle, sizeof (needle), "edge %u %u\n", from, to);
  return strstr (text, needle) != NULL;
}

static int
validate_on1x_plan (const ccw_plan *plan, char *error, size_t capacity)
{
  const char *text = ccw_plan_text (plan);
  char *copy;
  char *line;
  unsigned barrier = 0;
  unsigned backend[32];
  size_t backend_count = 0;
  unsigned vm_nodes[64];
  size_t vm_count = 0;
  int has_ic = 0, has_barrier = 0, has_safepoint = 0;

  kstring_t copy_text = { 0, 0, NULL };
  if (kputs (text, &copy_text) == EOF)
    {
      snprintf (error, capacity, "out of memory validating plan");
      return 0;
    }
  copy = ks_release (&copy_text);
  for (line = strtok (copy, "\n"); line != NULL; line = strtok (NULL, "\n"))
    {
      plan_node node;
      node.rest[0] = '\0';
      if (sscanf (line, "node %u %u %127s %511[^\n]", &node.id, &node.kind,
                  node.name, node.rest)
          < 3)
        continue;
      if (node.kind == 3 && strcmp (node.name, "on1x-complete") == 0)
        barrier = node.id;
      if (node.kind == 1 && strstr (node.rest, "vm.") != NULL)
        {
          if (vm_count < sizeof (vm_nodes) / sizeof (vm_nodes[0]))
            vm_nodes[vm_count++] = node.id;
          if (strstr (node.rest, "vm.inline-cache") != NULL)
            has_ic = 1;
          if (strstr (node.rest, "vm.gc-barrier-insertion") != NULL)
            has_barrier = 1;
          if (strstr (node.rest, "vm.safepoint-insertion") != NULL)
            has_safepoint = 1;
        }
      if (node.kind == 1 && strstr (node.rest, "codegen.") != NULL
          && strstr (node.rest, "codegen.x86-64") != NULL)
        {
          if (backend_count < sizeof (backend) / sizeof (backend[0]))
            backend[backend_count++] = node.id;
        }
    }
  free (copy);

  if (barrier == 0 || !has_ic || !has_barrier || !has_safepoint
      || backend_count == 0)
    {
      snprintf (error, capacity,
                "plan lacks on1x-complete or mandatory On1x capabilities");
      return 0;
    }
  for (size_t i = 0; i < vm_count; ++i)
    {
      if (vm_nodes[i] >= barrier || !edge_present (text, vm_nodes[i], barrier))
        {
          snprintf (error, capacity,
                    "vm node is not ordered before on1x-complete");
          return 0;
        }
    }
  for (size_t i = 0; i < backend_count; ++i)
    {
      if (backend[i] <= barrier || !edge_present (text, barrier, backend[i]))
        {
          snprintf (error, capacity,
                    "codegen is not ordered after on1x-complete");
          return 0;
        }
    }
  return 1;
}

static moonix_status
load_plan (moonix_state *state, moonix_tier tier)
{
  int index = (int)tier - (int)MOONIX_TIER_T1;
  char path[1024];
  ccw_sched_error sched_error = { 0 };
  ccw_plan *plan = NULL;
  if (state->plans[index] != NULL)
    return MOONIX_OK;
  snprintf (path, sizeof (path), "%s/T%d.lua", state->sched_dir, (int)tier);
  if (!ccw_sched_run_script (path, state->manifest_dir, &plan, &sched_error))
    {
      moonix_set_error (state, sched_error.message);
      return MOONIX_ERR_SCHED;
    }
  if (!validate_on1x_plan (plan, state->error, sizeof (state->error))
      || !ccw_plan_hash (plan, state->plan_hashes[index]))
    {
      ccw_plan_free (plan);
      if (state->error[0] == '\0')
        moonix_set_error (state, "could not hash Moonix plan");
      return MOONIX_ERR_SCHED;
    }
  state->plans[index] = plan;
  return MOONIX_OK;
}

moonix_status
moonix_jit_select_tier (moonix_state *state, moonix_tier tier)
{
  moonix_status status;
  if (tier == MOONIX_TIER_T0)
    {
      state->requested_tier = tier;
      state->active_tier = MOONIX_TIER_T0;
      state->error[0] = '\0';
      return MOONIX_OK;
    }
  status = load_plan (state, tier);
  if (status != MOONIX_OK)
    return status;
  state->requested_tier = tier;
  /* T1 becomes active only after an On1x chunk passes admission.  T2 is a
   * v0.2 execution feature and always falls back to the semantic T0. */
  state->active_tier = MOONIX_TIER_T0;
  state->error[0] = '\0';
  return MOONIX_OK;
}

moonix_status
moonix_jit_apply_rewrites (moonix_state *state, moonix_tier tier, ccw_ir *ir)
{
  ccw_sched_error error = { 0 };
  if (state == NULL || ir == NULL || tier != MOONIX_TIER_T2)
    return MOONIX_OK;
  if (load_plan (state, tier) != MOONIX_OK)
    return MOONIX_ERR_SCHED;
  if (!ccw_rewrite_scheme_apply (
          state->plans[(int)tier - (int)MOONIX_TIER_T1], ir,
          state->manifest_dir, ccw_oeuph_default_budget(),
          CCW_COST_PERFORMANCE, NULL, 0, NULL, &error))
    {
      moonix_set_error (state, error.message);
      return MOONIX_ERR_SCHED;
    }
  return MOONIX_OK;
}
