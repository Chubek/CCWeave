/* salvo-libc <stdio.h> — unbuffered v0.1 streams over file descriptors.
 *
 * Streams are thin wrappers around read(2)/write(2); there is no user-space
 * buffering yet, so fflush is a successful no-op. Formatting supports the
 * integer/string/pointer conversions; floating-point conversions (%f/%e/%g)
 * are staged until salvo-libm exists. */

#ifndef SALVO_STDIO_H
#define SALVO_STDIO_H

#include <stdarg.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C"
{
#endif

#define EOF (-1)

#define SEEK_SET 0
#define SEEK_CUR 1
#define SEEK_END 2

  typedef struct __salvo_FILE FILE;

  extern FILE *stdin;
  extern FILE *stdout;
  extern FILE *stderr;

  FILE *fopen (const char *path, const char *mode);
  int fclose (FILE *stream);
  int fflush (FILE *stream);

  size_t fread (void *ptr, size_t size, size_t nmemb, FILE *stream);
  size_t fwrite (const void *ptr, size_t size, size_t nmemb, FILE *stream);

  int fseek (FILE *stream, long offset, int whence);
  long ftell (FILE *stream);
  int feof (FILE *stream);
  int ferror (FILE *stream);

  int fgetc (FILE *stream);
  int fputc (int c, FILE *stream);
  char *fgets (char *s, int size, FILE *stream);
  int fputs (const char *s, FILE *stream);
  int puts (const char *s);
  int putchar (int c);

  int printf (const char *fmt, ...);
  int fprintf (FILE *stream, const char *fmt, ...);
  int sprintf (char *str, const char *fmt, ...);
  int snprintf (char *str, size_t size, const char *fmt, ...);
  int vprintf (const char *fmt, va_list ap);
  int vfprintf (FILE *stream, const char *fmt, va_list ap);
  int vsprintf (char *str, const char *fmt, va_list ap);
  int vsnprintf (char *str, size_t size, const char *fmt, va_list ap);

#ifdef __cplusplus
}
#endif

#endif /* SALVO_STDIO_H */
