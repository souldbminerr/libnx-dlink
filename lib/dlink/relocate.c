

#include "dlink_internal.h"

#include <stdlib.h>
#include <string.h>

Result dlink_relocate_basic(void *module_base_void) {
  uint8_t *module_base = module_base_void;
  module_header_t *mod_header = (module_header_t *)&module_base[*(uint32_t *)&module_base[4]];
  Elf64_Dyn *dynamic = (Elf64_Dyn *)(((uint8_t *)mod_header) + mod_header->dynamic_off);
  uint64_t rela_offset = 0;
  uint64_t rela_size = 0;
  uint64_t rela_ent = 0;
  uint64_t rela_count = 0;

  if (mod_header->magic != 0x30444f4d) {
    return DLINK_ERR_INVALID_MODULE_HEADER;
  }

  Result r;
  DLINK_ASSERT_OK(fail, elf_dynamic_find_value(dynamic, DT_RELA, &rela_offset));
  DLINK_ASSERT_OK(fail, elf_dynamic_find_value(dynamic, DT_RELASZ, &rela_size));
  DLINK_ASSERT_OK(fail, elf_dynamic_find_value(dynamic, DT_RELAENT, &rela_ent));
  DLINK_ASSERT_OK(fail, elf_dynamic_find_value(dynamic, DT_RELACOUNT, &rela_count));

  if (rela_ent != 0x18) {
    return DLINK_ERR_INVALID_RELOC_ENT;
  }

  if (rela_size != rela_count * rela_ent) {
    return DLINK_ERR_INVALID_RELOC_TBL_SIZE;
  }

  Elf64_Rela *rela_base = (Elf64_Rela *)(module_base + rela_offset);
  for (uint64_t i = 0; i < rela_count; i++) {
    Elf64_Rela rela = rela_base[i];

    switch (rela.r_reloc_type) {
    case 0x403:
      if (rela.r_symbol != 0) {
        return DLINK_ERR_RELA_SYMBOL_UNSUP;
      }
      *(void **)(module_base + rela.r_offset) = module_base + rela.r_addend;
      break;
    default:
      return DLINK_ERR_UNRECOGNIZED_RELOC;
    }
  }

  return DLINK_OK;
fail:
  return r;
}

static Result dlink_run_relocation_table(dlink_module_t *mod, uint32_t offset_tag,
                                         uint32_t size_tag) {
  void *raw_table;
  Elf64_Dyn *dynamic = mod->dynamic;
  Result r = elf_dynamic_find_offset(dynamic, offset_tag, &raw_table, mod->input.base);
  if (r == DLINK_ERR_MISSING_DT_ENTRY) {
    return DLINK_OK;
  }
  if (r != DLINK_OK) {
    return r;
  }

  uint64_t table_size;
  DLINK_ASSERT_OK(fail, elf_dynamic_find_value(dynamic, size_tag, &table_size));

  uint64_t table_type = offset_tag;
  if (offset_tag == DT_JMPREL) {
    DLINK_ASSERT_OK(fail, elf_dynamic_find_value(dynamic, DT_PLTREL, &table_type));
  }

  uint64_t ent_size;
  switch (table_type) {
  case DT_RELA:
    r = elf_dynamic_find_value(dynamic, DT_RELAENT, &ent_size);
    if (r == DLINK_ERR_MISSING_DT_ENTRY) {
      ent_size = sizeof(Elf64_Rela);
    } else if (r == DLINK_OK && ent_size != sizeof(Elf64_Rela)) {
      return DLINK_ERR_INVALID_RELOC_ENT;
    } else if (r != DLINK_OK) {
      return r;
    }
    break;
  case DT_REL:
    r = elf_dynamic_find_value(dynamic, DT_RELENT, &ent_size);
    if (r == DLINK_ERR_MISSING_DT_ENTRY) {
      ent_size = sizeof(Elf64_Rel);
    } else if (r == DLINK_OK && ent_size != sizeof(Elf64_Rel)) {
      return DLINK_ERR_INVALID_RELOC_ENT;
    } else if (r != DLINK_OK) {
      return r;
    }
    break;
  default:
    return DLINK_ERR_INVALID_RELOC_TBL_TYPE;
  }

  if ((table_size % ent_size) != 0) {
    return DLINK_ERR_INVALID_RELOC_TBL_SIZE;
  }

  for (size_t offset = 0; offset < table_size; offset += ent_size) {
    Elf64_Rela rela = {0};
    switch (table_type) {
    case DT_RELA:
      rela = *(Elf64_Rela *)((uint8_t *)raw_table + offset);
      break;
    case DT_REL: {
      Elf64_Rel rel = *(Elf64_Rel *)((uint8_t *)raw_table + offset);
      rela.r_offset = rel.r_offset;
      rela.r_reloc_type = rel.r_reloc_type;
      rela.r_symbol = rel.r_symbol;
      break;
    }
    default:
      return DLINK_ERR_INVALID_RELOC_TBL_TYPE;
    }

    void *symbol = NULL;
    dlink_module_t *defining_module = mod;
    if (rela.r_symbol != 0) {
      if (mod->symtab == NULL) {
        return DLINK_ERR_NEEDS_SYMTAB;
      }
      if (mod->strtab == NULL) {
        return DLINK_ERR_NEEDS_STRTAB;
      }
      Elf64_Sym *sym = &mod->symtab[rela.r_symbol];

      Elf64_Sym *def;
      if ((r = dlink_resolve_load_symbol(mod, mod->strtab + sym->st_name, &def,
                                         &defining_module)) != DLINK_OK) {
        return r;
      }
      symbol = (uint8_t *)defining_module->input.base + def->st_value;
    }
    void *delta_symbol = defining_module->input.base;

    switch (rela.r_reloc_type) {
    case 257:

    case 1025:

    case 1026: {
      void **target = (void **)((uint8_t *)mod->input.base + rela.r_offset);
      if (table_type == DT_REL) {
        rela.r_addend = (uint64_t)*target;
      }
      *target = (uint8_t *)symbol + rela.r_addend;
      break;
    }
    case 1027: {
      if (!mod->input.has_run_basic_relocations) {
        void **target = (void **)((uint8_t *)mod->input.base + rela.r_offset);
        if (table_type == DT_REL) {
          rela.r_addend = (uint64_t)*target;
        }
        *target = (uint8_t *)delta_symbol + rela.r_addend;
      }
      break;
    }
    default:
      return DLINK_ERR_UNRECOGNIZED_RELOC;
    }
  }

  return DLINK_OK;
fail:
  return r;
}

static Result dlink_run_relr_table(dlink_module_t *mod) {
  void *raw_table;
  Result r = elf_dynamic_find_offset(mod->dynamic, DT_RELR, &raw_table, mod->input.base);
  if (r == DLINK_ERR_MISSING_DT_ENTRY) {
    return DLINK_OK;
  }
  if (r != DLINK_OK) {
    return r;
  }

  uint64_t table_size, ent_size;
  DLINK_ASSERT_OK(fail, elf_dynamic_find_value(mod->dynamic, DT_RELRSZ, &table_size));

  r = elf_dynamic_find_value(mod->dynamic, DT_RELRENT, &ent_size);
  if (r == DLINK_ERR_MISSING_DT_ENTRY) {
    ent_size = sizeof(uint64_t);
  } else if (r == DLINK_OK && ent_size != sizeof(uint64_t)) {
    return DLINK_ERR_INVALID_RELOC_ENT;
  } else if (r != DLINK_OK) {
    return r;
  }
  if (table_size % sizeof(uint64_t) != 0) {
    return DLINK_ERR_INVALID_RELOC_TBL_SIZE;
  }

  uint8_t *bias = mod->input.base;
  uint64_t *p = raw_table;
  uint64_t *end = (uint64_t *)((uint8_t *)raw_table + table_size);
  uint64_t base = 0;
  for (; p < end; p++) {
    uint64_t e = *p;
    if ((e & 1) == 0) {
      base = e;
      *(uint64_t *)(bias + base) += (uint64_t)bias;
    } else {
      for (int i = 1; i < 64; i++) {
        if (e & (1ULL << i)) {
          *(uint64_t *)(bias + base + (uint64_t)i * 8) += (uint64_t)bias;
        }
      }
      base += 64 * 8;
    }
  }

  return DLINK_OK;
fail:
  return r;
}

Result dlink_relocate_module(dlink_module_t *mod) {
  Result r = DLINK_OK;
  DLINK_ASSERT_OK(fail, dlink_run_relocation_table(mod, DT_RELA, DT_RELASZ));
  DLINK_ASSERT_OK(fail, dlink_run_relocation_table(mod, DT_REL, DT_RELSZ));
  DLINK_ASSERT_OK(fail, dlink_run_relocation_table(mod, DT_JMPREL, DT_PLTRELSZ));
  DLINK_ASSERT_OK(fail, dlink_run_relr_table(mod));
  mod->state = MODULE_STATE_RELOCATED;
fail:
  return r;
}
