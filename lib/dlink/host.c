#include "dlink_internal.h"

#include <stdlib.h>
#include <string.h>

typedef struct {
  dlink_module_t mod;
  module_list_node_t node;
  bool present;
  Elf64_Sym *symtab;
  size_t sym_cap;
  size_t sym_count;
  char *strtab;
  size_t str_cap;
  size_t str_len;
  uint32_t *hash;
} host_module_t;

static host_module_t s_provided;
static host_module_t s_harvested;

static void host_module_init(host_module_t *hm) {
  memset(hm, 0, sizeof(*hm));
  memset(&hm->mod, 0, sizeof(hm->mod));
  hm->mod.state = MODULE_STATE_INITIALIZED;
  hm->mod.refcount = 0x7fffffff;
  hm->mod.input.is_global = true;
  hm->node.module = &hm->mod;
  dlink_list_add_tail(&dlink_module_list_head, &hm->node.list);
  hm->present = true;
}

static Result host_rebuild_hash(host_module_t *hm) {
  size_t nbucket = 1;
  while (nbucket < hm->sym_count * 2 + 1) {
    nbucket *= 2;
  }
  size_t nchain = hm->sym_count + 1;
  uint32_t *hash = malloc((2 + nbucket + nchain) * sizeof(uint32_t));
  if (hash == NULL) {
    return DLINK_ERR_OUT_OF_MEMORY;
  }
  hash[0] = (uint32_t)nbucket;
  hash[1] = (uint32_t)nchain;
  memset(hash + 2, 0, (nbucket + nchain) * sizeof(uint32_t));
  uint32_t *buckets = hash + 2;
  uint32_t *chains = hash + 2 + nbucket;
  for (size_t i = 1; i <= hm->sym_count; i++) {
    uint32_t b = (uint32_t)(elf_hash_string(hm->strtab + hm->symtab[i].st_name) % nbucket);
    chains[i] = buckets[b];
    buckets[b] = (uint32_t)i;
  }
  free(hm->hash);
  hm->hash = hash;
  hm->mod.hash = hash;
  return DLINK_OK;
}

Result dlink_provide(const char *name, void *addr) {
  if (name == NULL || addr == NULL) {
    return DLINK_ERR_INVALID_MODULE_TYPE;
  }
  host_module_t *hm = &s_provided;
  if (!hm->present) {
    host_module_init(hm);
  }

  size_t namelen = strlen(name) + 1;
  if (hm->sym_count + 1 >= hm->sym_cap) {
    size_t ncap = hm->sym_cap ? hm->sym_cap * 2 : 16;
    Elf64_Sym *nsym = realloc(hm->symtab, ncap * sizeof(Elf64_Sym));
    if (nsym == NULL) {
      return DLINK_ERR_OUT_OF_MEMORY;
    }
    hm->symtab = nsym;
    hm->sym_cap = ncap;
    hm->mod.symtab = nsym;
  }
  if (hm->str_len + namelen >= hm->str_cap) {
    size_t ncap = hm->str_cap ? hm->str_cap * 2 : 256;
    while (hm->str_len + namelen >= ncap) {
      ncap *= 2;
    }
    char *nstr = realloc(hm->strtab, ncap);
    if (nstr == NULL) {
      return DLINK_ERR_OUT_OF_MEMORY;
    }
    hm->strtab = nstr;
    hm->str_cap = ncap;
    hm->mod.strtab = nstr;
  }

  if (hm->sym_count == 0) {
    memset(&hm->symtab[0], 0, sizeof(Elf64_Sym));
    hm->strtab[0] = 0;
    hm->str_len = 1;
    hm->sym_count = 0;
  }
  hm->sym_count++;
  Elf64_Sym *sym = &hm->symtab[hm->sym_count];
  sym->st_name = (uint32_t)hm->str_len;
  sym->st_info = 0x10;
  sym->st_other = 0;
  sym->st_shndx = 1;
  sym->st_value = (uint64_t)addr;
  sym->st_size = 0;
  memcpy(hm->strtab + hm->str_len, name, namelen);
  hm->str_len += namelen;

  return host_rebuild_hash(hm);
}

Result dlink_register_host(void) {
  host_module_t *hm = &s_harvested;
  if (hm->present) {
    return DLINK_OK;
  }

  uint8_t *ra = (uint8_t *)__builtin_return_address(0);
  MemoryInfo mi;
  u32 pi = 0;
  if (R_FAILED(svcQueryMemory(&mi, &pi, (u64)ra))) {
    return DLINK_ERR_INVALID_MODULE_HEADER;
  }
  bool is_code = mi.type == MemType_CodeStatic || mi.type == MemType_ModuleCodeStatic ||
                 mi.type == MemType_ModuleCodeMutable;
  if (!is_code || !(mi.perm & Perm_R)) {
    return DLINK_ERR_INVALID_MODULE_HEADER;
  }
  uint8_t *base = (uint8_t *)mi.addr;
  if (*(uint32_t *)(base + 0x10) != NROHEADER_MAGIC) {
    return DLINK_ERR_INVALID_MODULE_HEADER;
  }
  uint32_t mod_off = *(uint32_t *)(base + 4);
  module_header_t *mh = (module_header_t *)(base + mod_off);
  const char mod_magic[] = "MOD0";
  if (mh->magic != *((uint32_t *)mod_magic)) {
    return DLINK_ERR_INVALID_MODULE_HEADER;
  }
  Elf64_Dyn *dynamic = (Elf64_Dyn *)((uint8_t *)mh + mh->dynamic_off);

  Elf64_Sym *symtab = NULL;
  const char *strtab = NULL;
  uint32_t *hash = NULL;
  uint64_t syment = 0;
  Result r;
  r = elf_dynamic_find_offset(dynamic, DT_HASH, (void **)&hash, base);
  if (r != DLINK_OK) {
    return r == DLINK_ERR_MISSING_DT_ENTRY ? DLINK_ERR_NEEDS_SYMTAB : r;
  }
  r = elf_dynamic_find_offset(dynamic, DT_STRTAB, (void **)&strtab, base);
  if (r != DLINK_OK) {
    return r == DLINK_ERR_MISSING_DT_ENTRY ? DLINK_ERR_NEEDS_STRTAB : r;
  }
  r = elf_dynamic_find_offset(dynamic, DT_SYMTAB, (void **)&symtab, base);
  if (r != DLINK_OK) {
    return r == DLINK_ERR_MISSING_DT_ENTRY ? DLINK_ERR_NEEDS_SYMTAB : r;
  }
  r = elf_dynamic_find_value(dynamic, DT_SYMENT, &syment);
  if (r == DLINK_OK) {
    if (syment != sizeof(Elf64_Sym)) {
      return DLINK_ERR_INVALID_SYM_ENT;
    }
  } else if (r != DLINK_ERR_MISSING_DT_ENTRY) {
    return r;
  }
  if (hash[0] == 0 || hash[0] > 0x100000) {
    return DLINK_ERR_INVALID_MODULE_HEADER;
  }

  host_module_init(hm);
  hm->mod.input.base = base;
  hm->mod.dynamic = dynamic;
  hm->mod.symtab = symtab;
  hm->mod.strtab = strtab;
  hm->mod.hash = hash;
  return DLINK_OK;
}
