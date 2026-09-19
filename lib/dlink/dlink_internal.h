

#pragma once

#include <dlink/dlink.h>
#include <switch.h>

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct dlink_list_head dlink_list_head_t;
struct dlink_list_head {
  dlink_list_head_t *prev;
  dlink_list_head_t *next;
};

#define DLINK_LIST_HEAD_INITIALIZER {.prev = NULL, .next = NULL}

#define dlink_list_entry(type, field, head) ((type *)(((void *)(head)) - offsetof(type, field)))

static inline void dlink_list_add_tail(dlink_list_head_t *list, dlink_list_head_t *item) {
  while (list->next != NULL) {
    list = list->next;
  }
  list->next = item;
  item->prev = list;
  item->next = NULL;
}

static inline void dlink_list_delink(dlink_list_head_t *item) {
  if (item->prev != NULL) {
    item->prev->next = item->next;
  }
  if (item->next != NULL) {
    item->next->prev = item->prev;
  }
}

#define dlink_list_foreach(list, i)                                                                \
  for (dlink_list_head_t *i = (list)->next; i != NULL; i = i->next)

#define DLINK_ASSERT_OK(label, expr)                                                               \
  if (((r) = (expr)) != DLINK_OK) {                                                                \
    goto label;                                                                                    \
  }

typedef struct {
  int64_t d_tag;
  union {
    uint64_t d_val;
    void *d_ptr;
  };
} Elf64_Dyn;

typedef struct {
  uint64_t r_offset;
  uint32_t r_reloc_type;
  uint32_t r_symbol;
  uint64_t r_addend;
} Elf64_Rela;

typedef struct {
  uint64_t r_offset;
  uint32_t r_reloc_type;
  uint32_t r_symbol;
} Elf64_Rel;

typedef struct {
  uint32_t st_name;
  uint8_t st_info;
  uint8_t st_other;
  uint16_t st_shndx;
  uint64_t st_value;
  uint64_t st_size;
} Elf64_Sym;

#define STN_UNDEF 0
#define SHN_UNDEF 0

static_assert(sizeof(Elf64_Rela) == 0x18, "Elf64_Rela size should be 0x18");

enum {
  DT_NULL = 0,
  DT_NEEDED = 1,
  DT_PLTRELSZ = 2,
  DT_PLTGOT = 3,
  DT_HASH = 4,
  DT_STRTAB = 5,
  DT_SYMTAB = 6,
  DT_RELA = 7,
  DT_RELASZ = 8,
  DT_RELAENT = 9,
  DT_STRSZ = 10,
  DT_SYMENT = 11,
  DT_SYMBOLIC = 16,
  DT_REL = 17,
  DT_RELSZ = 18,
  DT_RELENT = 19,
  DT_PLTREL = 20,
  DT_JMPREL = 23,
  DT_INIT_ARRAY = 25,
  DT_FINI_ARRAY = 26,
  DT_INIT_ARRAYSZ = 27,
  DT_FINI_ARRAYSZ = 28,
  DT_FLAGS = 30,
  DT_GNU_HASH = 0x6ffffef5,
  DT_RELACOUNT = 0x6ffffff9,
  DT_RELR = 36,
  DT_RELRSZ = 35,
  DT_RELRENT = 37,
};

typedef struct {
  uint32_t magic, dynamic_off, bss_start_off, bss_end_off;
  uint32_t unwind_start_off, unwind_end_off, module_object_off;
} module_header_t;

typedef struct dlink_module dlink_module_t;
typedef struct dlink_loader dlink_loader_t;

typedef enum {
  MODULE_STATE_INVALID = 0,
  MODULE_STATE_QUEUED = 1,
  MODULE_STATE_SCANNED = 2,
  MODULE_STATE_RELOCATED = 3,
  MODULE_STATE_INITIALIZED = 4,
  MODULE_STATE_FINALIZED = 5,
  MODULE_STATE_UNLOADED = 6,
} module_state_t;

typedef struct {
  const char *name;
  void *base;
  dlink_loader_t *loader;
  void *loader_data;
  bool is_global;
  bool has_run_basic_relocations;
} module_input_t;

typedef struct {
  dlink_module_t *module;
  dlink_list_head_t list;
} module_list_node_t;

struct dlink_module {
  module_state_t state;
  int refcount;

  module_input_t input;

  dlink_list_head_t dependencies;

  Elf64_Dyn *dynamic;
  Elf64_Sym *symtab;
  const char *strtab;
  uint32_t *hash;
};

struct dlink_loader {
  Result (*can_load)(void *file, size_t file_size);

  Result (*load)(module_input_t *spec_out, void *file, size_t file_size);
  Result (*unload)(module_input_t *spec);
};

extern dlink_list_head_t dlink_module_list_head;
extern dlink_loader_t dlink_loader_dnro;

#define DNRO_TAG_OFFSET 0x3C
#define DNRO_TAG_SIZE 4
#define DNRO_TAG "DNRO"

void dlink_sha256(const void *data, size_t len, unsigned char out[32]);

Result dlink_add_module(module_input_t input, dlink_module_t **out);
Result dlink_discover_module(const char *name, dlink_module_t **out, bool is_global);
Result dlink_decref_module(dlink_module_t *module);
Result dlink_process_modules(void);
Result dlink_scan_module(dlink_module_t *mod);
Result dlink_relocate_module(dlink_module_t *mod);
Result dlink_initialize_module(dlink_module_t *mod);
Result dlink_finalize_module(dlink_module_t *mod);
Result dlink_destroy_module(dlink_module_t *mod);

/* host.c */
Result dlink_provide(const char *name, void *addr);
Result dlink_register_host(void);

Result dlink_resolve_load_symbol(dlink_module_t *find_mod, const char *find_name, Elf64_Sym **def,
                                 dlink_module_t **defining_module);
Result dlink_resolve_dependency_symbol(dlink_module_t *find_mod, const char *find_name,
                                       Elf64_Sym **def, dlink_module_t **defining_module);

Result elf_dynamic_find_value(Elf64_Dyn *dynamic, int64_t tag, uint64_t *value);
Result elf_dynamic_find_offset(Elf64_Dyn *dynamic, int64_t tag, void **value, void *base);
uint64_t elf_hash_string(const char *string);

void *dlink_alloc_pages(size_t size);
void dlink_free_pages(void *pages);

static inline Result dlink_result_or(Result a, Result b) {
  if (R_FAILED(a)) {
    return a;
  }
  return b;
}

#ifdef __cplusplus
}
#endif
