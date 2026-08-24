#define _POSIX_C_SOURCE 200809L

#include "ccw_test.h"
#include "../toolchain/ccwld/ccwld.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/* Minimal ELF64 LE ET_REL input: one .text with a _start symbol, so
 * the real pipeline (load ▸ … ▸ emit) has something to link. */
static int
write_input (const char *path)
{
    static const unsigned char code[] = { 0x55, 0x31, 0xc0, 0x5d, 0xc3 };
    unsigned char buf[512];
    size_t n = 0;

    /* e_ident + fixed fields, ET_REL / EM_X86_64 / 5 sections */
    unsigned char ehdr[64] = { 0 };
    ehdr[0] = 0x7f; ehdr[1] = 'E'; ehdr[2] = 'L'; ehdr[3] = 'F';
    ehdr[4] = 2; ehdr[5] = 1; ehdr[6] = 1;
    ehdr[16] = 1; ehdr[18] = 62;               /* ET_REL, EM_X86_64 */
    ehdr[20] = 1;                              /* EV_CURRENT */
    ehdr[52] = 64;                             /* e_ehsize */
    ehdr[58] = 64;                             /* e_shentsize */
    ehdr[60] = 5;                              /* e_shnum */
    ehdr[62] = 4;                              /* e_shstrndx */
    memcpy (buf + n, ehdr, sizeof (ehdr));
    n += sizeof (ehdr);

    size_t text_off = n;
    memcpy (buf + n, code, sizeof (code));
    n += sizeof (code);
    while (n % 8) buf[n++] = 0;

    /* symtab: null + _start(global, section 1) */
    size_t symtab_off = n;
    memset (buf + n, 0, 24); n += 24;
    buf[n + 0] = 1;                            /* st_name = 1 */
    buf[n + 4] = (1 << 4) | 1;                 /* GLOBAL | STT_OBJECT */
    buf[n + 6] = 1; buf[n + 7] = 0;            /* st_shndx = .text */
    /* value 0, size 5 */
    n += 24;

    /* strtab: "\0_start\0" */
    size_t strtab_off = n;
    memcpy (buf + n, "\0_start\0", 8);
    n += 8;

    /* shstrtab: "\0.text\0.symtab\0.strtab\0.shstrtab\0"
     * offsets: .text=1, .symtab=7, .strtab=15, .shstrtab=23 */
    size_t shstr_off = n;
    memcpy (buf + n, "\0.text\0.symtab\0.strtab\0.shstrtab\0", 33);
    n += 33;
    while (n % 8) buf[n++] = 0;

    size_t shoff = n;
    /* ELF64 shdr fields: name@0 type@4 flags@8 addr@16 offset@24
     * size@32 link@40 info@44 align@48 entsize@56 — 5 sections */
    memset (buf + n, 0, 5 * 64);
    /* [1] .text: PROGBITS, ALLOC|EXEC */
    buf[n + 64 + 0] = 1;
    buf[n + 64 + 4] = 1;
    buf[n + 64 + 8] = 0x6;
    buf[n + 64 + 24] = (unsigned char)text_off;
    buf[n + 64 + 32] = (unsigned char)sizeof (code);
    buf[n + 64 + 48] = 16;
    /* [2] .symtab */
    buf[n + 128 + 0] = 7;
    buf[n + 128 + 4] = 2;
    buf[n + 128 + 24] = (unsigned char)symtab_off;
    buf[n + 128 + 32] = 48;
    buf[n + 128 + 40] = 3;                     /* link -> .strtab */
    buf[n + 128 + 44] = 1;                     /* info: first global */
    buf[n + 128 + 48] = 8;
    buf[n + 128 + 56] = 24;
    /* [3] .strtab */
    buf[n + 192 + 0] = 15;
    buf[n + 192 + 4] = 3;
    buf[n + 192 + 24] = (unsigned char)strtab_off;
    buf[n + 192 + 32] = 8;
    buf[n + 192 + 48] = 1;
    /* [4] .shstrtab */
    buf[n + 256 + 0] = 23;
    buf[n + 256 + 4] = 3;
    buf[n + 256 + 24] = (unsigned char)shstr_off;
    buf[n + 256 + 32] = 33;
    buf[n + 256 + 48] = 1;
    n += 5 * 64;

    /* patch e_shoff */
    for (int i = 0; i < 8; i++)
        buf[40 + i] = (unsigned char)(shoff >> (8 * i));

    FILE *f = fopen (path, "wb");
    if (!f) return 0;
    int ok = fwrite (buf, 1, n, f) == n;
    fclose (f);
    return ok;
}

int main(void)
{
    ccwld_error error = {0};
    ccwld_output output;
    ccwld_plan *plan = ccwld_plan_new("x86_64-linux-gnu");
    char path[] = "/tmp/ccwld-test-emit-XXXXXX";
    char inpath[] = "/tmp/ccwld-test-input-XXXXXX";
    FILE *file;
    int fd;

    CCW_CHECK(plan != NULL, "plan allocation failed");
    if (plan == NULL) return ccw_test_report("ccwld-emit");

    fd = mkstemp(inpath);
    CCW_CHECK(fd >= 0, "could not create input path");
    close(fd);
    CCW_CHECK(write_input(inpath), "could not write input object");

    memset(&output, 0, sizeof(output));
    output.kind = strdup("exe");
    output.format = strdup("elf");
    output.entry = strdup("_start");
    CCW_CHECK(ccwld_plan_output(plan, &output, &error),
              "output declaration failed: %s", error.message);
    free(output.kind);
    free(output.format);
    free(output.entry);
    CCW_CHECK(ccwld_plan_input(plan, inpath, 0, 0, &error),
              "input registration failed: %s", error.message);
    CCW_CHECK(ccwld_plan_seal(plan, &error),
              "plan sealing failed: %s", error.message);

    fd = mkstemp(path);
    CCW_CHECK(fd >= 0, "could not create output path");
    if (fd >= 0) {
        close(fd);
        CCW_CHECK(ccwld_link_run(plan, path, &error),
                  "link emission failed: %s", error.message);
        file = fopen(path, "rb");
        CCW_CHECK(file != NULL, "emitted object could not be opened");
        if (file != NULL) {
            long fsz;
            fseek(file, 0, SEEK_END);
            fsz = ftell(file);
            fseek(file, 0, SEEK_SET);
            char *whole = malloc(fsz > 0 ? (size_t)fsz : 1);
            CCW_CHECK(whole != NULL, "out of memory reading output");
            if (whole != NULL) {
                size_t n = fread(whole, 1, (size_t)fsz, file);
                fclose(file);
                file = NULL;
                CCW_CHECK(n >= 64, "emitted object truncated");
                CCW_CHECK(n >= 4 && whole[0] == 0x7f && whole[1] == 'E' &&
                          whole[2] == 'L' && whole[3] == 'F',
                          "emitted object is not ELF");
                if (n >= 120) {
                    uint64_t p_offset = 0, p_vaddr = 0, p_align = 0;
                    for (int b = 0; b < 8; b++) {
                        p_offset |= (uint64_t)(unsigned char)whole[64 + 8 + b]
                                    << (8 * b);
                        p_vaddr |= (uint64_t)(unsigned char)whole[64 + 16 + b]
                                   << (8 * b);
                        p_align |= (uint64_t)(unsigned char)whole[64 + 48 + b]
                                   << (8 * b);
                    }
                    CCW_CHECK(p_align != 0 && (p_align & (p_align - 1)) == 0,
                              "PT_LOAD alignment is not a power of two");
                    CCW_CHECK((p_offset % p_align) == (p_vaddr % p_align),
                              "PT_LOAD offset/vaddr congruence violated");
                }
                /* the producer note (§8) rides in a real SHT_NOTE
                 * section whose name lands in the section-header
                 * string table near the end of the file */
                {
                    int has_note = 0;
                    for (long i = 0; i + 9 < (long)n; i++)
                        if (memcmp(whole + i, ".note.ccw", 9) == 0)
                            has_note = 1;
                    CCW_CHECK(has_note, "producer note missing");
                }
                free(whole);
            }
        }
        if (file != NULL)
            fclose(file);
        unlink(path);
    }
    unlink(inpath);
    ccwld_plan_free(plan);
    return ccw_test_report("ccwld-emit");
}
