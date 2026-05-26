/* ============================================================================
 * cf_util.c — utility functions
 * ========================================================================= */

#include "cf_util.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

unsigned int cf_strlcpy(char *dst, const char *src, unsigned int dstsz)
{
    unsigned int n;

    if (dstsz == 0U) return 0U;
    n = (unsigned int)strlen(src);
    if (n >= dstsz) n = dstsz - 1U;
    memcpy(dst, src, n);
    dst[n] = '\0';
    return n;
}

int cf_unquote(char *dst, unsigned int dstsz,
               const char *src, unsigned int src_len)
{
    unsigned int ri = 0U;
    unsigned int wi = 0U;

    if (src_len < 2U) return -1;
    ri = 1U; /* skip opening '"' */

    while (ri < src_len - 1U) { /* -1 to skip closing '"' */
        char c = src[ri];

        if (c == '\\' && ri + 1U < src_len - 1U) {
            char next = src[ri + 1U];
            /* backslash-newline: line continuation — skip both chars */
            if (next == '\n' || next == '\r') {
                ri += 2U;
                /* also skip any leading whitespace on the next line */
                while (ri < src_len - 1U &&
                       (src[ri] == ' ' || src[ri] == '\t')) {
                    ri++;
                }
                continue;
            }
            /* standard escape sequences */
            if (wi + 1U >= dstsz) return -1;
            switch (next) {
            case 'n':  dst[wi++] = '\n'; break;
            case 't':  dst[wi++] = '\t'; break;
            case 'r':  dst[wi++] = '\r'; break;
            case '"':  dst[wi++] = '"';  break;
            case '\\': dst[wi++] = '\\'; break;
            default:   dst[wi++] = next; break;
            }
            ri += 2U;
            continue;
        }
        if (wi + 1U >= dstsz) return -1;
        dst[wi++] = c;
        ri++;
    }

    dst[wi] = '\0';
    return 0;
}

void cf_ident_to_c(char *dst, const char *src, unsigned int dstsz)
{
    unsigned int i;
    unsigned int n;

    if (dstsz == 0U) return;
    n = (unsigned int)strlen(src);
    if (n >= dstsz) n = dstsz - 1U;
    for (i = 0U; i < n; i++) {
        dst[i] = (src[i] == '-') ? '_' : src[i];
    }
    dst[n] = '\0';
}

char *cf_read_file(const char *path, unsigned int *out_len)
{
    FILE *fp;
    long  fsize;
    char *buf;
    size_t nread;

    fp = fopen(path, "rb");
    if (fp == NULL) return NULL;

    if (fseek(fp, 0L, SEEK_END) != 0) { fclose(fp); return NULL; }
    fsize = ftell(fp);
    if (fsize < 0L)                   { fclose(fp); return NULL; }
    rewind(fp);

    buf = (char *)malloc((size_t)(fsize + 1L));
    if (buf == NULL)                  { fclose(fp); return NULL; }

    nread = fread(buf, 1U, (size_t)fsize, fp);
    fclose(fp);

    if ((long)nread != fsize) { free(buf); return NULL; }
    buf[fsize] = '\0';
    *out_len   = (unsigned int)fsize;
    return buf;
}

int cf_str_eq_tok(const char *s, const char *tok, unsigned int tok_len)
{
    unsigned int slen = (unsigned int)strlen(s);
    if (slen != tok_len) return 0;
    return (memcmp(s, tok, tok_len) == 0) ? 1 : 0;
}
