//
//  Copyright (C) 2026  Nick Gasson
//
//  This program is free software: you can redistribute it and/or modify
//  it under the terms of the GNU General Public License as published by
//  the Free Software Foundation, either version 3 of the License, or
//  (at your option) any later version.
//
//  This program is distributed in the hope that it will be useful,
//  but WITHOUT ANY WARRANTY; without even the implied warranty of
//  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//  GNU General Public License for more details.
//
//  You should have received a copy of the GNU General Public License
//  along with this program.  If not, see <http://www.gnu.org/licenses/>.
//

#ifndef _JIT_CACHE_H
#define _JIT_CACHE_H

#include "prim.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define NVC_JIT_CACHE_ABI 2

typedef struct _jit_cache jit_cache_t;
typedef struct _jit_func jit_func_t;

typedef struct {
   uint8_t bytes[32];
} jit_cache_key_t;

typedef struct {
   uint32_t code_model;
   uint32_t reloc_mode;
   uint8_t  preserve_fp;
   uint8_t  debug_build;
   uint8_t  enable_dwarf;
   uint8_t  opaque_ptrs;
   uint32_t inline_limit;
} jit_cache_cfg_t;

jit_cache_t *jit_cache_new(const char *triple, const char *cpu,
                           const char *features, const char *build_fp,
                           const jit_cache_cfg_t *cfg);
void jit_cache_free(jit_cache_t *cache);

bool jit_cache_enabled(jit_cache_t *cache);
const char *jit_cache_dir(jit_cache_t *cache);

void jit_cache_prepare(jit_cache_t *cache, jit_func_t *f, bool huge,
                       jit_cache_key_t *key_out, void ***relocs_out,
                       unsigned *nrelocs_out, bool *uses_cpool_out);

bool jit_cache_lookup(jit_cache_t *cache, const jit_cache_key_t *key,
                      void **data_out, size_t *size_out);

void jit_cache_store(jit_cache_t *cache, const jit_cache_key_t *key,
                     const void *data, size_t size);

void jit_cache_clear(void);

void jit_cache_hex(const jit_cache_key_t *key, char out[65]);
void jit_cache_sha256(const void *data, size_t len, uint8_t out[32]);

#endif  // _JIT_CACHE_H
