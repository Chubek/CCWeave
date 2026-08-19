/* salvo-libc <stdio.h>: stream objects over file descriptors.
 *
 * v0.1 is unbuffered: every fgetc/fwrite reaches read(2)/write(2)
 * directly. The FILE layout is private so buffering can arrive without
 * an ABI break. */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <fcntl.h>

struct __salvo_FILE {
    int fd;
    int eof;
    int err;
};

static FILE salvo_stdin_obj  = { 0, 0, 0 };
static FILE salvo_stdout_obj = { 1, 0, 0 };
static FILE salvo_stderr_obj = { 2, 0, 0 };

FILE *stdin  = &salvo_stdin_obj;
FILE *stdout = &salvo_stdout_obj;
FILE *stderr = &salvo_stderr_obj;

FILE *fopen(const char *path, const char *mode)
{
    int flags;
    int plus;
    int fd;
    FILE *stream;

    if (path == NULL || mode == NULL || mode[0] == '\0') {
        errno = EINVAL;
        return NULL;
    }
    plus = (strchr(mode, '+') != NULL);
    switch (mode[0]) {
    case 'r': flags = plus ? O_RDWR : O_RDONLY; break;
    case 'w': flags = (plus ? O_RDWR : O_WRONLY) | O_CREAT | O_TRUNC; break;
    case 'a': flags = (plus ? O_RDWR : O_WRONLY) | O_CREAT | O_APPEND; break;
    default:
        errno = EINVAL;
        return NULL;
    }
    fd = open(path, flags, 0666);
    if (fd < 0)
        return NULL;
    stream = (FILE *)malloc(sizeof(*stream));
    if (stream == NULL) {
        close(fd);
        errno = ENOMEM;
        return NULL;
    }
    stream->fd = fd;
    stream->eof = 0;
    stream->err = 0;
    return stream;
}

int fclose(FILE *stream)
{
    int rc;

    if (stream == NULL)
        return EOF;
    rc = close(stream->fd);
    free(stream);
    return rc == 0 ? 0 : EOF;
}

int fflush(FILE *stream)
{
    /* Unbuffered: nothing to drain. NULL flushes "all streams", which is
     * likewise a no-op here. */
    (void)stream;
    return 0;
}

size_t fread(void *ptr, size_t size, size_t nmemb, FILE *stream)
{
    size_t total;
    size_t done = 0;
    unsigned char *out = (unsigned char *)ptr;

    if (size == 0 || nmemb == 0)
        return 0;
    if (ptr == NULL || stream == NULL)
        return 0;
    if (nmemb > (size_t)-1 / size) {
        stream->err = 1;
        return 0;
    }
    total = size * nmemb;
    while (done < total) {
        ssize_t n = read(stream->fd, out + done, total - done);
        if (n < 0) {
            if (errno == EINTR)
                continue;
            stream->err = 1;
            break;
        }
        if (n == 0) {
            stream->eof = 1;
            break;
        }
        done += (size_t)n;
    }
    return done / size;
}

size_t fwrite(const void *ptr, size_t size, size_t nmemb, FILE *stream)
{
    size_t total;
    size_t done = 0;
    const unsigned char *in = (const unsigned char *)ptr;

    if (size == 0 || nmemb == 0)
        return 0;
    if (ptr == NULL || stream == NULL)
        return 0;
    if (nmemb > (size_t)-1 / size) {
        stream->err = 1;
        return 0;
    }
    total = size * nmemb;
    while (done < total) {
        ssize_t n = write(stream->fd, in + done, total - done);
        if (n < 0) {
            if (errno == EINTR)
                continue;
            stream->err = 1;
            break;
        }
        done += (size_t)n;
    }
    return done / size;
}

int fseek(FILE *stream, long offset, int whence)
{
    if (stream == NULL)
        return -1;
    if (lseek(stream->fd, (off_t)offset, whence) == (off_t)-1) {
        stream->err = 1;
        return -1;
    }
    stream->eof = 0;
    return 0;
}

long ftell(FILE *stream)
{
    off_t pos;

    if (stream == NULL)
        return -1;
    pos = lseek(stream->fd, 0, SEEK_CUR);
    if (pos == (off_t)-1) {
        stream->err = 1;
        return -1;
    }
    return (long)pos;
}

int feof(FILE *stream)
{
    return stream != NULL && stream->eof;
}

int ferror(FILE *stream)
{
    return stream != NULL && stream->err;
}

int fgetc(FILE *stream)
{
    unsigned char c;

    if (stream == NULL)
        return EOF;
    for (;;) {
        ssize_t n = read(stream->fd, &c, 1);
        if (n == 1)
            return (int)c;
        if (n == 0) {
            stream->eof = 1;
            return EOF;
        }
        if (errno == EINTR)
            continue;
        stream->err = 1;
        return EOF;
    }
}

int fputc(int c, FILE *stream)
{
    unsigned char byte = (unsigned char)c;

    if (stream == NULL)
        return EOF;
    for (;;) {
        ssize_t n = write(stream->fd, &byte, 1);
        if (n == 1)
            return (int)byte;
        if (errno == EINTR)
            continue;
        stream->err = 1;
        return EOF;
    }
}

char *fgets(char *s, int size, FILE *stream)
{
    int i = 0;

    if (s == NULL || stream == NULL || size <= 0)
        return NULL;
    while (i + 1 < size) {
        int c = fgetc(stream);
        if (c == EOF)
            break;
        s[i++] = (char)c;
        if (c == '\n')
            break;
    }
    if (i == 0)
        return NULL;
    s[i] = '\0';
    return s;
}

int fputs(const char *s, FILE *stream)
{
    size_t len;

    if (s == NULL || stream == NULL)
        return EOF;
    len = strlen(s);
    if (fwrite(s, 1, len, stream) != len)
        return EOF;
    return 0;
}

int puts(const char *s)
{
    if (s == NULL)
        return EOF;
    if (fputs(s, stdout) == EOF)
        return EOF;
    if (fputc('\n', stdout) == EOF)
        return EOF;
    return 0;
}

int putchar(int c)
{
    return fputc(c, stdout);
}
