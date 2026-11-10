/**
 * @file    util.c
 * @brief   Small helpers shared by the portable application modules.
 */
#include "app/util.h"

static char ascii_lower(char c)
{
    return (c >= 'A' && c <= 'Z') ? (char)(c - 'A' + 'a') : c;
}

bool str_ieq(const char *a, const char *b)
{
    if (a == NULL || b == NULL) {
        return false;
    }
    while (*a != '\0' && *b != '\0') {
        if (ascii_lower(*a) != ascii_lower(*b)) {
            return false;
        }
        a++;
        b++;
    }
    return *a == *b;
}
