

#include "dlink_internal.h"

#include <stdlib.h>
#include <string.h>

dlink_list_head_t dlink_module_list_head = DLINK_LIST_HEAD_INITIALIZER;

Result dlink_add_module(module_input_t input, dlink_module_t **out) {
  dlink_module_t *mod = malloc(sizeof(*mod));
  if (mod == NULL) {
    return DLINK_ERR_OUT_OF_MEMORY;
  }
  memset(mod, 0, sizeof(*mod));
  mod->refcount = 1;
  mod->input = input;

  module_list_node_t *node = malloc(sizeof(*node));
  if (node == NULL) {
    free(mod);
    return DLINK_ERR_OUT_OF_MEMORY;
  }
  node->module = mod;

  mod->state = MODULE_STATE_QUEUED;
  dlink_list_add_tail(&dlink_module_list_head, &node->list);

  *out = mod;
  return DLINK_OK;
}

Result dlink_scan_module(dlink_module_t *mod) {
  Result r;
  uint8_t *module_base = mod->input.base;
  module_header_t *mod_header = (module_header_t *)&module_base[*(uint32_t *)&module_base[4]];
  Elf64_Dyn *dynamic = (Elf64_Dyn *)(((uint8_t *)mod_header) + mod_header->dynamic_off);
  mod->dynamic = dynamic;

  const char mod_magic[] = "MOD0";
  if (mod_header->magic != *((uint32_t *)mod_magic)) {
    r = DLINK_ERR_INVALID_MODULE_HEADER;
    goto fail;
  }

  r = elf_dynamic_find_offset(dynamic, DT_HASH, (void **)&mod->hash, module_base);
  if (r != DLINK_OK && r != DLINK_ERR_MISSING_DT_ENTRY) {
    goto fail;
  }

  r = elf_dynamic_find_offset(dynamic, DT_STRTAB, (void **)&mod->strtab, module_base);
  if (r != DLINK_OK && r != DLINK_ERR_MISSING_DT_ENTRY) {
    goto fail;
  }

  r = elf_dynamic_find_offset(mod->dynamic, DT_SYMTAB, (void **)&mod->symtab, mod->input.base);
  if (r != DLINK_OK && r != DLINK_ERR_MISSING_DT_ENTRY) {
    return r;
  }

  uint64_t syment;
  r = elf_dynamic_find_value(mod->dynamic, DT_SYMENT, &syment);
  if (r == DLINK_OK) {
    if (syment != sizeof(Elf64_Sym)) {
      return DLINK_ERR_INVALID_SYM_ENT;
    }
  } else if (r != DLINK_ERR_MISSING_DT_ENTRY) {
    return r;
  }

  for (Elf64_Dyn *walker = dynamic; walker->d_tag != DT_NULL; walker++) {
    if (walker->d_tag == DT_NEEDED) {
      dlink_module_t *dep;
      DLINK_ASSERT_OK(
          fail, dlink_discover_module(mod->strtab + walker->d_val, &dep, mod->input.is_global));
      module_list_node_t *node = malloc(sizeof(*node));
      if (node == NULL) {
        dlink_decref_module(dep);
        r = DLINK_ERR_OUT_OF_MEMORY;
        goto fail;
      }
      node->module = dep;
      dlink_list_add_tail(&mod->dependencies, &node->list);
    }
  }

  mod->state = MODULE_STATE_SCANNED;

  return DLINK_OK;

fail:
  return r;
}

Result dlink_initialize_module(dlink_module_t *mod) {
  if (mod->state != MODULE_STATE_RELOCATED) {
    return DLINK_ERR_INVALID_MODULE_STATE;
  }

  void (**init_array)(void);
  size_t init_array_size;

  Result r;
  r = elf_dynamic_find_offset(mod->dynamic, DT_INIT_ARRAY, (void **)&init_array, mod->input.base);
  if (r == DLINK_OK) {
    DLINK_ASSERT_OK(fail, elf_dynamic_find_value(mod->dynamic, DT_INIT_ARRAYSZ, &init_array_size));

    for (size_t i = 0; i < init_array_size / sizeof(init_array[0]); i++) {
      init_array[i]();
    }
  } else if (r != DLINK_ERR_MISSING_DT_ENTRY) {
    goto fail;
  }

  mod->state = MODULE_STATE_INITIALIZED;
  return DLINK_OK;
fail:
  return r;
}

Result dlink_finalize_module(dlink_module_t *mod) {
  if (mod->state != MODULE_STATE_INITIALIZED) {
    return DLINK_ERR_INVALID_MODULE_STATE;
  }

  void (**fini_array)(void);
  size_t fini_array_size;

  Result r;
  r = elf_dynamic_find_offset(mod->dynamic, DT_FINI_ARRAY, (void **)&fini_array, mod->input.base);
  if (r == DLINK_OK) {
    DLINK_ASSERT_OK(fail, elf_dynamic_find_value(mod->dynamic, DT_FINI_ARRAYSZ, &fini_array_size));

    for (size_t i = 0; i < fini_array_size / sizeof(fini_array[0]); i++) {
      fini_array[i]();
    }
  } else if (r != DLINK_ERR_MISSING_DT_ENTRY) {
    goto fail;
  }

  mod->state = MODULE_STATE_FINALIZED;
  return DLINK_OK;
fail:
  return r;
}

Result dlink_process_modules(void) {
  Result r = DLINK_OK;
  dlink_list_foreach(&dlink_module_list_head, i) {
    module_list_node_t *node = dlink_list_entry(module_list_node_t, list, i);
    if (node->module->state == MODULE_STATE_QUEUED) {
      DLINK_ASSERT_OK(fail, dlink_scan_module(node->module));
    }
  }
  dlink_list_foreach(&dlink_module_list_head, i) {
    module_list_node_t *node = dlink_list_entry(module_list_node_t, list, i);
    if (node->module->state == MODULE_STATE_SCANNED) {
      DLINK_ASSERT_OK(fail, dlink_relocate_module(node->module));
    }
  }
  dlink_list_foreach(&dlink_module_list_head, i) {
    module_list_node_t *node = dlink_list_entry(module_list_node_t, list, i);
    if (node->module->state == MODULE_STATE_RELOCATED) {
      DLINK_ASSERT_OK(fail, dlink_initialize_module(node->module));
    }
  }
fail:
  return r;
}

Result dlink_decref_module(dlink_module_t *mod) {
  if (--(mod->refcount) == 0) {
    return dlink_destroy_module(mod);
  }
  return DLINK_OK;
}

Result dlink_destroy_module(dlink_module_t *mod) {
  Result r = DLINK_OK;
  if (mod->state == MODULE_STATE_INITIALIZED) {
    dlink_finalize_module(mod);
  }

  dlink_list_foreach(&mod->dependencies, i) {
    module_list_node_t *node = dlink_list_entry(module_list_node_t, list, i);
    dlink_decref_module(node->module);
    dlink_list_delink(i);
    i = i->prev;
    free(node);
  }

  if (mod->input.loader) {
    mod->input.loader->unload(&mod->input);
  }

  dlink_list_foreach(&dlink_module_list_head, i) {
    module_list_node_t *node = dlink_list_entry(module_list_node_t, list, i);
    if (node->module == mod) {
      dlink_list_delink(i);
      i = i->prev;
      free(node);
    }
  }

  free(mod);
  return r;
}
