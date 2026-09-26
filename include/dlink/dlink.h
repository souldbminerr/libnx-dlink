/**
 * @file dlink.h
 * @brief Header libnx-dlink, a dynamic linker library for libnx.
 * @authors Souldbminer and ReSwitched
 * Licensed under The Unlicense
 */
#pragma once

#include <switch.h>

#ifdef __cplusplus
extern "C" {
#endif

/* libnx result module used by this library */
#define DLINK_MODULE 358
#define DLINK_MAKE_RESULT(code) MAKERESULT(DLINK_MODULE, (code))
#define DLINK_OK 0

#define DLINK_ERR_OUT_OF_MEMORY DLINK_MAKE_RESULT(3)
#define DLINK_ERR_INVALID_MODULE_HEADER DLINK_MAKE_RESULT(12001)
#define DLINK_ERR_DUPLICATE_DT_ENTRY DLINK_MAKE_RESULT(12002)
#define DLINK_ERR_MISSING_DT_ENTRY DLINK_MAKE_RESULT(12003)
#define DLINK_ERR_INVALID_RELOC_ENT DLINK_MAKE_RESULT(12004)
#define DLINK_ERR_INVALID_RELOC_TBL_SIZE DLINK_MAKE_RESULT(12005)
#define DLINK_ERR_RELA_SYMBOL_UNSUP DLINK_MAKE_RESULT(12006)
#define DLINK_ERR_UNRECOGNIZED_RELOC DLINK_MAKE_RESULT(12007)
#define DLINK_ERR_FAILED_TO_READ_MODULE DLINK_MAKE_RESULT(12008)
#define DLINK_ERR_FAILED_TO_FIND_MODULE DLINK_MAKE_RESULT(12009)
#define DLINK_ERR_INVALID_RELOC_TBL_TYPE DLINK_MAKE_RESULT(12011)
#define DLINK_ERR_INVALID_SYM_ENT DLINK_MAKE_RESULT(12012)
#define DLINK_ERR_NEEDS_STRTAB DLINK_MAKE_RESULT(12013)
#define DLINK_ERR_NEEDS_SYMTAB DLINK_MAKE_RESULT(12014)
#define DLINK_ERR_COULD_NOT_RESOLVE DLINK_MAKE_RESULT(12015)
#define DLINK_ERR_INVALID_MODULE_TYPE DLINK_MAKE_RESULT(12016)
#define DLINK_ERR_INVALID_MODULE_STATE DLINK_MAKE_RESULT(12017)
#define DLINK_ERR_NO_LOADER_FOR_MODULE DLINK_MAKE_RESULT(12018)
#define DLINK_ERR_MALFORMED_NRO DLINK_MAKE_RESULT(12019)

/* dlink_open flags */
#define DLINK_RTLD_LOCAL 0
#define DLINK_RTLD_GLOBAL 0x100

/**
 * @brief Loads a module and its dependencies, relocates and initializes them.
 * @param path Module name or path. Bare names are searched for in the
 *             configured search paths (see dlink_set_search_paths).
 *             A NULL path returns a handle to the global scope.
 * @param flags DLINK_RTLD_LOCAL or DLINK_RTLD_GLOBAL. Global modules provide
 *              symbols for subsequently loaded modules.
 * @returns Opaque handle, or NULL on failure (see dlink_error()).
 */
void *dlink_open(const char *path, int flags);

/**
 * @brief Resolves a symbol. A NULL handle (or one from dlink_open(NULL, ...))
 *        searches the global scope; otherwise the module's dependencies.
 */
void *dlink_symbol(void *handle, const char *symbol);

/**
 * @brief Decrements the module refcount, unloading when it reaches zero.
 *        Always succeeds; safe to call with NULL.
 */
int dlink_close(void *handle);

/**
 * @brief Human-readable description of the last failure on this thread.
 *        Returns NULL if there was no failure. Reading clears the error.
 */
const char *dlink_error(void);

/**
 * @brief Overrides the module search paths used for bare module names.
 *        The pointer array and strings are copied. Pass NULL/0 to restore
 *        defaults. Defaults:
 *          "sdmc:/lib/"
 */
void dlink_set_search_paths(const char *const *paths, size_t count);

/**
 * @brief Sets the single directory bare module names are searched in.
 *        Convenience wrapper for hosts with one library directory;
 *        equivalent to dlink_set_search_paths(&path, 1).
 * @param path Directory to search (a trailing slash is not required but
 *             recommended). Pass NULL to fall back to "sdmc:/lib/".
 */
void dlink_set_library_dir(const char *path);

/**
 * @brief Applies basic (RELATIVE-only) relocations to an already-mapped
 *        module image. Advanced use (e.g. custom loaders); dlink_open
 *        handles this internally for its own loaders.
 */
Result dlink_relocate_basic(void *module_base);

/* Unix dlfcn-style names. libnx-dlink is the dlfcn provider on Switch
 * (newlib ships no dlopen family for this target), so these are always
 * defined. Flags match glibc values; LAZY/NOW are accepted but loading
 * always binds immediately. */
#ifndef RTLD_LAZY
#define RTLD_LAZY 0x001
#endif
#ifndef RTLD_NOW
#define RTLD_NOW 0x002
#endif
#ifndef RTLD_LOCAL
#define RTLD_LOCAL 0
#endif
#ifndef RTLD_GLOBAL
#define RTLD_GLOBAL 0x100
#endif

/**
 * @brief Exposes one host symbol (libc, libnx, or game code) to modules.
 *        Stored in a synthetic global module that lives for the rest of the
 *        process; addresses must stay valid (pass function or static addresses).
 *        Needed when the host does not export a full dynamic symbol table.
 */
Result dlink_provide(const char *name, void *addr);

/**
 * @brief Exposes many host symbols at once. Same per-symbol semantics as
 *        dlink_provide(), but the lookup hash is rebuilt once for the whole
 *        batch instead of once per symbol: exposing thousands of symbols
 *        one by one costs O(n^2) rebuilds, this costs O(n). NULL entries
 *        are skipped. An empty batch is a no-op returning DLINK_OK.
 */
Result dlink_provide_bulk(const char **names, void **addrs, size_t count);

/* Shorthand: registers sym under its own name. Taking the address also
 * forces the object into the host link, so a later dlink_register_host()
 * harvest sees it too. This is the whole "manual" step: unlinked code has
 * no address to expose, so each module-only import must be named once,
 * here. Hosts that already call these functions need no provides at all. */
#define DLINK_PROVIDE(sym) dlink_provide(#sym, (void *)(sym))

/**
 * @brief Exposes the host's own dynamic symbols to modules, so modules can
 *        import libc/libnx (or anything else the host exports) instead of
 *        carrying static copies. Reads the host NRO's .dynamic/.dynsym, so
 *        the host must export what modules need (link with --export-dynamic
 *        or a --dynamic-list). Safe to call repeatedly; runs once.
 * @returns DLINK_OK, or NEEDS_SYMTAB/NEEDS_STRTAB/INVALID_MODULE_HEADER if
 *          the host image has no usable dynamic symbols.
 */
Result dlink_register_host(void);

void *dlopen(const char *path, int flags);
void *dlsym(void *handle, const char *symbol);
int dlclose(void *handle);
const char *dlerror(void);

#ifdef __cplusplus
}
#endif
