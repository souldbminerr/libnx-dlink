#include "dlink_internal.h"

#include <switch/services/ro.h>

#include <stdlib.h>
#include <string.h>

#define DLINK_DNRO_NRR_SIZE 0x1000
#define DLINK_DNRO_NRR_HASH_OFFSET 0x350

typedef struct {
  void *nro_image;
  void *nrr;
  void *bss;
  size_t nro_size;
} dlink_dnro_data_t;

static Result dlink_dnro_image_size(void *file, size_t file_size, size_t *out_image_size) {
  if (file_size < sizeof(NroStart) + sizeof(NroHeader)) {
    return DLINK_ERR_MALFORMED_NRO;
  }
  if (memcmp((uint8_t *)file + DNRO_TAG_OFFSET, DNRO_TAG, DNRO_TAG_SIZE) != 0) {
    return DLINK_ERR_INVALID_MODULE_HEADER;
  }
  NroHeader *header = (NroHeader *)((uint8_t *)file + sizeof(NroStart));
  if (header->magic != NROHEADER_MAGIC) {
    return DLINK_ERR_MALFORMED_NRO;
  }
  if (header->size < sizeof(NroStart) + sizeof(NroHeader) || header->size > file_size) {
    return DLINK_ERR_MALFORMED_NRO;
  }
  *out_image_size = header->size;
  return DLINK_OK;
}

static Result dlink_dnro_can_load(void *file, size_t file_size) {
  size_t image_size = 0;
  return dlink_dnro_image_size(file, file_size, &image_size);
}

static Result dlink_dnro_load(module_input_t *spec_out, void *nro_image,
                              size_t nro_image_size_file) {
  Result r;
  size_t nro_image_size = 0;
  if ((r = dlink_dnro_image_size(nro_image, nro_image_size_file, &nro_image_size)) != DLINK_OK) {
    return r;
  }
  NroHeader *header = (NroHeader *)((uint8_t *)nro_image + sizeof(NroStart));
  uint32_t nro_bss_size = header->bss_size;

  dlink_dnro_data_t *loader_data = malloc(sizeof(*loader_data));
  if (loader_data == NULL) {
    return DLINK_ERR_OUT_OF_MEMORY;
  }
  loader_data->nro_image = NULL;
  loader_data->nrr = NULL;
  loader_data->bss = NULL;
  loader_data->nro_size = nro_image_size;

  uint32_t *nrr = dlink_alloc_pages(DLINK_DNRO_NRR_SIZE);
  if (nrr == NULL) {
    r = DLINK_ERR_OUT_OF_MEMORY;
    goto fail_loader_data;
  }
  memset(nrr, 0, DLINK_DNRO_NRR_SIZE);
  loader_data->nrr = nrr;
  nrr[0] = 0x3052524E;
  nrr[0x338 >> 2] = DLINK_DNRO_NRR_SIZE;
  nrr[(0x340 >> 2) + 0] = DLINK_DNRO_NRR_HASH_OFFSET;
  nrr[(0x340 >> 2) + 1] = 0x1;
  dlink_sha256(nro_image, nro_image_size, (uint8_t *)nrr + DLINK_DNRO_NRR_HASH_OFFSET);

  void *nro_bss = dlink_alloc_pages(nro_bss_size);
  if (nro_bss == NULL) {
    r = DLINK_ERR_OUT_OF_MEMORY;
    goto fail_nrr;
  }
  memset(nro_bss, 0, (nro_bss_size + 0xFFF) & ~(size_t)0xFFF);
  loader_data->bss = nro_bss;

  if (R_FAILED(r = ldrRoInitialize())) {
    goto fail_bss;
  }

  if (R_FAILED(r = ldrRoLoadNrr((u64)nrr, DLINK_DNRO_NRR_SIZE))) {
    goto fail_ro;
  }

  u64 nro_base = 0;
  r = ldrRoLoadNro(&nro_base, (u64)nro_image, nro_image_size, (u64)nro_bss, nro_bss_size);
  if (R_FAILED(r)) {
    goto fail_loaded_nrr;
  }

  ldrRoUnloadNrr((u64)nrr);
  dlink_free_pages(nrr);
  loader_data->nrr = NULL;

  loader_data->nro_image = nro_image;
  spec_out->base = (void *)nro_base;
  spec_out->loader = &dlink_loader_dnro;
  spec_out->loader_data = loader_data;
  return DLINK_OK;

fail_loaded_nrr:
  ldrRoUnloadNrr((u64)nrr);
fail_ro:
  ldrRoExit();
fail_bss:
  dlink_free_pages(nro_bss);
  loader_data->bss = NULL;
fail_nrr:
  dlink_free_pages(nrr);
  loader_data->nrr = NULL;
fail_loader_data:
  free(loader_data);
  return r;
}

static Result dlink_dnro_unload(module_input_t *spec) {
  dlink_dnro_data_t *loader_data = spec->loader_data;
  Result r = ldrRoUnloadNro((u64)spec->base);
  if (loader_data->nrr != NULL) {
    Result r2 = ldrRoUnloadNrr((u64)loader_data->nrr);
    if (R_SUCCEEDED(r)) {
      r = r2;
    }
    dlink_free_pages(loader_data->nrr);
    loader_data->nrr = NULL;
  }
  ldrRoExit();
  dlink_free_pages(loader_data->nro_image);
  dlink_free_pages(loader_data->bss);
  free(loader_data);
  return r;
}

dlink_loader_t dlink_loader_dnro = {
    .can_load = dlink_dnro_can_load,
    .load = dlink_dnro_load,
    .unload = dlink_dnro_unload,
};
