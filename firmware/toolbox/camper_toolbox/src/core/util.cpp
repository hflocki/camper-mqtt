#include "util.h"

bool normalizeMac(const char* in, char* out, size_t size) {
    char hex[13];
    int n = 0;
    for (const char* p = in; *p; p++) {
        if (*p == ':' || *p == '-' || *p == ' ') continue;
        if (!isxdigit((unsigned char)*p) || n >= 12) return false;
        hex[n++] = toupper((unsigned char)*p);
    }
    if (n == 0) { out[0] = '\0'; return true; }
    if (n != 12 || size < 18) return false;
    for (int i = 0; i < 6; i++) {
        out[i * 3] = hex[i * 2];
        out[i * 3 + 1] = hex[i * 2 + 1];
        out[i * 3 + 2] = (i < 5) ? ':' : '\0';
    }
    return true;
}

void bytesToHex(const uint8_t* d, size_t n, char* out, size_t size) {
    size_t pos = 0;
    out[0] = '\0';
    for (size_t i = 0; i < n && pos + 3 < size; i++) {
        pos += snprintf(out + pos, size - pos, i ? " %02X" : "%02X", d[i]);
    }
}

bool containsIgnoreCase(const char* h, const char* n) {
    if (!h || !n) return false;
    size_t nl = strlen(n);
    if (nl == 0) return true;
    for (; *h; h++) {
        if (strncasecmp(h, n, nl) == 0) return true;
    }
    return false;
}

void printableCopy(const uint8_t* d, size_t n, char* out, size_t size) {
    size_t i = 0;
    for (; i < n && i < size - 1; i++) out[i] = (d[i] >= 0x20 && d[i] < 0x7F) ? (char)d[i] : '.';
    out[i] = '\0';
}
