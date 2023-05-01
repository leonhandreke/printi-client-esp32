#include <stdio.h>
#include <string.h>

char *strreplace(char *s, const char *s1, const char *s2) {
  char *p = strstr(s, s1);
  if (p != NULL) {
    size_t len1 = strlen(s1);
    size_t len2 = strlen(s2);
    if (len1 != len2)
      memmove(p + len2, p + len1, strlen(p + len1) + 1);
    memcpy(p, s2, len2);
  }
  return s;
}