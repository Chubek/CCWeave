/* salvo-libc: NUL-terminated string operations. */

#include <string.h>

size_t
strlen (const char *s)
{
  const char *p = s;
  while (*p)
    ++p;
  return (size_t)(p - s);
}

size_t
strnlen (const char *s, size_t maxlen)
{
  size_t n = 0;
  while (n < maxlen && s[n])
    ++n;
  return n;
}

int
strcmp (const char *a, const char *b)
{
  const unsigned char *pa = (const unsigned char *)a;
  const unsigned char *pb = (const unsigned char *)b;
  while (*pa != '\0' && *pa == *pb)
    {
      ++pa;
      ++pb;
    }
  return (int)*pa - (int)*pb;
}

int
strncmp (const char *a, const char *b, size_t n)
{
  const unsigned char *pa = (const unsigned char *)a;
  const unsigned char *pb = (const unsigned char *)b;
  while (n != 0)
    {
      if (*pa != *pb)
        return (int)*pa - (int)*pb;
      if (*pa == '\0')
        return 0;
      ++pa;
      ++pb;
      --n;
    }
  return 0;
}

char *
strcpy (char *dst, const char *src)
{
  char *out = dst;
  while ((*dst++ = *src++) != '\0')
    ;
  return out;
}

char *
strncpy (char *dst, const char *src, size_t n)
{
  char *out = dst;
  while (n != 0 && *src != '\0')
    {
      *dst++ = *src++;
      --n;
    }
  while (n != 0)
    {
      *dst++ = '\0';
      --n;
    }
  return out;
}

char *
strcat (char *dst, const char *src)
{
  char *out = dst;
  while (*dst)
    ++dst;
  while ((*dst++ = *src++) != '\0')
    ;
  return out;
}

char *
strncat (char *dst, const char *src, size_t n)
{
  char *out = dst;
  while (*dst)
    ++dst;
  while (n != 0 && *src != '\0')
    {
      *dst++ = *src++;
      --n;
    }
  *dst = '\0';
  return out;
}

char *
strchr (const char *s, int c)
{
  char want = (char)c;
  for (;; ++s)
    {
      if (*s == want)
        return (char *)s;
      if (*s == '\0')
        return NULL;
    }
}

char *
strrchr (const char *s, int c)
{
  const char *last = NULL;
  char want = (char)c;
  for (;; ++s)
    {
      if (*s == want)
        last = s;
      if (*s == '\0')
        return (char *)last;
    }
}

char *
strstr (const char *haystack, const char *needle)
{
  size_t needle_len = strlen (needle);
  if (needle_len == 0)
    return (char *)haystack;
  for (; *haystack != '\0'; ++haystack)
    {
      if (*haystack == *needle && strncmp (haystack, needle, needle_len) == 0)
        return (char *)haystack;
    }
  return NULL;
}

char *
strpbrk (const char *s, const char *accept)
{
  for (; *s != '\0'; ++s)
    if (strchr (accept, (unsigned char)*s) != NULL)
      return (char *)s;
  return NULL;
}

size_t
strspn (const char *s, const char *accept)
{
  size_t n = 0;
  while (s[n] != '\0' && strchr (accept, (unsigned char)s[n]) != NULL)
    ++n;
  return n;
}

size_t
strcspn (const char *s, const char *reject)
{
  size_t n = 0;
  while (s[n] != '\0' && strchr (reject, (unsigned char)s[n]) == NULL)
    ++n;
  return n;
}
