

#include "dlink_internal.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
  dlink_module_t *module;
} dlink_handle_t;

static char s_error_buffer[512];
static const char *s_error = NULL;

static void dlink_set_error(Result r) {
  snprintf(s_error_buffer, sizeof(s_error_buffer), "dlink result 0x%x (module %d desc %d)", r,
           R_MODULE(r), R_DESCRIPTION(r));
  s_error = s_error_buffer;
}

void *dlink_open(const char *path, int flags) {
  Result r;
  dlink_handle_t *handle = malloc(sizeof(*handle));
  if (handle == NULL) {
    r = DLINK_ERR_OUT_OF_MEMORY;
    goto fail;
  }
  handle->module = NULL;
  if (path == NULL) {
    return handle;
  }
  DLINK_ASSERT_OK(fail_handle,
                  dlink_discover_module(path, &handle->module, (flags & DLINK_RTLD_GLOBAL) != 0));
  DLINK_ASSERT_OK(fail_module, dlink_process_modules());

  return handle;

fail_module:
  dlink_decref_module(handle->module);
fail_handle:
  free(handle);
fail:
  dlink_set_error(r);
  return NULL;
}

void *dlink_symbol(void *ptr, const char *symbol) {
  dlink_handle_t *handle = ptr;
  Result r;
  Elf64_Sym *def;
  dlink_module_t *def_mod;

  if (symbol == NULL) {
    r = DLINK_ERR_INVALID_MODULE_TYPE;
    goto fail;
  }

  if (handle == NULL || handle->module == NULL) {
    r = dlink_resolve_load_symbol(NULL, symbol, &def, &def_mod);
  } else {
    r = dlink_resolve_dependency_symbol(handle->module, symbol, &def, &def_mod);
  }
  if (r != DLINK_OK) {
    goto fail;
  }
  return (uint8_t *)def_mod->input.base + def->st_value;

fail:
  dlink_set_error(r);
  return NULL;
}

int dlink_close(void *ptr) {
  dlink_handle_t *handle = ptr;
  if (handle != NULL) {
    if (handle->module) {
      dlink_decref_module(handle->module);
    }
    free(handle);
  }
  return 0;
}

const char *dlink_error(void) {
  const char *e = s_error;
  s_error = NULL;
  return e;
}

void *dlopen(const char *path, int flags) {
  int dlink_flags = (flags & RTLD_GLOBAL) ? DLINK_RTLD_GLOBAL : DLINK_RTLD_LOCAL;
  return dlink_open(path, dlink_flags);
}

void *dlsym(void *handle, const char *symbol) {
  return dlink_symbol(handle, symbol);
}

int dlclose(void *handle) {
  return dlink_close(handle);
}

const char *dlerror(void) {
  return dlink_error();
}
