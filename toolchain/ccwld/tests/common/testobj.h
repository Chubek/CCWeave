/* Shared helpers for the ccwld test suites (§10): temp files, a
 * minimal ELF64 ET_REL writer for link inputs, and byte-compare for
 * the determinism gate. */
#ifndef CCWLD_TESTOBJ_H
#define CCWLD_TESTOBJ_H

#include <stddef.h>
#include <stdint.h>

/* section descriptor for the test-object writer */
typedef struct
{
  const char *name;
  uint32_t type;  /* 1 PROGBITS, 8 NOBITS, … */
  uint64_t flags; /* SHF_* */
  uint64_t align;
  const unsigned char *data;
  size_t size;
} ccwld_test_sec;

/* symbol descriptor (defined in section `shndx`, 1-based over the
 * sections array; 0xfff1 = SHN_ABS) */
typedef struct
{
  const char *name;
  int bind; /* 1 global, 2 weak */
  uint16_t shndx;
  uint64_t value;
  uint64_t size;
} ccwld_test_sym;

/* scratch path "<tmp>/ccwldtest-<pid>-<n>-<leaf>"; caller frees. */
char *ccwld_test_tmp (const char *leaf);

/* atomic file write; returns 1 on success. */
int ccwld_test_write_file (const char *path, const void *data, size_t n);

/* write a minimal ELF64 LE ET_REL object; returns 1 on success. */
int ccwld_test_write_rel (const char *path, const ccwld_test_sec *secs,
                          size_t nsecs, const ccwld_test_sym *syms,
                          size_t nsyms);

size_t ccwld_test_file_size (const char *path);

/* byte-for-byte comparison (determinism gate). */
int ccwld_test_files_equal (const char *a, const char *b);

/* simple assertion ledger: CHECK(cond) records pass/fail, the main()
 * returns nonzero when any check failed. */
int ccwld_test_failures (void);
#define CHECK(cond)                                                           \
  do                                                                          \
    {                                                                         \
      if (!(cond))                                                            \
        {                                                                     \
          ccwld_test_fail_inc (__FILE__, __LINE__, #cond);                    \
        }                                                                     \
    }                                                                         \
  while (0)
void ccwld_test_fail_inc (const char *file, int line, const char *what);

#endif
