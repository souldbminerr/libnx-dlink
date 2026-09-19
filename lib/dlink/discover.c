

#include "dlink_internal.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *dlink_default_search_path[] = {
    "sdmc:/lib/",
};

#define DLINK_MAX_SEARCH_PATHS 8
#define DLINK_MAX_PATH 0x301

static char s_search_paths[DLINK_MAX_SEARCH_PATHS][DLINK_MAX_PATH];
static size_t s_num_search_paths = 0;
static bool s_search_paths_init = false;

static void dlink_ensure_search_paths_fallback(void);

static void dlink_ensure_search_paths(void) {
  if (s_search_paths_init) {
    return;
  }
  size_t n = sizeof(dlink_default_search_path) / sizeof(dlink_default_search_path[0]);
  if (n > DLINK_MAX_SEARCH_PATHS) {
    n = DLINK_MAX_SEARCH_PATHS;
  }
  for (size_t i = 0; i < n; i++) {
    strncpy(s_search_paths[i], dlink_default_search_path[i], DLINK_MAX_PATH - 1);
    s_search_paths[i][DLINK_MAX_PATH - 1] = 0;
  }
  s_num_search_paths = n;
  s_search_paths_init = true;
}

void dlink_set_library_dir(const char *path) {
  if (path == NULL) {
    dlink_set_search_paths(NULL, 0);
    return;
  }
  dlink_set_search_paths(&path, 1);
}

void dlink_set_search_paths(const char *const *paths, size_t count) {
  s_search_paths_init = true;
  if (paths == NULL || count == 0) {
    s_num_search_paths = 0;
    dlink_ensure_search_paths_fallback();
    return;
  }
  if (count > DLINK_MAX_SEARCH_PATHS) {
    count = DLINK_MAX_SEARCH_PATHS;
  }
  for (size_t i = 0; i < count; i++) {
    strncpy(s_search_paths[i], paths[i] ? paths[i] : "", DLINK_MAX_PATH - 1);
    s_search_paths[i][DLINK_MAX_PATH - 1] = 0;
  }
  s_num_search_paths = count;
}

static void dlink_ensure_search_paths_fallback(void) {
  s_search_paths_init = false;
  dlink_ensure_search_paths();
}

static dlink_loader_t *dlink_loaders[] = {
    &dlink_loader_dnro,
};

static Result dlink_load_module(FILE *f, const char *name_src, dlink_module_t **out,
                                bool is_global) {
  if (fseek(f, 0, SEEK_END) != 0) {
    fclose(f);
    return DLINK_ERR_FAILED_TO_READ_MODULE;
  }

  long raw_size = ftell(f);
  if (raw_size == -1) {
    fclose(f);
    return DLINK_ERR_FAILED_TO_READ_MODULE;
  }

  if (fseek(f, 0, SEEK_SET) != 0) {
    fclose(f);
    return DLINK_ERR_FAILED_TO_READ_MODULE;
  }

  size_t file_size = ((size_t)raw_size + 0xFFF) & ~(size_t)0xFFF;
  if (file_size == 0) {
    file_size = 0x1000;
  }

  void *file_buffer = dlink_alloc_pages(file_size);
  if (file_buffer == NULL) {
    fclose(f);
    return DLINK_ERR_OUT_OF_MEMORY;
  }

  size_t total_read = 0;
  while (total_read < (size_t)raw_size) {
    size_t r = fread((uint8_t *)file_buffer + total_read, 1, (size_t)raw_size - total_read, f);
    if (r == 0) {
      dlink_free_pages(file_buffer);
      fclose(f);
      return DLINK_ERR_FAILED_TO_READ_MODULE;
    }
    total_read += r;
  }
  fclose(f);
  memset((uint8_t *)file_buffer + raw_size, 0, file_size - (size_t)raw_size);

  module_input_t input;
  input.name = name_src;
  input.has_run_basic_relocations = false;
  input.is_global = is_global;
  input.loader = NULL;
  input.loader_data = NULL;
  input.base = NULL;

  for (size_t i = 0; i < sizeof(dlink_loaders) / sizeof(dlink_loaders[0]); i++) {
    if (dlink_loaders[i]->can_load(file_buffer, (size_t)file_size) == DLINK_OK) {
      Result r;
      if ((r = dlink_loaders[i]->load(&input, file_buffer, (size_t)file_size)) != DLINK_OK) {
        dlink_free_pages(file_buffer);
        return r;
      }
      return dlink_add_module(input, out);
    }
  }
  dlink_free_pages(file_buffer);
  return DLINK_ERR_NO_LOADER_FOR_MODULE;
}

Result dlink_discover_module(const char *name_src, dlink_module_t **out, bool is_global) {
  dlink_ensure_search_paths();

  char name[DLINK_MAX_PATH];
  strncpy(name, name_src, DLINK_MAX_PATH - 1);
  name[DLINK_MAX_PATH - 1] = 0;

  size_t len = strlen(name);

  for (size_t i = 0; i < len; i++) {
    if (name[i] == '/' || name[i] == ':') {
      FILE *f = fopen(name, "rb");
      if (f == NULL) {
        return DLINK_ERR_FAILED_TO_FIND_MODULE;
      }
      return dlink_load_module(f, name_src, out, is_global);
    }
  }

  char path[DLINK_MAX_PATH];
  for (size_t i = 0; i < s_num_search_paths; i++) {
    size_t plen = strlen(s_search_paths[i]);
    if (plen >= DLINK_MAX_PATH - 1) {
      continue;
    }
    size_t room = DLINK_MAX_PATH - 1 - plen;
    size_t nlen = strlen(name);
    if (nlen > room) {
      nlen = room;
    }
    memcpy(path, s_search_paths[i], plen);
    memcpy(path + plen, name, nlen);
    path[plen + nlen] = 0;

    FILE *f = fopen(path, "rb");
    if (f == NULL) {
      continue;
    }

    return dlink_load_module(f, name_src, out, is_global);
  }

  if (len > 3 && strcmp(name + len - 3, ".so") == 0) {
    name[len - 3] = 0;
    return dlink_discover_module(name, out, is_global);
  }

  if (!(len > 5 && strcmp(name + len - 5, ".dnro") == 0)) {
    if (len + 5 < DLINK_MAX_PATH) {
      strcat(name, ".dnro");
      return dlink_discover_module(name, out, is_global);
    }
  }
  if (!(len > 4 && strcmp(name + len - 4, ".nro") == 0)) {
    if (len + 4 < DLINK_MAX_PATH) {
      strcat(name, ".nro");
      return dlink_discover_module(name, out, is_global);
    }
  }
  return DLINK_ERR_FAILED_TO_FIND_MODULE;
}
