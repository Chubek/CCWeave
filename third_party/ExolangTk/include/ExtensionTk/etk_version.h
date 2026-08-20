/**
 * @file etk_version.h
 * @brief Semantic-version parsing, comparison, formatting, and constraints.
 * @stability stable
 * @depends ExtensionTk::platform, ExtensionTk::types, InteropTk::cstring
 */
#ifndef ETK_VERSION_H
#define ETK_VERSION_H
#include "etk_types.h"
typedef struct etk_version { unsigned major, minor, patch;
                              const char *prerelease; size_t prerelease_len; } etk_version;
ETK_DEF etk_status etk_version_parse(const char *text, etk_version *out);
ETK_DEF int etk_version_compare(const etk_version *a, const etk_version *b);
ETK_DEF etk_status etk_version_fmt(const etk_version *v, char *buf, size_t cap);
ETK_DEF etk_bool etk_version_satisfies(const etk_version *v, const char *constraint);
#ifdef ETK_VERSION_IMPLEMENTATION
#include <stdio.h>
#include <string.h>
ETK_DEF etk_status etk_version_parse(const char *text, etk_version *out)
{
    unsigned a = 0, b = 0, c = 0;
    int n;
    const char *dash;
    if (text == NULL || out == NULL) return ETK_EINVAL;
    n = sscanf(text, "%u.%u.%u", &a, &b, &c);
    if (n < 1) return ETK_EVERSION;
    out->major = a;
    out->minor = n > 1 ? b : 0;
    out->patch = n > 2 ? c : 0;
    dash = strchr(text, '-');
    out->prerelease = dash ? dash + 1 : NULL;
    out->prerelease_len = dash ? strlen(dash + 1) : 0;
    return ETK_OK;
}
ETK_DEF int etk_version_compare(const etk_version *a,const etk_version *b)
{
    if (a == NULL || b == NULL) return 0;
    if (a->major != b->major) return a->major > b->major ? 1 : -1;
    if (a->minor != b->minor) return a->minor > b->minor ? 1 : -1;
    if (a->patch != b->patch) return a->patch > b->patch ? 1 : -1;
    return 0;
}
ETK_DEF etk_status etk_version_fmt(const etk_version *v,char *buf,size_t cap)
{
    int n;
    if (v == NULL || buf == NULL || cap == 0) return ETK_EINVAL;
    n = snprintf(buf, cap, "%u.%u.%u", v->major, v->minor, v->patch);
    return n < 0 || (size_t)n >= cap ? ETK_EFAIL : ETK_OK;
}
ETK_DEF etk_bool etk_version_satisfies(const etk_version *v,const char *constraint)
{
    etk_version want;
    if (v == NULL || constraint == NULL ||
        etk_version_parse(constraint, &want) != ETK_OK) return ETK_FALSE;
    return etk_version_compare(v, &want) >= 0;
}
#endif
#endif
