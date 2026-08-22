#include "libc.h"
#include <stdint.h>

static unsigned long rand_state = 1;

void srand(unsigned int seed) { rand_state = seed; }

int rand(void) {
    rand_state = rand_state * 1103515245 + 12345;
    return (int)((rand_state / 65536) % 32768);
}

unsigned long strtoul(const char *nptr, char **endptr, int base) {
    const char *s = nptr;
    unsigned long acc = 0;
    int c;
    unsigned long cutoff;
    int cutlim;
    int any = 0;

    while (isspace((unsigned char)*s)) s++;

    int neg = 0;
    if (*s == '-') { neg = 1; s++; }
    else if (*s == '+') s++;

    if ((base == 0 || base == 16) && s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) {
        s += 2; base = 16;
    }
    if (base == 0) base = (s[0] == '0') ? 8 : 10;

    cutoff = (unsigned long)-1 / (unsigned long)base;
    cutlim = (unsigned long)-1 % (unsigned long)base;

    for ( ; ; s++) {
        c = *s;
        if (isdigit(c)) c -= '0';
        else if (isalpha(c)) c -= isupper(c) ? 'A' - 10 : 'a' - 10;
        else break;
        if (c >= base) break;
        if (any < 0 || acc > cutoff || (acc == cutoff && c > cutlim)) {
            any = -1;
        } else {
            any = 1;
            acc = acc * base + c;
        }
    }
    if (any < 0) { acc = (unsigned long)-1; errno = 34; }
    else if (neg) acc = -acc;
    if (endptr) *endptr = (char *)(any ? s : nptr);
    return acc;
}

long strtol(const char *nptr, char **endptr, int base) {
    const char *s = nptr;
    unsigned long acc;
    int c;
    unsigned long cutoff;
    int cutlim;
    int any = 0;
    int neg = 0;

    while (isspace((unsigned char)*s)) s++;
    if (*s == '-') { neg = 1; s++; }
    else if (*s == '+') s++;

    if ((base == 0 || base == 16) && s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) {
        s += 2; base = 16;
    }
    if (base == 0) base = (s[0] == '0') ? 8 : 10;

    cutoff = neg ? -(unsigned long)LONG_MIN : LONG_MAX;
    cutlim = cutoff % (unsigned long)base;
    cutoff /= (unsigned long)base;

    for ( ; ; s++) {
        c = *s;
        if (isdigit(c)) c -= '0';
        else if (isalpha(c)) c -= isupper(c) ? 'A' - 10 : 'a' - 10;
        else break;
        if (c >= base) break;
        if (any < 0 || acc > cutoff || (acc == cutoff && c > cutlim)) {
            any = -1;
        } else {
            any = 1;
            acc = acc * base + c;
        }
    }
    if (any < 0) { acc = neg ? LONG_MIN : LONG_MAX; errno = 34; }
    else if (neg) acc = -acc;
    if (endptr) *endptr = (char *)(any ? s : nptr);
    return acc;
}

long long strtoll(const char *nptr, char **endptr, int base) { return (long long)strtol(nptr, endptr, base); }
unsigned long long strtoull(const char *nptr, char **endptr, int base) { return strtoul(nptr, endptr, base); }

int atoi(const char *nptr) { return (int)strtol(nptr, NULL, 10); }
long atol(const char *nptr) { return strtol(nptr, NULL, 10); }
double atof(const char *nptr) { return strtod(nptr, NULL); }

double strtod(const char *nptr, char **endptr) {
    const char *s = nptr;
    double acc = 0.0;
    int neg = 0;

    while (isspace((unsigned char)*s)) s++;
    if (*s == '-') { neg = 1; s++; }
    else if (*s == '+') s++;

    while (isdigit((unsigned char)*s)) {
        acc = acc * 10.0 + (*s - '0');
        s++;
    }

    if (*s == '.') {
        s++;
        double factor = 0.1;
        while (isdigit((unsigned char)*s)) {
            acc += (*s - '0') * factor;
            factor *= 0.1;
            s++;
        }
    }

    if (*s == 'e' || *s == 'E') {
        s++;
        int exp_neg = 0;
        if (*s == '-') { exp_neg = 1; s++; }
        else if (*s == '+') s++;
        int exp = 0;
        while (isdigit((unsigned char)*s)) {
            exp = exp * 10 + (*s - '0');
            s++;
        }
        double factor = 1.0;
        for (int i = 0; i < exp; i++) factor *= 10.0;
        if (exp_neg) acc /= factor;
        else acc *= factor;
    }

    if (neg) acc = -acc;
    if (endptr) *endptr = (char *)s;
    return acc;
}

void *bsearch(const void *key, const void *base, size_t nmemb, size_t size, int (*compar)(const void *, const void *)) {
    size_t l = 0, u = nmemb;
    while (l < u) {
        size_t idx = (l + u) / 2;
        const void *p = (const char *)base + idx * size;
        int c = compar(key, p);
        if (c < 0) u = idx;
        else if (c > 0) l = idx + 1;
        else return (void *)p;
    }
    return NULL;
}

static void swap(char *a, char *b, size_t size) {
    char tmp;
    for (size_t i = 0; i < size; i++) {
        tmp = a[i]; a[i] = b[i]; b[i] = tmp;
    }
}

void qsort(void *base, size_t nmemb, size_t size, int (*compar)(const void *, const void *)) {
    if (nmemb < 2) return;
    char *b = (char *)base;
    size_t i, j;
    char *pivot = b + (nmemb / 2) * size;
    for (i = 0, j = nmemb - 1; ; i++, j--) {
        while (compar(b + i * size, pivot) < 0) i++;
        while (compar(b + j * size, pivot) > 0) j--;
        if (i >= j) break;
        swap(b + i * size, b + j * size, size);
    }
    qsort(b, i, size, compar);
    qsort(b + i * size, nmemb - i, size, compar);
}

void abort(void) { _exit(127); }
void exit(int status) { _exit(status); }
