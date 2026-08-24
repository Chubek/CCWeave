#include "vendored-bridge.h"

#define SIMDE_ENABLE_NATIVE_ALIASES
#include <simde/x86/avx.h>
#include <simde/x86/sse2.h>

#include <string.h>

static ccw_v128
v128_from (const void *p)
{
  ccw_v128 value;
  memcpy (value.bytes, p, sizeof (value.bytes));
  return value;
}

static ccw_v256
v256_from (const void *p)
{
  ccw_v256 value;
  memcpy (value.bytes, p, sizeof (value.bytes));
  return value;
}

ccw_v128
ccw_simde_load (const void *p)
{
  return v128_from (p);
}

ccw_v128
ccw_simde_loadu (const void *p)
{
  return v128_from (p);
}

void
ccw_simde_store (void *p, ccw_v128 value)
{
  memcpy (p, value.bytes, sizeof (value.bytes));
}

void
ccw_simde_storeu (void *p, ccw_v128 value)
{
  ccw_simde_store (p, value);
}

ccw_v256
ccw_simde_load256 (const void *p)
{
  return v256_from (p);
}

ccw_v256
ccw_simde_loadu256 (const void *p)
{
  return v256_from (p);
}

void
ccw_simde_store256 (void *p, ccw_v256 value)
{
  memcpy (p, value.bytes, sizeof (value.bytes));
}

void
ccw_simde_storeu256 (void *p, ccw_v256 value)
{
  ccw_simde_store256 (p, value);
}

ccw_v128
ccw_simde_add_i32x4 (ccw_v128 a, ccw_v128 b)
{
  simde__m128i result = simde_mm_add_epi32 (
      simde_mm_loadu_si128 ((const simde__m128i *)a.bytes),
      simde_mm_loadu_si128 ((const simde__m128i *)b.bytes));
  ccw_v128 out;
  simde_mm_storeu_si128 ((simde__m128i *)out.bytes, result);
  return out;
}

ccw_v128
ccw_simde_sub_i32x4 (ccw_v128 a, ccw_v128 b)
{
  simde__m128i result = simde_mm_sub_epi32 (
      simde_mm_loadu_si128 ((const simde__m128i *)a.bytes),
      simde_mm_loadu_si128 ((const simde__m128i *)b.bytes));
  ccw_v128 out;
  simde_mm_storeu_si128 ((simde__m128i *)out.bytes, result);
  return out;
}

ccw_v128
ccw_simde_mul_i32x4 (ccw_v128 a, ccw_v128 b)
{
  simde__m128i result = simde_mm_mullo_epi32 (
      simde_mm_loadu_si128 ((const simde__m128i *)a.bytes),
      simde_mm_loadu_si128 ((const simde__m128i *)b.bytes));
  ccw_v128 out;
  simde_mm_storeu_si128 ((simde__m128i *)out.bytes, result);
  return out;
}

ccw_v128
ccw_simde_add_f32x4 (ccw_v128 a, ccw_v128 b)
{
  simde__m128 result
      = simde_mm_add_ps (simde_mm_loadu_ps ((const float *)a.bytes),
                         simde_mm_loadu_ps ((const float *)b.bytes));
  ccw_v128 out;
  simde_mm_storeu_ps ((float *)out.bytes, result);
  return out;
}

ccw_v128
ccw_simde_sub_f32x4 (ccw_v128 a, ccw_v128 b)
{
  simde__m128 result
      = simde_mm_sub_ps (simde_mm_loadu_ps ((const float *)a.bytes),
                         simde_mm_loadu_ps ((const float *)b.bytes));
  ccw_v128 out;
  simde_mm_storeu_ps ((float *)out.bytes, result);
  return out;
}

ccw_v128
ccw_simde_mul_f32x4 (ccw_v128 a, ccw_v128 b)
{
  simde__m128 result
      = simde_mm_mul_ps (simde_mm_loadu_ps ((const float *)a.bytes),
                         simde_mm_loadu_ps ((const float *)b.bytes));
  ccw_v128 out;
  simde_mm_storeu_ps ((float *)out.bytes, result);
  return out;
}

ccw_v128
ccw_simde_div_f32x4 (ccw_v128 a, ccw_v128 b)
{
  simde__m128 result
      = simde_mm_div_ps (simde_mm_loadu_ps ((const float *)a.bytes),
                         simde_mm_loadu_ps ((const float *)b.bytes));
  ccw_v128 out;
  simde_mm_storeu_ps ((float *)out.bytes, result);
  return out;
}

ccw_v256
ccw_simde_add_f32x8 (ccw_v256 a, ccw_v256 b)
{
  simde__m256 result
      = simde_mm256_add_ps (simde_mm256_loadu_ps ((const float *)a.bytes),
                            simde_mm256_loadu_ps ((const float *)b.bytes));
  ccw_v256 out;
  simde_mm256_storeu_ps ((float *)out.bytes, result);
  return out;
}

ccw_v256
ccw_simde_mul_f32x8 (ccw_v256 a, ccw_v256 b)
{
  simde__m256 result
      = simde_mm256_mul_ps (simde_mm256_loadu_ps ((const float *)a.bytes),
                            simde_mm256_loadu_ps ((const float *)b.bytes));
  ccw_v256 out;
  simde_mm256_storeu_ps ((float *)out.bytes, result);
  return out;
}

ccw_v128
ccw_simde_shuffle (ccw_v128 a, const int indices[4])
{
  ccw_v128 out;
  const float *input = (const float *)a.bytes;
  float *output = (float *)out.bytes;
  for (int index = 0; index < 4; index++)
    output[index] = (indices[index] >= 0 && indices[index] < 4)
                        ? input[indices[index]]
                        : 0.0f;
  return out;
}

ccw_v128
ccw_simde_select (ccw_v128 mask, ccw_v128 a, ccw_v128 b)
{
  ccw_v128 out;
  for (int index = 0; index < 16; index++)
    out.bytes[index] = (mask.bytes[index] & a.bytes[index])
                       | ((unsigned char)~mask.bytes[index] & b.bytes[index]);
  return out;
}

float
ccw_simde_hreduce_add_f32x4 (ccw_v128 a)
{
  const float *values = (const float *)a.bytes;
  return values[0] + values[1] + values[2] + values[3];
}

double
ccw_simde_hreduce_add_f64x2 (ccw_v128 a)
{
  const double *values = (const double *)a.bytes;
  return values[0] + values[1];
}
