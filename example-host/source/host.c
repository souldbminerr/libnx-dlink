#include <stdio.h>
#include <string.h>
#include <switch.h>

int dlink_test_host_add(int a, int b);

static volatile int s_seed = 0x1234;

__attribute__((visibility("default"))) int example_host_entry(void) {
  char buf[32];
  snprintf(buf, sizeof(buf), "%d", s_seed);
  int v = 0;
  for (const char *p = buf; *p; p++) {
    v = v * 10 + (*p - '0');
  }
  if (v != s_seed) {
    return -1;
  }
  return dlink_test_host_add(v, 1) == v + 1 ? 0x484F5354 : -1;
}

__attribute__((visibility("default"))) int example_host_io(void) {
  const char *msg = "dnro-io-test";
  char path[] = "sdmc:/TEST";
  FILE *f = fopen(path, "w");
  if (f == NULL) {
    return -1;
  }
  size_t n = fwrite(msg, 1, strlen(msg), f);
  fclose(f);
  if (n != strlen(msg)) {
    remove(path);
    return -1;
  }
  char buf[32];
  f = fopen(path, "r");
  if (f == NULL) {
    return -1;
  }
  size_t r = fread(buf, 1, sizeof(buf) - 1, f);
  fclose(f);
  buf[r < sizeof(buf) ? r : 0] = 0;
  if (r != strlen(msg) || strcmp(buf, msg) != 0) {
    remove(path);
    return -1;
  }
  if (remove(path) != 0) {
    return -1;
  }
  f = fopen(path, "r");
  if (f != NULL) {
    fclose(f);
    return -1;
  }
  return 0x494F5A5A;
}

__attribute__((visibility("default"))) int example_host_console(void) {
  return consoleGetDefault() != NULL ? 0x434F4E53 : -1;
}

int main(int argc, char **argv) {
  (void)argc;
  (void)argv;
  return 0;
}
