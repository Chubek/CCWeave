/* ccw-manifest — generates manifests/Kernel.yaml and
 * manifests/Capabilities.yaml from the live kernels (§4.2).
 *
 * Kernel source is the single source of truth: this tool loads each
 * kernel through the executor and calls kernel-capabilities. --check
 * regenerates in memory and diffs against the on-disk files. */

#include "GlueSTD.h"
#include "ccw_host_accessors.h"

#include <dirent.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CCW_MANIFEST_GENERATOR "ccw-manifest/0.1"

/* ---------- string builder ---------- */

typedef struct { char *buf; size_t len, cap; } sb;

static void sb_add(sb *s, const char *fmt, ...)
{
    char tmp[1024];
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(tmp, sizeof(tmp), fmt, ap);
    va_end(ap);
    if (n < 0) return;
    if (s->len + (size_t)n + 1u > s->cap) {
        size_t cap = s->cap ? s->cap : 1024;
        while (cap < s->len + (size_t)n + 1u) cap *= 2;
        char *b = (char *)realloc(s->buf, cap);
        if (b == NULL) return;
        s->buf = b;
        s->cap = cap;
    }
    memcpy(s->buf + s->len, tmp, (size_t)n + 1u);
    s->len += (size_t)n;
}

/* ---------- capability id grammar: [a-z0-9-]+(\.[a-z0-9-]+)+ ---------- */

static bool capability_valid(const char *cap)
{
    if (cap == NULL || *cap == '\0') return false;
    int segments = 0, seglen = 0;
    for (const char *p = cap; ; p++) {
        if (*p == '\0' || *p == '.') {
            if (seglen == 0) return false;
            segments++;
            seglen = 0;
            if (*p == '\0') break;
            continue;
        }
        bool ok = (*p >= 'a' && *p <= 'z') || (*p >= '0' && *p <= '9') || *p == '-';
        if (!ok) return false;
        seglen++;
    }
    return segments >= 2;
}

/* ---------- kernel discovery ---------- */

typedef struct {
    char  *path;         /* repo-relative, e.g. kernels/strength-reduce.scm */
    char  *library;
    char  *name;
    char  *version;
    char  *description;
    char **caps;
    int    cap_count;
} kernel_entry;

static int compare_strings(const void *a, const void *b)
{
    return strcmp(*(const char *const *)a, *(const char *const *)b);
}

static int compare_entries(const void *a, const void *b)
{
    return strcmp(((const kernel_entry *)a)->path, ((const kernel_entry *)b)->path);
}

static char *dup_str(const char *s)
{
    if (s == NULL) return NULL;
    size_t n = strlen(s) + 1u;
    char *p = (char *)malloc(n);
    if (p != NULL) memcpy(p, s, n);
    return p;
}

/* The library name is recovered from the kernel's define-library form. */
static char *read_library_name(const char *path)
{
    FILE *fp = fopen(path, "rb");
    if (fp == NULL) return NULL;
    char line[1024];
    char *found = NULL;
    while (found == NULL && fgets(line, sizeof(line), fp) != NULL) {
        const char *at = strstr(line, "define-library");
        if (at == NULL) continue;
        const char *open = strchr(at, '(');
        const char *close = open ? strchr(open, ')') : NULL;
        if (open == NULL || close == NULL) continue;
        size_t n = (size_t)(close - open) + 1u;
        found = (char *)malloc(n + 1u);
        if (found == NULL) break;
        memcpy(found, open, n);
        found[n] = '\0';
    }
    fclose(fp);
    return found;
}

static bool ends_with_scm(const char *name)
{
    size_t n = strlen(name);
    return n > 4 && strcmp(name + n - 4, ".scm") == 0;
}

static int collect_kernels(const char *root, const char *kernel_dir,
                           kernel_entry **out, int *out_count)
{
    char dir_path[1024];
    snprintf(dir_path, sizeof(dir_path), "%s/%s", root, kernel_dir);
    DIR *d = opendir(dir_path);
    if (d == NULL) {
        fprintf(stderr, "ccw-manifest: cannot open %s\n", dir_path);
        return -1;
    }
    kernel_entry *entries = NULL;
    int count = 0, cap = 0;
    struct dirent *de;
    while ((de = readdir(d)) != NULL) {
        if (!ends_with_scm(de->d_name)) continue;
        if (count == cap) {
            cap = cap ? cap * 2 : 8;
            kernel_entry *e = (kernel_entry *)realloc(entries, (size_t)cap * sizeof(*e));
            if (e == NULL) { closedir(d); return -1; }
            entries = e;
        }
        memset(&entries[count], 0, sizeof(entries[count]));
        char rel[1024];
        snprintf(rel, sizeof(rel), "%s/%s", kernel_dir, de->d_name);
        entries[count].path = dup_str(rel);
        count++;
    }
    closedir(d);
    qsort(entries, (size_t)count, sizeof(*entries), compare_entries);
    *out = entries;
    *out_count = count;
    return 0;
}

static void free_entries(kernel_entry *entries, int count)
{
    for (int i = 0; i < count; i++) {
        free(entries[i].path);
        free(entries[i].library);
        free(entries[i].name);
        free(entries[i].version);
        free(entries[i].description);
        for (int j = 0; j < entries[i].cap_count; j++) free(entries[i].caps[j]);
        free(entries[i].caps);
    }
    free(entries);
}

/* ---------- YAML emission ---------- */

static void emit_header(sb *s, const char *derivation)
{
    sb_add(s, "# GENERATED by ccw-manifest — DO NOT EDIT.\n");
    sb_add(s, "# %s\n", derivation);
    sb_add(s, "# Regenerate: ccw-manifest ; verify: ccw-manifest --check\n");
    sb_add(s, "generator: %s\n", CCW_MANIFEST_GENERATOR);
    sb_add(s, "glue_abi: %d\n", CCW_GLUE_ABI_VERSION);
}

static char *render_kernel_yaml(const kernel_entry *entries, int count)
{
    sb s = { NULL, 0, 0 };
    emit_header(&s, "Source of truth: kernel-capabilities of each listed kernel.");
    sb_add(&s, "kernels:\n");
    for (int i = 0; i < count; i++) {
        sb_add(&s, "  - path: %s\n", entries[i].path);
        sb_add(&s, "    library: %s\n", entries[i].library ? entries[i].library : "()");
        sb_add(&s, "    name: %s\n", entries[i].name ? entries[i].name : "");
        sb_add(&s, "    version: %s\n", entries[i].version ? entries[i].version : "");
        sb_add(&s, "    description: %s\n",
               entries[i].description ? entries[i].description : "");
        if (entries[i].cap_count == 0) {
            sb_add(&s, "    capabilities: []\n");
        } else {
            sb_add(&s, "    capabilities:\n");
            for (int j = 0; j < entries[i].cap_count; j++)
                sb_add(&s, "      - %s\n", entries[i].caps[j]);
        }
    }
    return s.buf ? s.buf : dup_str("");
}

/* Inverted index: capability -> providing kernels, both sorted. */
static char *render_capabilities_yaml(const kernel_entry *entries, int count)
{
    int total = 0;
    for (int i = 0; i < count; i++) total += entries[i].cap_count;

    char **caps = (char **)calloc((size_t)(total > 0 ? total : 1), sizeof(char *));
    int ncaps = 0;
    for (int i = 0; i < count; i++) {
        for (int j = 0; j < entries[i].cap_count; j++) {
            bool seen = false;
            for (int k = 0; k < ncaps; k++)
                if (strcmp(caps[k], entries[i].caps[j]) == 0) { seen = true; break; }
            if (!seen) caps[ncaps++] = entries[i].caps[j];
        }
    }
    qsort(caps, (size_t)ncaps, sizeof(*caps), compare_strings);

    sb s = { NULL, 0, 0 };
    emit_header(&s, "Inverted index derived from Kernel.yaml.");
    sb_add(&s, "capabilities:\n");
    for (int i = 0; i < ncaps; i++) {
        sb_add(&s, "  %s:\n", caps[i]);
        for (int k = 0; k < count; k++)
            for (int j = 0; j < entries[k].cap_count; j++)
                if (strcmp(entries[k].caps[j], caps[i]) == 0)
                    sb_add(&s, "    - %s\n", entries[k].path);
    }
    free(caps);
    return s.buf ? s.buf : dup_str("");
}

/* ---------- file helpers ---------- */

static char *read_file(const char *path)
{
    FILE *fp = fopen(path, "rb");
    if (fp == NULL) return NULL;
    fseek(fp, 0, SEEK_END);
    long size = ftell(fp);
    rewind(fp);
    if (size < 0) { fclose(fp); return NULL; }
    char *buf = (char *)malloc((size_t)size + 1u);
    if (buf == NULL) { fclose(fp); return NULL; }
    size_t got = fread(buf, 1, (size_t)size, fp);
    buf[got] = '\0';
    fclose(fp);
    return buf;
}

static bool write_file(const char *path, const char *text)
{
    FILE *fp = fopen(path, "wb");
    if (fp == NULL) return false;
    size_t n = strlen(text);
    bool ok = fwrite(text, 1, n, fp) == n;
    fclose(fp);
    return ok;
}

/* ---------- main ---------- */

static int populate(ccw_executor *ex, kernel_entry *e, const char *root)
{
    char full[2048];
    snprintf(full, sizeof(full), "%s/%s", root, e->path);

    char *err = NULL;
    int id = ccw_kernel_load(ex, full, &err);
    if (id < 0) {
        fprintf(stderr, "ccw-manifest: %s: %s\n", e->path,
                err ? err : "failed to load");
        free(err);
        return -1;
    }
    free(err);

    e->library = read_library_name(full);
    ccw_kernel_info(ex, id, &e->name, &e->version, &e->description);

    int n = ccw_kernel_capability_count(ex, id);
    if (n < 0) n = 0;
    e->caps = (char **)calloc((size_t)(n > 0 ? n : 1), sizeof(char *));
    for (int i = 0; i < n; i++) {
        const char *cap = ccw_kernel_capability(ex, id, i);
        if (cap == NULL) continue;
        if (!capability_valid(cap)) {
            fprintf(stderr,
                    "ccw-manifest: %s: capability \"%s\" does not match "
                    "[a-z0-9-]+(\\.[a-z0-9-]+)+\n", e->path, cap);
            return -1;
        }
        e->caps[e->cap_count++] = dup_str(cap);
    }
    qsort(e->caps, (size_t)e->cap_count, sizeof(*e->caps), compare_strings);
    return 0;
}

static int report_diff(const char *path, const char *want, const char *have)
{
    if (have != NULL && strcmp(want, have) == 0) return 0;
    fprintf(stderr, "ccw-manifest: %s is out of date%s\n", path,
            have == NULL ? " (missing)" : "");
    return 1;
}

int main(int argc, char **argv)
{
    bool check = false;
    const char *root = ".";
    const char *kernel_dir = "kernels";
    const char *manifest_dir = "manifests";

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--check") == 0) check = true;
        else if (strcmp(argv[i], "--root") == 0 && i + 1 < argc) root = argv[++i];
        else if (strcmp(argv[i], "--kernels") == 0 && i + 1 < argc) kernel_dir = argv[++i];
        else if (strcmp(argv[i], "--manifests") == 0 && i + 1 < argc) manifest_dir = argv[++i];
        else {
            fprintf(stderr, "usage: ccw-manifest [--check] [--root DIR] "
                            "[--kernels DIR] [--manifests DIR]\n");
            return 2;
        }
    }

    kernel_entry *entries = NULL;
    int count = 0;
    if (collect_kernels(root, kernel_dir, &entries, &count) != 0) return 2;

    ccw_executor *ex = ccw_executor_create();
    if (ex == NULL || ccw_executor_abi_version(ex) != CCW_GLUE_ABI_VERSION) {
        fprintf(stderr, "ccw-manifest: executor ABI mismatch\n");
        free_entries(entries, count);
        return 2;
    }
    if (ccw_host_register_core_accessors(ex) != CCW_OK) {
        fprintf(stderr, "ccw-manifest: could not register core accessors\n");
        ccw_executor_destroy(ex);
        free_entries(entries, count);
        return 2;
    }

    int rc = 0;
    for (int i = 0; i < count && rc == 0; i++)
        if (populate(ex, &entries[i], root) != 0) rc = 2;
    ccw_executor_destroy(ex);
    if (rc != 0) { free_entries(entries, count); return rc; }

    char *kernel_yaml = render_kernel_yaml(entries, count);
    char *caps_yaml = render_capabilities_yaml(entries, count);

    char kpath[2048], cpath[2048];
    snprintf(kpath, sizeof(kpath), "%s/%s/Kernel.yaml", root, manifest_dir);
    snprintf(cpath, sizeof(cpath), "%s/%s/Capabilities.yaml", root, manifest_dir);

    if (check) {
        char *k_on_disk = read_file(kpath);
        char *c_on_disk = read_file(cpath);
        rc = report_diff(kpath, kernel_yaml, k_on_disk);
        rc |= report_diff(cpath, caps_yaml, c_on_disk);
        free(k_on_disk);
        free(c_on_disk);
        if (rc == 0) printf("ccw-manifest: manifests are up to date\n");
    } else {
        if (!write_file(kpath, kernel_yaml) || !write_file(cpath, caps_yaml)) {
            fprintf(stderr, "ccw-manifest: could not write manifests\n");
            rc = 2;
        } else {
            printf("ccw-manifest: wrote %s and %s (%d kernels)\n", kpath, cpath, count);
        }
    }

    free(kernel_yaml);
    free(caps_yaml);
    free_entries(entries, count);
    return rc;
}
