/* salvo-libc: string-to-integer conversion.
 *
 * One 64-bit core feeds strtol/strtoul/strtoll/strtoull and the atoi/atol
 * shorthands. LP64 makes long and long long the same width, so clamping
 * happens only at the public wrappers. */

#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <stdlib.h>

/* Parses the longest valid prefix in [2..36]/auto base.
 * Returns the magnitude; *neg reports a leading '-'; *any reports that at
 * least one digit was consumed; endptr semantics follow the standard. */
static unsigned long long
salvo_strtoull_core (const char *s, char **endptr, int base, int *neg,
                     int *any)
{
  const char *p = s;
  unsigned long long value = 0;
  int digits = 0;
  int overflow = 0;

  while (isspace ((unsigned char)*p))
    ++p;
  if (*p == '+' || *p == '-')
    {
      *neg = (*p == '-');
      ++p;
    }
  if ((base == 0 || base == 16) && p[0] == '0' && (p[1] == 'x' || p[1] == 'X')
      && isxdigit ((unsigned char)p[2]))
    {
      p += 2;
      base = 16;
    }
  else if (base == 0)
    {
      base = (p[0] == '0') ? 8 : 10;
    }
  for (;; ++p)
    {
      unsigned char c = (unsigned char)*p;
      int digit;
      if (isdigit (c))
        digit = c - '0';
      else if (c >= 'a' && c <= 'z')
        digit = c - 'a' + 10;
      else if (c >= 'A' && c <= 'Z')
        digit = c - 'A' + 10;
      else
        break;
      if (digit >= base)
        break;
      if (!overflow)
        {
          if (value > (ULLONG_MAX - (unsigned long long)digit)
                          / (unsigned long long)base)
            {
              overflow = 1;
              value = ULLONG_MAX;
            }
          else
            {
              value = value * (unsigned long long)base
                      + (unsigned long long)digit;
            }
        }
      digits = 1;
    }
  if (!digits)
    {
      if (endptr != NULL)
        *endptr = (char *)s;
      *any = 0;
      return 0;
    }
  if (overflow)
    errno = ERANGE;
  if (endptr != NULL)
    *endptr = (char *)p;
  *any = 1;
  return value;
}

long
strtol (const char *s, char **endptr, int base)
{
  int neg = 0;
  int any = 0;
  unsigned long long value = salvo_strtoull_core (s, endptr, base, &neg, &any);
  if (!any)
    return 0;
  if (neg)
    {
      if (value > (unsigned long long)LONG_MAX + 1ULL)
        {
          errno = ERANGE;
          return LONG_MIN;
        }
      if (value == (unsigned long long)LONG_MAX + 1ULL)
        return LONG_MIN;
      return -(long)value;
    }
  if (value > (unsigned long long)LONG_MAX)
    {
      errno = ERANGE;
      return LONG_MAX;
    }
  return (long)value;
}

long long
strtoll (const char *s, char **endptr, int base)
{
  /* LP64: long long and long share range, so reuse the same clamps. */
  return (long long)strtol (s, endptr, base);
}

unsigned long
strtoul (const char *s, char **endptr, int base)
{
  int neg = 0;
  int any = 0;
  unsigned long long value = salvo_strtoull_core (s, endptr, base, &neg, &any);
  if (!any)
    return 0;
  /* A leading '-' is legal for the unsigned conversions and negates the
   * result in unsigned arithmetic (C11 7.22.1.4p5). */
  return (unsigned long)(neg ? 0ULL - value : value);
}

unsigned long long
strtoull (const char *s, char **endptr, int base)
{
  return (unsigned long long)strtoul (s, endptr, base);
}

int
atoi (const char *s)
{
  return (int)strtol (s, NULL, 10);
}

long
atol (const char *s)
{
  return strtol (s, NULL, 10);
}
