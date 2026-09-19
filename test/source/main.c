#include <stdio.h>
#include <string.h>

#include <dlink/dlink.h>
#include <switch.h>

int dlink_test_host_add(int a, int b) {
  return a + b;
}

int main(int argc, char **argv) {
  consoleInit(NULL);
  printf("Dynamic Link test\n");
  printf("======================\n");

  Handle proc = envGetOwnProcessHandle();
  printf("own process handle: 0x%x\n", proc);

  void *global = dlopen(NULL, RTLD_GLOBAL);
  printf("[%s] dlopen(NULL) -> %p\n", global ? "ok" : "FAIL", global);
  if (global == NULL) {
    printf("  error: %s\n", dlerror());
  }

  void *sym = dlsym(global, "this_symbol_does_not_exist_anywhere");
  printf("[%s] dlsym(bogus) -> %p (expect NULL)\n", sym == NULL ? "ok" : "FAIL", sym);
  if (sym == NULL) {
    printf("  error: %s\n", dlerror());
  }

  void *mod = dlopen("no_such_module_xyz", RTLD_LOCAL);
  printf("[%s] dlopen(missing) -> %p (expect NULL)\n", mod == NULL ? "ok" : "FAIL", mod);
  if (mod == NULL) {
    printf("  error: %s\n", dlerror());
  }

  printf("[ok] dlclose(NULL) -> %d\n", dlclose(NULL));
  printf("[ok] dlclose(global) -> %d\n", dlclose(global));

  Result hr = dlink_register_host();
  printf("[%s] dlink_register_host() -> 0x%x\n", R_SUCCEEDED(hr) ? "ok" : "FAIL", hr);

  hr = DLINK_PROVIDE(snprintf);
  printf("[%s] DLINK_PROVIDE(snprintf) -> 0x%x\n", R_SUCCEEDED(hr) ? "ok" : "FAIL", hr);
  hr = DLINK_PROVIDE(dlink_test_host_add);
  printf("[%s] DLINK_PROVIDE(host_add) -> 0x%x\n", R_SUCCEEDED(hr) ? "ok" : "FAIL", hr);
  hr = DLINK_PROVIDE(fopen);
  printf("[%s] DLINK_PROVIDE(fopen) -> 0x%x\n", R_SUCCEEDED(hr) ? "ok" : "FAIL", hr);
  hr = DLINK_PROVIDE(fread);
  printf("[%s] DLINK_PROVIDE(fread) -> 0x%x\n", R_SUCCEEDED(hr) ? "ok" : "FAIL", hr);
  hr = DLINK_PROVIDE(fwrite);
  printf("[%s] DLINK_PROVIDE(fwrite) -> 0x%x\n", R_SUCCEEDED(hr) ? "ok" : "FAIL", hr);
  hr = DLINK_PROVIDE(fclose);
  printf("[%s] DLINK_PROVIDE(fclose) -> 0x%x\n", R_SUCCEEDED(hr) ? "ok" : "FAIL", hr);
  hr = DLINK_PROVIDE(remove);
  printf("[%s] DLINK_PROVIDE(remove) -> 0x%x\n", R_SUCCEEDED(hr) ? "ok" : "FAIL", hr);
  hr = DLINK_PROVIDE(strcmp);
  printf("[%s] DLINK_PROVIDE(strcmp) -> 0x%x\n", R_SUCCEEDED(hr) ? "ok" : "FAIL", hr);
  void *gh = dlopen(NULL, RTLD_GLOBAL);
  void *hsym = dlsym(gh, "consoleGetDefault");
  printf("[%s] harvest-only dlsym(consoleGetDefault) -> %p\n", hsym ? "ok" : "FAIL", hsym);
  dlclose(gh);

  dlink_set_library_dir("sdmc:/switch/dusklight/modules/");
  void *test = dlopen("dlink_testmod", RTLD_GLOBAL | RTLD_NOW);
  if (test != NULL) {
    printf("[ok] dlopen(dlink_testmod) -> %p\n", test);
    void *entry = dlsym(test, "dlink_testmod_entry");
    printf("  dlink_testmod_entry -> %p\n", entry);
    if (entry != NULL) {
      int (*entry_fn)(void) = entry;
      int rc = entry_fn();
      printf("  entry() returned 0x%x [%s]\n", rc, rc == 0x6D6F64 ? "PASS" : "FAIL");
    } else {
      printf("  symbol error: %s\n", dlerror());
    }
    dlclose(test);
  } else {
    printf("[fail] dlink_testmod not loaded: %s\n", dlerror());
  }

  void *host = dlopen("example_host", RTLD_GLOBAL | RTLD_NOW);
  if (host == NULL) {
    printf("[fail] example_host not loaded: %s\n", dlerror());
  } else {
    printf("[ok] dlopen(example_host) -> %p\n", host);
    const char *names[] = {"example_host_entry", "example_host_io", "example_host_console"};
    const int magics[] = {0x484F5354, 0x494F5A5A, 0x434F4E53};
    for (int i = 0; i < 3; i++) {
      int (*fn)(void) = (int (*)(void))dlsym(host, names[i]);
      if (fn == NULL) {
        printf("  [%s] missing: %s\n", names[i], dlerror());
        continue;
      }
      int rc2 = fn();
      printf("  %s() returned 0x%x [%s]\n", names[i], rc2, rc2 == magics[i] ? "PASS" : "FAIL");
    }
    dlclose(host);
  }

  printf("\ndone. Press + to exit.\n");

  padConfigureInput(1, HidNpadStyleSet_NpadStandard);
  PadState pad;
  padInitializeDefault(&pad);
  while (appletMainLoop()) {
    padUpdate(&pad);
    if (padGetButtonsDown(&pad) & HidNpadButton_Plus) {
      break;
    }
    consoleUpdate(NULL);
  }
  consoleExit(NULL);
  return 0;
}
