/* salvo-libc <stdio.h>: formatted output.
 *
 * One parser drives two emitters (bounded buffer, stream). Supported
 * conversions: %d %i %u %x %X %o %c %s %p %% with flags -0+<space>#,
 * width/precision (including *), and lengths hh h l ll z t.
 * Floating-point conversions are deliberately absent until salvo-libm;
 * encountering one emits the directive verbatim, like any unknown
 * conversion. */

#include <ctype.h>
#include <limits.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

typedef void (*salvo_emit_fn) (void *ctx, char c);

typedef struct
{
  char *buf;
  size_t size;
  size_t len; /* would-be length, for the return value */
} salvo_buf_ctx;

static void
salvo_buf_emit (void *opaque, char c)
{
  salvo_buf_ctx *ctx = (salvo_buf_ctx *)opaque;
  if (ctx->size != 0 && ctx->len < ctx->size - 1)
    ctx->buf[ctx->len] = c;
  ++ctx->len;
}

typedef struct
{
  FILE *stream;
  int err;
} salvo_file_ctx;

static void
salvo_file_emit (void *opaque, char c)
{
  salvo_file_ctx *ctx = (salvo_file_ctx *)opaque;
  if (!ctx->err && fputc ((unsigned char)c, ctx->stream) == EOF)
    ctx->err = 1;
}

enum salvo_length
{
  SALVO_LEN_NONE,
  SALVO_LEN_HH,
  SALVO_LEN_H,
  SALVO_LEN_L,
  SALVO_LEN_LL,
  SALVO_LEN_Z,
  SALVO_LEN_T
};

typedef struct
{
  int minus;
  int zero;
  int plus;
  int space;
  int alt;
  int width;
  int precision; /* -1 = none */
  enum salvo_length length;
} salvo_spec;

static void
salvo_emit_repeat (salvo_emit_fn emit, void *ctx, char c, int count)
{
  while (count-- > 0)
    emit (ctx, c);
}

static void
salvo_emit_string (salvo_emit_fn emit, void *ctx, const char *s, size_t n)
{
  while (n-- != 0)
    emit (ctx, *s++);
}

/* Renders one integer directive into the emitter. Returns characters
 * emitted. */
static int
salvo_format_int (salvo_emit_fn emit, void *ctx, unsigned long long magnitude,
                  int negative, unsigned base, int uppercase,
                  const salvo_spec *spec)
{
  char digits[64];
  char prefix[2];
  int ndigits = 0;
  int nprefix = 0;
  int body;
  int pad;
  int count = 0;
  const char *alphabet = uppercase ? "0123456789ABCDEF" : "0123456789abcdef";

  if (magnitude == 0)
    {
      /* A zero value with precision 0 prints no digits at all. */
      if (spec->precision != 0)
        digits[ndigits++] = '0';
    }
  else
    {
      while (magnitude != 0 && ndigits < (int)sizeof (digits))
        {
          digits[ndigits++] = alphabet[magnitude % base];
          magnitude /= base;
        }
    }
  if (negative)
    prefix[nprefix++] = '-';
  else if (spec->plus)
    prefix[nprefix++] = '+';
  else if (spec->space)
    prefix[nprefix++] = ' ';
  else if (spec->alt && base == 16 && !(ndigits == 1 && digits[0] == '0')
           && ndigits != 0)
    {
      prefix[nprefix++] = '0';
      prefix[nprefix++] = uppercase ? 'X' : 'x';
    }
  {
    int precision_pad = 0;
    if (spec->precision > ndigits)
      precision_pad = spec->precision - ndigits;
    if (spec->alt && base == 8 && precision_pad == 0
        && (ndigits == 0 || digits[ndigits - 1] != '0'))
      precision_pad = 1;

    body = nprefix + precision_pad + ndigits;
    pad = spec->width > body ? spec->width - body : 0;

    if (!spec->minus)
      {
        if (spec->zero && spec->precision < 0)
          {
            /* Zero padding follows any sign/prefix. */
            salvo_emit_string (emit, ctx, prefix, (size_t)nprefix);
            salvo_emit_repeat (emit, ctx, '0', pad);
            count += nprefix + pad;
            pad = 0;
          }
        else
          {
            salvo_emit_repeat (emit, ctx, ' ', pad);
            count += pad;
            salvo_emit_string (emit, ctx, prefix, (size_t)nprefix);
            count += nprefix;
          }
      }
    else
      {
        salvo_emit_string (emit, ctx, prefix, (size_t)nprefix);
        count += nprefix;
      }
    salvo_emit_repeat (emit, ctx, '0', precision_pad);
    count += precision_pad;
    while (ndigits > 0)
      emit (ctx, digits[--ndigits]), ++count;
    if (spec->minus && pad > 0)
      {
        salvo_emit_repeat (emit, ctx, ' ', pad);
        count += pad;
      }
  }
  return count;
}

static int
salvo_vformat (salvo_emit_fn emit, void *ctx, const char *fmt, va_list ap)
{
  int count = 0;

  for (const char *p = fmt; *p != '\0';)
    {
      salvo_spec spec;
      char conv;

      if (*p != '%')
        {
          const char *start = p;
          size_t run;
          while (*p != '\0' && *p != '%')
            ++p;
          run = (size_t)(p - start);
          salvo_emit_string (emit, ctx, start, run);
          count += (int)run;
          continue;
        }
      ++p;

      memset (&spec, 0, sizeof (spec));
      spec.precision = -1;
      for (;; ++p)
        {
          if (*p == '-')
            spec.minus = 1;
          else if (*p == '0')
            spec.zero = 1;
          else if (*p == '+')
            spec.plus = 1;
          else if (*p == ' ')
            spec.space = 1;
          else if (*p == '#')
            spec.alt = 1;
          else
            break;
        }
      if (*p == '*')
        {
          spec.width = va_arg (ap, int);
          if (spec.width < 0)
            {
              spec.minus = 1;
              spec.width = -spec.width;
            }
          ++p;
        }
      else
        {
          while (isdigit ((unsigned char)*p))
            {
              if (spec.width < INT_MAX / 10)
                spec.width = spec.width * 10 + (*p - '0');
              ++p;
            }
        }
      if (*p == '.')
        {
          ++p;
          spec.precision = 0;
          if (*p == '*')
            {
              spec.precision = va_arg (ap, int);
              ++p;
            }
          else
            {
              while (isdigit ((unsigned char)*p))
                {
                  if (spec.precision < INT_MAX / 10)
                    spec.precision = spec.precision * 10 + (*p - '0');
                  ++p;
                }
            }
          if (spec.precision < 0)
            spec.precision = -1;
        }
      if (*p == 'h')
        {
          ++p;
          if (*p == 'h')
            {
              ++p;
              spec.length = SALVO_LEN_HH;
            }
          else
            spec.length = SALVO_LEN_H;
        }
      else if (*p == 'l')
        {
          ++p;
          if (*p == 'l')
            {
              ++p;
              spec.length = SALVO_LEN_LL;
            }
          else
            spec.length = SALVO_LEN_L;
        }
      else if (*p == 'z')
        {
          ++p;
          spec.length = SALVO_LEN_Z;
        }
      else if (*p == 't')
        {
          ++p;
          spec.length = SALVO_LEN_T;
        }
      conv = *p != '\0' ? *p++ : '\0';

      switch (conv)
        {
        case 'd':
        case 'i':
          {
            long long sv;
            int negative;
            unsigned long long magnitude;
            switch (spec.length)
              {
              case SALVO_LEN_HH:
                sv = (signed char)va_arg (ap, int);
                break;
              case SALVO_LEN_H:
                sv = (short)va_arg (ap, int);
                break;
              case SALVO_LEN_L:
                sv = va_arg (ap, long);
                break;
              case SALVO_LEN_LL:
                sv = va_arg (ap, long long);
                break;
              case SALVO_LEN_Z:
              case SALVO_LEN_T:
                sv = va_arg (ap, long);
                break;
              default:
                sv = va_arg (ap, int);
                break;
              }
            negative = (sv < 0);
            magnitude = negative ? 0ULL - (unsigned long long)sv
                                 : (unsigned long long)sv;
            count += salvo_format_int (emit, ctx, magnitude, negative, 10, 0,
                                       &spec);
            break;
          }
        case 'u':
        case 'x':
        case 'X':
        case 'o':
          {
            unsigned long long uv;
            unsigned base = (conv == 'u') ? 10u : (conv == 'o') ? 8u : 16u;
            switch (spec.length)
              {
              case SALVO_LEN_HH:
                uv = (unsigned char)va_arg (ap, unsigned int);
                break;
              case SALVO_LEN_H:
                uv = (unsigned short)va_arg (ap, unsigned int);
                break;
              case SALVO_LEN_L:
                uv = va_arg (ap, unsigned long);
                break;
              case SALVO_LEN_LL:
                uv = va_arg (ap, unsigned long long);
                break;
              case SALVO_LEN_Z:
              case SALVO_LEN_T:
                uv = va_arg (ap, unsigned long);
                break;
              default:
                uv = va_arg (ap, unsigned int);
                break;
              }
            count += salvo_format_int (emit, ctx, uv, 0, base, conv == 'X',
                                       &spec);
            break;
          }
        case 'p':
          {
            void *ptr = va_arg (ap, void *);
            salvo_spec pspec = spec;
            pspec.alt = 1;
            pspec.plus = 0;
            pspec.space = 0;
            count += salvo_format_int (emit, ctx,
                                       (unsigned long long)(uintptr_t)ptr, 0,
                                       16, 0, &pspec);
            break;
          }
        case 'c':
          {
            char c = (char)va_arg (ap, int);
            int pad = spec.width > 1 ? spec.width - 1 : 0;
            if (!spec.minus)
              salvo_emit_repeat (emit, ctx, ' ', pad);
            emit (ctx, c);
            if (spec.minus)
              salvo_emit_repeat (emit, ctx, ' ', pad);
            count += 1 + pad;
            break;
          }
        case 's':
          {
            const char *s = va_arg (ap, const char *);
            size_t len;
            int pad;
            if (s == NULL)
              s = "(null)";
            len = strlen (s);
            if (spec.precision >= 0 && len > (size_t)spec.precision)
              len = (size_t)spec.precision;
            pad = spec.width > (int)len ? spec.width - (int)len : 0;
            if (!spec.minus)
              salvo_emit_repeat (emit, ctx, ' ', pad);
            salvo_emit_string (emit, ctx, s, len);
            if (spec.minus)
              salvo_emit_repeat (emit, ctx, ' ', pad);
            count += (int)len + pad;
            break;
          }
        case '%':
          emit (ctx, '%');
          ++count;
          break;
        case '\0':
          /* Trailing '%' at end of format: emit it and stop. */
          emit (ctx, '%');
          ++count;
          break;
        default:
          /* Unknown conversion: pass the directive through verbatim. */
          emit (ctx, '%');
          emit (ctx, conv);
          count += 2;
          break;
        }
    }
  return count;
}

int
vsnprintf (char *str, size_t size, const char *fmt, va_list ap)
{
  salvo_buf_ctx ctx;
  int n;

  ctx.buf = str;
  ctx.size = size;
  ctx.len = 0;
  n = salvo_vformat (salvo_buf_emit, &ctx, fmt, ap);
  if (size != 0)
    str[ctx.len < size ? ctx.len : size - 1] = '\0';
  return n;
}

int
vsprintf (char *str, const char *fmt, va_list ap)
{
  return vsnprintf (str, (size_t)-1, fmt, ap);
}

int
vfprintf (FILE *stream, const char *fmt, va_list ap)
{
  salvo_file_ctx ctx;
  int n;

  if (stream == NULL)
    return EOF;
  ctx.stream = stream;
  ctx.err = 0;
  n = salvo_vformat (salvo_file_emit, &ctx, fmt, ap);
  return ctx.err ? EOF : n;
}

int
vprintf (const char *fmt, va_list ap)
{
  return vfprintf (stdout, fmt, ap);
}

int
snprintf (char *str, size_t size, const char *fmt, ...)
{
  va_list ap;
  int n;
  va_start (ap, fmt);
  n = vsnprintf (str, size, fmt, ap);
  va_end (ap);
  return n;
}

int
sprintf (char *str, const char *fmt, ...)
{
  va_list ap;
  int n;
  va_start (ap, fmt);
  n = vsprintf (str, fmt, ap);
  va_end (ap);
  return n;
}

int
fprintf (FILE *stream, const char *fmt, ...)
{
  va_list ap;
  int n;
  va_start (ap, fmt);
  n = vfprintf (stream, fmt, ap);
  va_end (ap);
  return n;
}

int
printf (const char *fmt, ...)
{
  va_list ap;
  int n;
  va_start (ap, fmt);
  n = vfprintf (stdout, fmt, ap);
  va_end (ap);
  return n;
}
