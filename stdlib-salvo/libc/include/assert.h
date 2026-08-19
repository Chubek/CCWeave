/* salvo-libc <assert.h>
 *
 * Deliberately has no include guard: the C standard permits repeated
 * inclusion with a changed NDEBUG state, and each inclusion re-defines
 * assert accordingly. */

#ifdef __cplusplus
extern "C"
{
#endif

  void __assert_fail (const char *expr, const char *file, int line,
                      const char *func);

#ifdef __cplusplus
}
#endif

#undef assert
#ifdef NDEBUG
#define assert(expr) ((void)0)
#else
#define assert(expr)                                                          \
  ((expr) ? (void)0 : __assert_fail (#expr, __FILE__, __LINE__, __func__))
#endif
