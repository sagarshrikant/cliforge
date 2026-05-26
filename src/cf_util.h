/* ============================================================================
 * cf_util.h — small string and I/O utilities
 * ========================================================================= */

#ifndef CF_UTIL_H
#define CF_UTIL_H

#include <stddef.h>

/**
 * cf_strlcpy - NUL-safe strncpy. Always NUL-terminates @dst.
 * Returns number of bytes written (not counting NUL).
 */
unsigned int cf_strlcpy(char *dst, const char *src, unsigned int dstsz);

/**
 * cf_unquote - Copy string literal text @src (including surrounding quotes)
 * into @dst, stripping quotes and resolving backslash escapes and
 * backslash-newline continuations.  @src_len is the raw token length.
 * Returns 0 on success, -1 if @dst is too small.
 */
int cf_unquote(char *dst, unsigned int dstsz,
               const char *src, unsigned int src_len);

/**
 * cf_ident_to_c - Convert a cliforge identifier (may contain '-') to a
 * valid C identifier by replacing '-' with '_'. Result is written into @dst.
 */
void cf_ident_to_c(char *dst, const char *src, unsigned int dstsz);

/**
 * cf_read_file - Read entire file at @path into a heap buffer.
 * Caller must free() the returned pointer.  Sets *out_len on success.
 * Returns NULL on error.
 */
char *cf_read_file(const char *path, unsigned int *out_len);

/**
 * cf_str_eq_tok - Compare NUL-terminated string @s with token text
 * (not NUL-terminated, length @len).
 */
int cf_str_eq_tok(const char *s, const char *tok, unsigned int tok_len);

#endif /* CF_UTIL_H */
