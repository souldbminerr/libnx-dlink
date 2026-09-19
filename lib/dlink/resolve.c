

#include "dlink_internal.h"

#include <stdlib.h>
#include <string.h>

static Result dlink_try_resolve_symbol(dlink_module_t *try_mod, const char *find_name,
                                       uint64_t find_name_hash, Elf64_Sym **def,
                                       dlink_module_t **defining_module, bool require_global) {
  if (require_global && !try_mod->input.is_global) {
    return DLINK_ERR_COULD_NOT_RESOLVE;
  }
  if (try_mod->symtab == NULL) {
    return DLINK_ERR_COULD_NOT_RESOLVE;
  }
  if (try_mod->strtab == NULL) {
    return DLINK_ERR_COULD_NOT_RESOLVE;
  }
  if (try_mod->hash != NULL) {
    uint32_t nbucket = try_mod->hash[0];
    uint32_t nchain = try_mod->hash[1];
    (void)nchain;
    uint32_t index = try_mod->hash[2 + (find_name_hash % nbucket)];
    uint32_t *chains = try_mod->hash + 2 + nbucket;
    while (index != 0 && strcmp(find_name, try_mod->strtab + try_mod->symtab[index].st_name) != 0) {
      index = chains[index];
    }
    if (index == STN_UNDEF) {
      return DLINK_ERR_COULD_NOT_RESOLVE;
    }
    Elf64_Sym *sym = &try_mod->symtab[index];
    if (sym->st_shndx == SHN_UNDEF) {
      return DLINK_ERR_COULD_NOT_RESOLVE;
    }
    *def = sym;
    *defining_module = try_mod;
    return DLINK_OK;
  }
  return DLINK_ERR_COULD_NOT_RESOLVE;
}

Result dlink_resolve_load_symbol(dlink_module_t *find_mod, const char *find_name, Elf64_Sym **def,
                                 dlink_module_t **defining_module) {
  uint64_t find_name_hash = elf_hash_string(find_name);
  dlink_list_foreach(&dlink_module_list_head, i) {
    Result r = dlink_try_resolve_symbol(dlink_list_entry(module_list_node_t, list, i)->module,
                                        find_name, find_name_hash, def, defining_module, true);
    if (r == DLINK_ERR_COULD_NOT_RESOLVE) {
      continue;
    } else {
      return r;
    }
  }

  if (find_mod != NULL) {
    return dlink_try_resolve_symbol(find_mod, find_name, find_name_hash, def, defining_module,
                                    false);
  }

  return DLINK_ERR_COULD_NOT_RESOLVE;
}

Result dlink_resolve_dependency_symbol(dlink_module_t *find_mod, const char *find_name,
                                       Elf64_Sym **def, dlink_module_t **defining_module) {
  uint64_t find_name_hash = elf_hash_string(find_name);
  Result r;
  r = dlink_try_resolve_symbol(find_mod, find_name, find_name_hash, def, defining_module, false);
  if (r != DLINK_ERR_COULD_NOT_RESOLVE) {
    return r;
  }
  dlink_list_foreach(&find_mod->dependencies, i) {
    r = dlink_try_resolve_symbol(dlink_list_entry(module_list_node_t, list, i)->module, find_name,
                                 find_name_hash, def, defining_module, false);
    if (r == DLINK_ERR_COULD_NOT_RESOLVE) {
      continue;
    } else {
      return r;
    }
  }
  dlink_list_foreach(&find_mod->dependencies, i) {
    r = dlink_resolve_dependency_symbol(dlink_list_entry(module_list_node_t, list, i)->module,
                                        find_name, def, defining_module);
    if (r == DLINK_ERR_COULD_NOT_RESOLVE) {
      continue;
    } else {
      return r;
    }
  }
  return DLINK_ERR_COULD_NOT_RESOLVE;
}
