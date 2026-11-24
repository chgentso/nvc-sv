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

#include "util.h"
#include "array.h"
#include "jit/jit-cache.h"
#include "jit/jit-priv.h"
#include "option.h"
#include "rt/structs.h"
#include "thread.h"

#include <assert.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#ifdef __MINGW32__
#include <direct.h>
#include <io.h>
#include <windows.h>
#ifndef O_BINARY
#define O_BINARY _O_BINARY
#endif
#else
#ifndef O_BINARY
#define O_BINARY 0
#endif
#endif

#define JIT_CACHE_MAGIC     UINT32_C(0x4e56434a)   // "NVCJ"
#define JIT_CACHE_MAX_SIZE  (256u * 1024u * 1024u)
#define JIT_CACHE_STALE_SEC 3600

typedef struct {
   uint32_t magic;
   uint16_t abi_version;
   uint16_t header_size;
   uint8_t  key_hash[32];
   uint64_t payload_size;
   uint8_t  payload_hash[32];
} jit_cache_hdr_t;

STATIC_ASSERT(sizeof(jit_cache_hdr_t) == 80);

struct _jit_cache {
   nvc_lock_t      lock;
   char           *dir;
   char           *opt_dir_snap;
   bool            enabled;
   char           *triple;
   char           *cpu;
   char           *features;
   char           *build_fp;
   jit_cache_cfg_t cfg;
   uint64_t        writes;
   uint64_t        session_bytes;
   uint64_t        tmp_seq;
   int             evicting;
};

////////////////////////////////////////////////////////////////////////////////
// Self-contained SHA-256

typedef struct {
   uint32_t state[8];
   uint64_t bitlen;
   uint8_t  buf[64];
   size_t   buflen;
} sha256_t;

static inline uint32_t rotr32(uint32_t x, unsigned n)
{
   return (x >> n) | (x << (32 - n));
}

static void sha256_transform(sha256_t *ctx, const uint8_t block[64])
{
   static const uint32_t k[64] = {
      0x428a2f98u, 0x71374491u, 0xb5c0fbcfu, 0xe9b5dba5u,
      0x3956c25bu, 0x59f111f1u, 0x923f82a4u, 0xab1c5ed5u,
      0xd807aa98u, 0x12835b01u, 0x243185beu, 0x550c7dc3u,
      0x72be5d74u, 0x80deb1feu, 0x9bdc06a7u, 0xc19bf174u,
      0xe49b69c1u, 0xefbe4786u, 0x0fc19dc6u, 0x240ca1ccu,
      0x2de92c6fu, 0x4a7484aau, 0x5cb0a9dcu, 0x76f988dau,
      0x983e5152u, 0xa831c66du, 0xb00327c8u, 0xbf597fc7u,
      0xc6e00bf3u, 0xd5a79147u, 0x06ca6351u, 0x14292967u,
      0x27b70a85u, 0x2e1b2138u, 0x4d2c6dfcu, 0x53380d13u,
      0x650a7354u, 0x766a0abbu, 0x81c2c92eu, 0x92722c85u,
      0xa2bfe8a1u, 0xa81a664bu, 0xc24b8b70u, 0xc76c51a3u,
      0xd192e819u, 0xd6990624u, 0xf40e3585u, 0x106aa070u,
      0x19a4c116u, 0x1e376c08u, 0x2748774cu, 0x34b0bcb5u,
      0x391c0cb3u, 0x4ed8aa4au, 0x5b9cca4fu, 0x682e6ff3u,
      0x748f82eeu, 0x78a5636fu, 0x84c87814u, 0x8cc70208u,
      0x90befffau, 0xa4506cebu, 0xbef9a3f7u, 0xc67178f2u,
   };

   uint32_t w[64];
   for (int i = 0; i < 16; i++)
      w[i] = ((uint32_t)block[i * 4 + 0] << 24)
           | ((uint32_t)block[i * 4 + 1] << 16)
           | ((uint32_t)block[i * 4 + 2] << 8)
           | ((uint32_t)block[i * 4 + 3]);

   for (int i = 16; i < 64; i++) {
      const uint32_t s0 = rotr32(w[i - 15], 7) ^ rotr32(w[i - 15], 18)
         ^ (w[i - 15] >> 3);
      const uint32_t s1 = rotr32(w[i - 2], 17) ^ rotr32(w[i - 2], 19)
         ^ (w[i - 2] >> 10);
      w[i] = w[i - 16] + s0 + w[i - 7] + s1;
   }

   uint32_t a = ctx->state[0];
   uint32_t b = ctx->state[1];
   uint32_t c = ctx->state[2];
   uint32_t d = ctx->state[3];
   uint32_t e = ctx->state[4];
   uint32_t f = ctx->state[5];
   uint32_t g = ctx->state[6];
   uint32_t h = ctx->state[7];

   for (int i = 0; i < 64; i++) {
      const uint32_t S1 = rotr32(e, 6) ^ rotr32(e, 11) ^ rotr32(e, 25);
      const uint32_t ch = (e & f) ^ ((~e) & g);
      const uint32_t t1 = h + S1 + ch + k[i] + w[i];
      const uint32_t S0 = rotr32(a, 2) ^ rotr32(a, 13) ^ rotr32(a, 22);
      const uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
      const uint32_t t2 = S0 + maj;

      h = g;
      g = f;
      f = e;
      e = d + t1;
      d = c;
      c = b;
      b = a;
      a = t1 + t2;
   }

   ctx->state[0] += a;
   ctx->state[1] += b;
   ctx->state[2] += c;
   ctx->state[3] += d;
   ctx->state[4] += e;
   ctx->state[5] += f;
   ctx->state[6] += g;
   ctx->state[7] += h;
}

static void sha256_init(sha256_t *ctx)
{
   ctx->state[0] = 0x6a09e667u;
   ctx->state[1] = 0xbb67ae85u;
   ctx->state[2] = 0x3c6ef372u;
   ctx->state[3] = 0xa54ff53au;
   ctx->state[4] = 0x510e527fu;
   ctx->state[5] = 0x9b05688cu;
   ctx->state[6] = 0x1f83d9abu;
   ctx->state[7] = 0x5be0cd19u;
   ctx->bitlen   = 0;
   ctx->buflen   = 0;
}

static void sha256_update(sha256_t *ctx, const void *data, size_t len)
{
   const uint8_t *p = data;
   ctx->bitlen += (uint64_t)len * 8;

   if (ctx->buflen > 0) {
      const size_t take = MIN(len, sizeof(ctx->buf) - ctx->buflen);
      memcpy(ctx->buf + ctx->buflen, p, take);
      ctx->buflen += take;
      p += take;
      len -= take;

      if (ctx->buflen == sizeof(ctx->buf)) {
         sha256_transform(ctx, ctx->buf);
         ctx->buflen = 0;
      }
   }

   while (len >= sizeof(ctx->buf)) {
      sha256_transform(ctx, p);
      p += sizeof(ctx->buf);
      len -= sizeof(ctx->buf);
   }

   if (len > 0) {
      memcpy(ctx->buf, p, len);
      ctx->buflen = len;
   }
}

static void sha256_final(sha256_t *ctx, uint8_t out[32])
{
   ctx->buf[ctx->buflen++] = 0x80;
   if (ctx->buflen > 56) {
      memset(ctx->buf + ctx->buflen, 0, sizeof(ctx->buf) - ctx->buflen);
      sha256_transform(ctx, ctx->buf);
      ctx->buflen = 0;
   }

   memset(ctx->buf + ctx->buflen, 0, 56 - ctx->buflen);
   for (int i = 0; i < 8; i++)
      ctx->buf[56 + i] = (uint8_t)(ctx->bitlen >> ((7 - i) * 8));

   sha256_transform(ctx, ctx->buf);

   for (int i = 0; i < 8; i++) {
      out[i * 4 + 0] = (uint8_t)(ctx->state[i] >> 24);
      out[i * 4 + 1] = (uint8_t)(ctx->state[i] >> 16);
      out[i * 4 + 2] = (uint8_t)(ctx->state[i] >> 8);
      out[i * 4 + 3] = (uint8_t)(ctx->state[i]);
   }
}

void jit_cache_sha256(const void *data, size_t len, uint8_t out[32])
{
   sha256_t ctx;
   sha256_init(&ctx);
   sha256_update(&ctx, data, len);
   sha256_final(&ctx, out);
}

void jit_cache_hex(const jit_cache_key_t *key, char out[65])
{
   static const char hex[] = "0123456789abcdef";
   for (int i = 0; i < 32; i++) {
      out[i * 2 + 0] = hex[(key->bytes[i] >> 4) & 0xf];
      out[i * 2 + 1] = hex[key->bytes[i] & 0xf];
   }
   out[64] = '\0';
}

static inline void hash_u8(sha256_t *ctx, uint8_t v)
{
   sha256_update(ctx, &v, sizeof(v));
}

static inline void hash_u32(sha256_t *ctx, uint32_t v)
{
   sha256_update(ctx, &v, sizeof(v));
}

static inline void hash_u64(sha256_t *ctx, uint64_t v)
{
   sha256_update(ctx, &v, sizeof(v));
}

static inline void hash_str(sha256_t *ctx, const char *s)
{
   const uint32_t len = s != NULL ? (uint32_t)strlen(s) : 0;
   hash_u32(ctx, len);
   if (len > 0)
      sha256_update(ctx, s, len);
}

////////////////////////////////////////////////////////////////////////////////
// Filesystem helpers

static bool cache_is_dir(const char *path)
{
   struct stat st;
   return stat(path, &st) == 0 && S_ISDIR(st.st_mode);
}

static bool cache_mkdir_one(const char *path)
{
#ifdef __MINGW32__
   if (_mkdir(path) == 0)
      return true;
#else
   if (mkdir(path, 0755) == 0)
      return true;
#endif
   return errno == EEXIST && cache_is_dir(path);
}

static bool cache_mkdir_p(const char *path)
{
   if (path == NULL || *path == '\0')
      return false;

   if (cache_is_dir(path))
      return true;

   char *tmp LOCAL = xstrdup(path);
   const size_t len = strlen(tmp);
   if (len > 1 && (tmp[len - 1] == '/' || tmp[len - 1] == '\\'))
      tmp[len - 1] = '\0';

   for (char *p = tmp + 1; *p != '\0'; p++) {
      if (*p == '/' || *p == '\\') {
#ifdef __MINGW32__
         if (p - tmp == 2 && tmp[1] == ':')
            continue;
#endif
         const char saved = *p;
         *p = '\0';
         if (!cache_mkdir_one(tmp))
            return false;
         *p = saved;
      }
   }

   return cache_mkdir_one(tmp);
}

static bool cache_dir_writable(const char *path)
{
   if (!cache_mkdir_p(path))
      return false;

#ifdef __MINGW32__
   return _access(path, 06) == 0;
#else
   return access(path, W_OK | X_OK) == 0;
#endif
}

static char *resolve_cache_dir(void)
{
   const char *opt_dir = opt_get_str(OPT_JIT_CACHE_DIR);
   if (opt_dir != NULL && *opt_dir != '\0')
      return xstrdup(opt_dir);

   const char *env_dir = getenv("NVC_JIT_CACHE_DIR");
   if (env_dir != NULL && *env_dir != '\0')
      return xstrdup(env_dir);

   const char *xdg = getenv("XDG_CACHE_HOME");
   if (xdg != NULL && *xdg != '\0')
      return xasprintf("%s/nvc-sv/jit", xdg);

#ifdef __MINGW32__
   const char *local_app = getenv("LOCALAPPDATA");
   if (local_app != NULL && *local_app != '\0')
      return xasprintf("%s\\nvc-sv\\jit", local_app);

   const char *profile = getenv("USERPROFILE");
   if (profile != NULL && *profile != '\0')
      return xasprintf("%s\\.cache\\nvc-sv\\jit", profile);
#endif

   const char *home = getenv("HOME");
   if (home != NULL && *home != '\0')
      return xasprintf("%s/.cache/nvc-sv/jit", home);

   return NULL;
}

static bool write_all(int fd, const void *buf, size_t len)
{
   const uint8_t *p = buf;
   while (len > 0) {
      ssize_t n = write(fd, p, len);
      if (n < 0) {
         if (errno == EINTR)
            continue;
         return false;
      }
      p += n;
      len -= (size_t)n;
   }
   return true;
}

static bool read_all(int fd, void *buf, size_t len)
{
   uint8_t *p = buf;
   while (len > 0) {
      ssize_t n = read(fd, p, len);
      if (n < 0) {
         if (errno == EINTR)
            continue;
         return false;
      }
      if (n == 0)
         return false;
      p += n;
      len -= (size_t)n;
   }
   return true;
}

static int cache_rename(const char *src, const char *dst)
{
#ifdef __MINGW32__
   if (MoveFileExA(src, dst, MOVEFILE_REPLACE_EXISTING))
      return 0;
   errno = EIO;
   return -1;
#else
   return rename(src, dst);
#endif
}

////////////////////////////////////////////////////////////////////////////////
// Cache lifecycle & configuration

static void refresh_cache_dir(jit_cache_t *cache)
{
   const char *opt_dir = opt_get_str(OPT_JIT_CACHE_DIR);
   if (cache->opt_dir_snap == NULL && opt_dir == NULL)
      return;
   if (cache->opt_dir_snap != NULL && opt_dir != NULL
       && strcmp(cache->opt_dir_snap, opt_dir) == 0)
      return;

   SCOPED_LOCK(cache->lock);

   free(cache->opt_dir_snap);
   cache->opt_dir_snap = opt_dir ? xstrdup(opt_dir) : NULL;

   free(cache->dir);
   cache->dir = resolve_cache_dir();
   cache->enabled = (cache->dir != NULL) && cache_dir_writable(cache->dir);
}

jit_cache_t *jit_cache_new(const char *triple, const char *cpu,
                           const char *features, const char *build_fp,
                           const jit_cache_cfg_t *cfg)
{
   jit_cache_t *cache = xcalloc(sizeof(jit_cache_t));
   cache->triple   = xstrdup(triple ?: "");
   cache->cpu      = xstrdup(cpu ?: "");
   cache->features = xstrdup(features ?: "");
   cache->build_fp = xstrdup(build_fp ?: "");
   if (cfg != NULL)
      cache->cfg = *cfg;

   const char *opt_dir = opt_get_str(OPT_JIT_CACHE_DIR);
   cache->opt_dir_snap = opt_dir ? xstrdup(opt_dir) : NULL;
   cache->dir = resolve_cache_dir();
   if (cache->dir != NULL && opt_get_int(OPT_JIT_CACHE))
      cache->enabled = cache_dir_writable(cache->dir);

   return cache;
}

void jit_cache_free(jit_cache_t *cache)
{
   if (cache == NULL)
      return;

   free(cache->dir);
   free(cache->opt_dir_snap);
   free(cache->triple);
   free(cache->cpu);
   free(cache->features);
   free(cache->build_fp);
   free(cache);
}

bool jit_cache_enabled(jit_cache_t *cache)
{
   if (cache == NULL || !opt_get_int(OPT_JIT_CACHE))
      return false;

   refresh_cache_dir(cache);
   return relaxed_load(&cache->enabled);
}

const char *jit_cache_dir(jit_cache_t *cache)
{
   if (cache == NULL)
      return NULL;

   refresh_cache_dir(cache);
   return cache->dir;
}

////////////////////////////////////////////////////////////////////////////////
// Relocations & 256-bit key computation

static inline void hash_value(sha256_t *ctx, jit_value_t val,
                              bool is_call, bool is_getpriv,
                              unsigned *slot_counter, bool enable_dwarf,
                              uint32_t inline_limit)
{
   hash_u8(ctx, (uint8_t)val.kind);

   switch (val.kind) {
   case JIT_VALUE_INVALID:
      break;
   case JIT_VALUE_REG:
      hash_u32(ctx, val.reg);
      break;
   case JIT_VALUE_INT64:
   case JIT_ADDR_CPOOL:
      hash_u64(ctx, (uint64_t)val.int64);
      break;
   case JIT_VALUE_DOUBLE:
      {
         uint64_t bits = 0;
         memcpy(&bits, &val.dval, sizeof(bits));
         hash_u64(ctx, bits);
      }
      break;
   case JIT_ADDR_REG:
      hash_u32(ctx, val.reg);
      hash_u32(ctx, (uint32_t)val.disp);
      break;
   case JIT_ADDR_ABS:
      {
         const uint8_t is_null = (val.int64 == 0) ? 1 : 0;
         hash_u8(ctx, is_null);
         if (!is_null)
            hash_u32(ctx, (*slot_counter)++);
      }
      break;
   case JIT_VALUE_LABEL:
      hash_u32(ctx, val.label);
      break;
   case JIT_VALUE_HANDLE:
      hash_u32(ctx, (*slot_counter)++);
      if (is_call) {
         // INLINE_LIMIT == 0 never inlines; included explicitly for safety.
         hash_u8(ctx, (uint8_t)(inline_limit > 0 ? 1 : 0));
      }
      else if (is_getpriv) {
         hash_u8(ctx, 1);
      }
      break;
   case JIT_VALUE_EXIT:
      hash_u32(ctx, (uint32_t)val.exit);
      break;
   case JIT_VALUE_LOC:
      if (enable_dwarf) {
         hash_u32(ctx, val.loc.first_line);
         hash_u32(ctx, val.loc.first_column);
         hash_u32(ctx, val.loc.line_delta);
         hash_u32(ctx, val.loc.column_delta);
         hash_u32(ctx, val.loc.file_ref);
      }
      break;
   case JIT_VALUE_VPOS:
      hash_u32(ctx, val.vpos.block);
      hash_u32(ctx, val.vpos.op);
      break;
   case JIT_VALUE_LOCUS:
      hash_u32(ctx, (*slot_counter)++);
      break;
   }
}

void jit_cache_prepare(jit_cache_t *cache, jit_func_t *f, bool huge,
                       jit_cache_key_t *key_out, void ***relocs_out,
                       unsigned *nrelocs_out, bool *uses_cpool_out)
{
   const bool compute_key = (key_out != NULL) && jit_cache_enabled(cache);

   sha256_t ctx;
   if (compute_key) {
      sha256_init(&ctx);

      static const char magic[] = "NVC_JIT_CACHE_V1";
      sha256_update(&ctx, magic, sizeof(magic) - 1);
      hash_u32(&ctx, NVC_JIT_CACHE_ABI);
      hash_str(&ctx, cache->build_fp);

      // ABI struct layout constants used by emitted IR
      hash_u64(&ctx, sizeof(jit_func_t));
      hash_u64(&ctx, offsetof(jit_func_t, entry));
      hash_u64(&ctx, offsetof(jit_func_t, relocs));
      hash_u64(&ctx, offsetof(jit_func_t, cpool));
      hash_u64(&ctx, sizeof(jit_anchor_t));
      hash_u64(&ctx, offsetof(jit_anchor_t, caller));
      hash_u64(&ctx, offsetof(jit_anchor_t, func));
      hash_u64(&ctx, offsetof(jit_anchor_t, irpos));
      hash_u64(&ctx, offsetof(jit_anchor_t, watermark));
      hash_u64(&ctx, sizeof(tlab_t));
      hash_u64(&ctx, offsetof(tlab_t, alloc));
      hash_u64(&ctx, offsetof(tlab_t, limit));
      hash_u64(&ctx, offsetof(tlab_t, mem_dirty));
      hash_u64(&ctx, offsetof(tlab_t, data));
      hash_u64(&ctx, sizeof(sig_shared_t));
      hash_u64(&ctx, sizeof(jit_ir_t));
      hash_u64(&ctx, sizeof(jit_value_t));
      hash_u64(&ctx, RT_ALIGN_MASK);

      // Target & codegen configuration
      hash_str(&ctx, cache->triple);
      hash_str(&ctx, cache->cpu);
      hash_str(&ctx, cache->features);
      hash_u32(&ctx, cache->cfg.code_model);
      hash_u32(&ctx, cache->cfg.reloc_mode);
      hash_u8(&ctx, cache->cfg.preserve_fp);
      hash_u8(&ctx, cache->cfg.debug_build);
      hash_u8(&ctx, cache->cfg.enable_dwarf);
      hash_u8(&ctx, cache->cfg.opaque_ptrs);
      hash_u32(&ctx, cache->cfg.inline_limit);
      hash_u8(&ctx, huge ? 1 : 0);
      hash_u8(&ctx, (uint8_t)opt_get_int(OPT_TWO_STATE));

      // Canonical function metadata & constant pool
      hash_u32(&ctx, f->nirs);
      hash_u32(&ctx, f->nregs);
      hash_u32(&ctx, f->framesz);
      hash_u32(&ctx, f->cpoolsz);
      if (f->cpoolsz > 0)
         sha256_update(&ctx, f->cpool, f->cpoolsz);
   }

   A(void *) relocs = AINIT;
   bool uses_cpool = false;
   unsigned slot_counter = 0;
   const bool enable_dwarf = cache ? (cache->cfg.enable_dwarf != 0) : false;
   const uint32_t inline_limit = cache ? cache->cfg.inline_limit : 0;

   for (unsigned i = 0; i < f->nirs; i++) {
      const jit_ir_t *ir = &(f->irbuf[i]);

      if (compute_key) {
         hash_u8(&ctx, (uint8_t)ir->op);
         hash_u8(&ctx, (uint8_t)ir->size);
         hash_u8(&ctx, (uint8_t)ir->target);
         hash_u8(&ctx, (uint8_t)ir->cc);
         hash_u32(&ctx, ir->result);
         hash_value(&ctx, ir->arg1, ir->op == J_CALL,
                    ir->op == MACRO_GETPRIV, &slot_counter,
                    enable_dwarf, inline_limit);
         hash_value(&ctx, ir->arg2, false, false, &slot_counter,
                    enable_dwarf, inline_limit);
      }

      if (ir->op == J_CALL) {
         assert(ir->arg1.kind == JIT_VALUE_HANDLE);
         jit_func_t *callee = jit_get_func(f->jit, ir->arg1.handle);
         APUSH(relocs, callee);
      }
      else if (ir->op == MACRO_GETPRIV) {
         assert(ir->arg1.kind == JIT_VALUE_HANDLE);
         jit_func_t *target = jit_get_func(f->jit, ir->arg1.handle);
         APUSH(relocs, jit_get_privdata_ptr(target->jit, target));
      }
      else {
         const jit_value_t args[2] = { ir->arg1, ir->arg2 };
         for (int a = 0; a < 2; a++) {
            switch (args[a].kind) {
            case JIT_ADDR_CPOOL:
               uses_cpool = true;
               break;
            case JIT_ADDR_ABS:
               if (args[a].int64 != 0)
                  APUSH(relocs, (void *)(intptr_t)args[a].int64);
               break;
            case JIT_VALUE_LOCUS:
               APUSH(relocs, args[a].locus);
               break;
            case JIT_VALUE_HANDLE:
               APUSH(relocs, (void *)(uintptr_t)args[a].handle);
               break;
            default:
               break;
            }
         }
      }
   }

   assert(!compute_key || slot_counter == relocs.count);

   if (compute_key)
      sha256_final(&ctx, key_out->bytes);

   *nrelocs_out = relocs.count;
   *relocs_out = relocs.items;
   if (uses_cpool_out != NULL)
      *uses_cpool_out = uses_cpool;
}

////////////////////////////////////////////////////////////////////////////////
// Lookup, atomic store, eviction, and clear

typedef struct {
   char     *path;
   size_t    size;
   uint64_t  mtime_ns;
} cache_entry_t;

static uint64_t stat_mtime_ns(const struct stat *st)
{
#ifdef HAVE_STRUCT_STAT_ST_MTIM_TV_NSEC
   return (uint64_t)st->st_mtim.tv_sec * UINT64_C(1000000000)
      + (uint64_t)st->st_mtim.tv_nsec;
#elif defined HAVE_STRUCT_STAT_ST_MTIMESPEC_TV_NSEC
   return (uint64_t)st->st_mtimespec.tv_sec * UINT64_C(1000000000)
      + (uint64_t)st->st_mtimespec.tv_nsec;
#else
   return (uint64_t)st->st_mtime * UINT64_C(1000000000);
#endif
}

static int cmp_cache_entry_mtime(const void *a, const void *b)
{
   const cache_entry_t *ea = a;
   const cache_entry_t *eb = b;
   if (ea->mtime_ns < eb->mtime_ns)
      return -1;
   if (ea->mtime_ns > eb->mtime_ns)
      return 1;
   return 0;
}

static void cache_evict_if_needed(jit_cache_t *cache)
{
   const size_t limit = opt_get_size(OPT_JIT_CACHE_LIMIT);
   if (limit == 0 || cache->dir == NULL)
      return;

   if (!atomic_cas(&cache->evicting, 0, 1))
      return;

   const time_t now = time(NULL);
   DIR *root = opendir(cache->dir);
   if (root == NULL) {
      atomic_store(&cache->evicting, 0);
      return;
   }

   A(cache_entry_t) entries = AINIT;
   size_t total = 0;

   struct dirent *de;
   while ((de = readdir(root)) != NULL) {
      if (strlen(de->d_name) != 2)
         continue;

      char *subdir LOCAL = xasprintf("%s/%s", cache->dir, de->d_name);
      DIR *sd = opendir(subdir);
      if (sd == NULL)
         continue;

      struct dirent *fe;
      while ((fe = readdir(sd)) != NULL) {
         if (fe->d_name[0] == '.') {
            if (strncmp(fe->d_name, ".tmp.", 5) == 0) {
               char *tpath LOCAL = xasprintf("%s/%s", subdir, fe->d_name);
               struct stat tst;
               if (stat(tpath, &tst) == 0
                   && now - tst.st_mtime > JIT_CACHE_STALE_SEC)
                  (void)unlink(tpath);
            }
            continue;
         }

         const size_t nlen = strlen(fe->d_name);
         if (nlen < 3 || strcmp(fe->d_name + nlen - 2, ".o") != 0)
            continue;

         char *fpath = xasprintf("%s/%s", subdir, fe->d_name);
         struct stat st;
         if (stat(fpath, &st) == 0 && S_ISREG(st.st_mode)) {
            cache_entry_t ent = {
               .path     = fpath,
               .size     = (size_t)st.st_size,
               .mtime_ns = stat_mtime_ns(&st),
            };
            total += ent.size;
            APUSH(entries, ent);
         }
         else
            free(fpath);
      }

      closedir(sd);
   }

   closedir(root);

   if (total > limit && entries.count > 0) {
      qsort(entries.items, entries.count, sizeof(cache_entry_t),
            cmp_cache_entry_mtime);

      const size_t target = (limit * 3) / 4;
      for (unsigned i = 0; i < entries.count && total > target; i++) {
         if (unlink(entries.items[i].path) == 0)
            total -= entries.items[i].size;
      }
   }

   for (unsigned i = 0; i < entries.count; i++)
      free(entries.items[i].path);
   ACLEAR(entries);

   {
      SCOPED_LOCK(cache->lock);
      cache->session_bytes = 0;
   }

   atomic_store(&cache->evicting, 0);
}

bool jit_cache_lookup(jit_cache_t *cache, const jit_cache_key_t *key,
                      void **data_out, size_t *size_out)
{
   *data_out = NULL;
   *size_out = 0;

   if (!jit_cache_enabled(cache))
      return false;

   char hex[65];
   jit_cache_hex(key, hex);

   char *path LOCAL = xasprintf("%s/%c%c/%s.o", cache->dir, hex[0], hex[1], hex);
   int fd = open(path, O_RDONLY | O_BINARY);
   if (fd < 0)
      return false;

   struct stat st;
   if (fstat(fd, &st) != 0 || !S_ISREG(st.st_mode)
       || st.st_size <= (off_t)sizeof(jit_cache_hdr_t)
       || (uint64_t)st.st_size > sizeof(jit_cache_hdr_t) + JIT_CACHE_MAX_SIZE) {
      close(fd);
      (void)unlink(path);
      return false;
   }

   jit_cache_hdr_t hdr;
   if (!read_all(fd, &hdr, sizeof(hdr))) {
      close(fd);
      (void)unlink(path);
      return false;
   }

   const uint64_t fsize = (uint64_t)st.st_size;
   if (hdr.magic != JIT_CACHE_MAGIC
       || hdr.abi_version != NVC_JIT_CACHE_ABI
       || hdr.header_size != sizeof(jit_cache_hdr_t)
       || hdr.payload_size == 0
       || hdr.payload_size != fsize - sizeof(jit_cache_hdr_t)
       || memcmp(hdr.key_hash, key->bytes, 32) != 0) {
      close(fd);
      (void)unlink(path);
      return false;
   }

   void *buf = xmalloc((size_t)hdr.payload_size);
   if (!read_all(fd, buf, (size_t)hdr.payload_size)) {
      free(buf);
      close(fd);
      (void)unlink(path);
      return false;
   }

   close(fd);

   uint8_t digest[32];
   jit_cache_sha256(buf, (size_t)hdr.payload_size, digest);
   if (memcmp(digest, hdr.payload_hash, 32) != 0) {
      free(buf);
      (void)unlink(path);
      return false;
   }

#ifndef __MINGW32__
   const time_t now = time(NULL);
   if (now > st.st_mtime && (now - st.st_mtime) > 60)
      (void)utimensat(AT_FDCWD, path, NULL, 0);
#endif

   *data_out = buf;
   *size_out = (size_t)hdr.payload_size;
   return true;
}

void jit_cache_store(jit_cache_t *cache, const jit_cache_key_t *key,
                     const void *data, size_t size)
{
   if (!jit_cache_enabled(cache) || data == NULL || size == 0
       || size > JIT_CACHE_MAX_SIZE)
      return;

   char hex[65];
   jit_cache_hex(key, hex);

   char *subdir LOCAL = xasprintf("%s/%c%c", cache->dir, hex[0], hex[1]);
   if (!cache_mkdir_p(subdir)) {
      if (errno == EACCES || errno == EROFS)
         relaxed_store(&cache->enabled, false);
      return;
   }

   const uint64_t seq = relaxed_fetch_add(&cache->tmp_seq, 1);
   char *tmp LOCAL = xasprintf("%s/.tmp.%s.%lu.%d.%" PRIu64, subdir, hex,
                               (unsigned long)getpid(), thread_id(), seq);
   char *dst LOCAL = xasprintf("%s/%s.o", subdir, hex);

   int fd = open(tmp, O_WRONLY | O_CREAT | O_EXCL | O_BINARY, 0644);
   if (fd < 0) {
      if (errno == EACCES || errno == EROFS)
         relaxed_store(&cache->enabled, false);
      return;
   }

   jit_cache_hdr_t hdr = {
      .magic        = JIT_CACHE_MAGIC,
      .abi_version  = NVC_JIT_CACHE_ABI,
      .header_size  = sizeof(jit_cache_hdr_t),
      .payload_size = size,
   };
   memcpy(hdr.key_hash, key->bytes, 32);
   jit_cache_sha256(data, size, hdr.payload_hash);

   const bool ok = write_all(fd, &hdr, sizeof(hdr)) && write_all(fd, data, size);
   if (close(fd) != 0 || !ok || cache_rename(tmp, dst) != 0) {
      (void)unlink(tmp);
      return;
   }

   bool need_evict = false;
   {
      SCOPED_LOCK(cache->lock);
      cache->writes++;
      cache->session_bytes += sizeof(hdr) + size;
      const size_t limit = opt_get_size(OPT_JIT_CACHE_LIMIT);
      if (limit > 0) {
         if (cache->session_bytes > limit
             || (cache->writes >= 16 && ((cache->writes - 16) % 64) == 0))
            need_evict = true;
      }
   }

   if (need_evict)
      cache_evict_if_needed(cache);
}

void jit_cache_clear(void)
{
   char *dir LOCAL = resolve_cache_dir();
   if (dir == NULL)
      return;

   DIR *root = opendir(dir);
   if (root == NULL)
      return;

   struct dirent *de;
   while ((de = readdir(root)) != NULL) {
      if (strlen(de->d_name) != 2)
         continue;

      char *subdir LOCAL = xasprintf("%s/%s", dir, de->d_name);
      DIR *sd = opendir(subdir);
      if (sd == NULL)
         continue;

      struct dirent *fe;
      while ((fe = readdir(sd)) != NULL) {
         if (strcmp(fe->d_name, ".") == 0 || strcmp(fe->d_name, "..") == 0)
            continue;

         const size_t nlen = strlen(fe->d_name);
         if ((nlen >= 3 && strcmp(fe->d_name + nlen - 2, ".o") == 0)
             || strncmp(fe->d_name, ".tmp.", 5) == 0) {
            char *fpath LOCAL = xasprintf("%s/%s", subdir, fe->d_name);
            (void)unlink(fpath);
         }
      }

      closedir(sd);
   }

   closedir(root);
}
