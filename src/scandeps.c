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
#include "common.h"
#include "diag.h"
#include "ident.h"
#include "lib.h"
#include "phase.h"
#include "scan.h"
#include "scandeps.h"
#include "vlog/vlog-util.h"
#include "tree.h"
#include "vlog/vlog-node.h"
#include "vlog/vlog-phase.h"

#include <stdlib.h>
#include <string.h>
#include <limits.h>

typedef struct {
   text_buf_t *tb;
   text_buf_t *inst;
   ident_t    *seen;
   unsigned    nseen, maxseen;
} scan_ctx_t;

static void scan_json_string(text_buf_t *tb, const char *str)
{
   tb_append(tb, '"');
   for (const unsigned char *p = (const unsigned char *)str; *p; p++) {
      switch (*p) {
      case '"':  tb_cat(tb, "\\\""); break;
      case '\\': tb_cat(tb, "\\\\"); break;
      case '\n': tb_cat(tb, "\\n"); break;
      case '\r': tb_cat(tb, "\\r"); break;
      case '\t': tb_cat(tb, "\\t"); break;
      default:
         if (*p < 0x20)
            tb_printf(tb, "\\u%04x", *p);
         else
            tb_append(tb, *p);
      }
   }
   tb_append(tb, '"');
}

static void scan_json_list_item(text_buf_t *tb, unsigned *count,
                                const char *str)
{
   if ((*count)++ > 0)
      tb_append(tb, ',');
   scan_json_string(tb, str);
}

static ident_t scan_unit_name(ident_t name)
{
   // Remove an architecture or package body suffix
   if (strchr(istr(name), '\\') == NULL) {
      ident_t base = ident_runtil(name, '-');
      if (base != name)
         return base;
   }

   return name;
}

static ident_t scan_resolve_work(ident_t name)
{
   // The name WORK is an alias for the working library in an instantiation
   ident_t lib = ident_until(name, '.');
   if (lib != name && ident_casecmp(lib, well_known(W_WORK)))
      return ident_prefix(lib_name(lib_work()), ident_from(name, '.'), '.');

   return name;
}

static void scan_instance_cb(tree_t inst, void *context)
{
   scan_ctx_t *ctx = context;

   ident_t name = NULL;
   switch (tree_class(inst)) {
   case C_COMPONENT:
   case C_ENTITY:
   case C_CONFIGURATION:
      if (tree_has_ident2(inst))
         name = scan_resolve_work(scan_unit_name(tree_ident2(inst)));
      break;
   default:
      break;
   }

   if (name == NULL)
      return;

   if (tree_class(inst) != C_COMPONENT)
      lib_scan_note(name);   // Checked when the unit is analysed

   for (unsigned i = 0; i < ctx->nseen; i++) {
      if (ctx->seen[i] == name)
         return;
   }

   if (ctx->nseen == ctx->maxseen) {
      ctx->maxseen = MAX(8, ctx->maxseen * 2);
      ctx->seen = xrealloc_array(ctx->seen, ctx->maxseen, sizeof(ident_t));
   }

   ctx->seen[ctx->nseen++] = name;

   if (ctx->nseen > 1)
      tb_append(ctx->inst, ',');
   scan_json_string(ctx->inst, istr(name));
}

static void scan_vhdl_unit(tree_t unit, text_buf_t *units, unsigned *nunits,
                           ident_t **provided, unsigned *nprovided)
{
   const char *kind = NULL;
   bool has_ports = false;
   ident_t name = tree_ident(unit);

   switch (tree_kind(unit)) {
   case T_ENTITY:
      kind = "entity";
      has_ports = tree_ports(unit) > 0;
      break;
   case T_ARCH:
      kind = "architecture";
      lib_scan_note(scan_unit_name(name));
      break;
   case T_PACKAGE:
   case T_PACK_INST:
      kind = "package";
      break;
   case T_PACK_BODY:
      kind = "package-body";
      lib_scan_note(scan_unit_name(name));
      break;
   case T_CONFIGURATION:
      kind = "configuration";
      if (tree_has_ident2(unit)) {
         ident_t entity = scan_unit_name(tree_ident2(unit));
         if (ident_runtil(entity, '.') == entity)
            entity = ident_prefix(ident_until(name, '.'), entity, '.');
         lib_scan_note(entity);
      }
      break;
   case T_CONTEXT:
      kind = "context";
      break;
   case T_VUNIT:
      kind = "vunit";
      break;
   default:
      return;
   }

   scan_ctx_t ctx = { .tb = units, .inst = tb_new() };
   tree_visit_only(unit, scan_instance_cb, &ctx, T_INSTANCE);

   if ((*nunits)++ > 0)
      tb_append(units, ',');

   tb_cat(units, "{\"name\":");
   scan_json_string(units, istr(name));
   tb_printf(units, ",\"kind\":\"%s\"", kind);
   if (tree_kind(unit) == T_ENTITY)
      tb_printf(units, ",\"ports\":%s", has_ports ? "true" : "false");
   tb_printf(units, ",\"instantiates\":[%s]}", tb_get(ctx.inst));

   tb_free(ctx.inst);
   free(ctx.seen);

   // Units which are not primary (and so cannot be required) are not
   // recorded as provided
   ident_t prov = NULL;
   switch (tree_kind(unit)) {
   case T_ENTITY:
   case T_PACKAGE:
   case T_PACK_INST:
   case T_CONFIGURATION:
   case T_CONTEXT:
   case T_VUNIT:
      prov = name;
      break;
   default:
      break;
   }

   if (prov != NULL) {
      *provided = xrealloc_array(*provided, *nprovided + 1, sizeof(ident_t));
      (*provided)[(*nprovided)++] = prov;
   }
}

static void scan_inst_list_cb(vlog_node_t v, void *context)
{
   scan_ctx_t *ctx = context;

   if (vlog_stmts(v) == 0 || !vlog_has_ident(v))
      return;   // Not an instantiation

   ident_t name = vlog_ident(v);

   for (unsigned i = 0; i < ctx->nseen; i++) {
      if (ctx->seen[i] == name)
         return;
   }

   if (ctx->nseen == ctx->maxseen) {
      ctx->maxseen = MAX(8, ctx->maxseen * 2);
      ctx->seen = xrealloc_array(ctx->seen, ctx->maxseen, sizeof(ident_t));
   }

   ctx->seen[ctx->nseen++] = name;

   if (ctx->nseen > 1)
      tb_append(ctx->inst, ',');
   scan_json_string(ctx->inst, istr(name));
}

static void scan_vlog_unit(vlog_node_t unit, text_buf_t *units,
                           unsigned *nunits, ident_t **provided,
                           unsigned *nprovided)
{
   const char *kind = NULL;
   bool has_ports = false;

   switch (vlog_kind(unit)) {
   case V_MODULE:
      if (vlog_flags(unit) & VLOG_F_INTERFACE)
         kind = "interface";
      else if (vlog_flags(unit) & VLOG_F_CHECKER)
         kind = "checker";
      else
         kind = "module";
      has_ports = vlog_ports(unit) > 0;
      break;
   case V_PROGRAM:
      kind = "program";
      has_ports = vlog_ports(unit) > 0;
      break;
   case V_PACKAGE:
      kind = "package";
      break;
   case V_PRIMITIVE:
      kind = "primitive";
      break;
   case V_CONFIG:
      kind = "configuration";
      break;
   default:
      return;
   }

   ident_t name = vlog_ident(unit);
   ident_t simple = vlog_has_ident2(unit) ? vlog_ident2(unit)
      : ident_rfrom(name, '.');

   scan_ctx_t ctx = { .tb = units, .inst = tb_new() };
   vlog_visit_only(unit, scan_inst_list_cb, &ctx, V_INST_LIST);

   if ((*nunits)++ > 0)
      tb_append(units, ',');

   tb_cat(units, "{\"name\":");
   scan_json_string(units, istr(name));
   tb_printf(units, ",\"kind\":\"%s\"", kind);
   if (vlog_kind(unit) == V_MODULE || vlog_kind(unit) == V_PROGRAM)
      tb_printf(units, ",\"ports\":%s", has_ports ? "true" : "false");
   tb_printf(units, ",\"instantiates\":[%s]}", tb_get(ctx.inst));

   tb_free(ctx.inst);
   free(ctx.seen);

   // A reference to the unit by name from the same file is satisfied
   char *ref LOCAL = xasprintf("?%s", istr(simple));
   *provided = xrealloc_array(*provided, *nprovided + 2, sizeof(ident_t));
   (*provided)[(*nprovided)++] = name;
   (*provided)[(*nprovided)++] = ident_new(ref);
}

unsigned scan_file_deps(const char *file, text_buf_t *json)
{
   // Parse the file as analysis would but without any libraries present:
   // references to missing units are recorded instead of being errors

   lib_t prev = lib_work();
   lib_t work = lib_tmp(istr(lib_name(prev)));   // Fresh for each file
   lib_set_work(work);

   lib_set_scan_mode(true);
   diag_set_scan_filter(true);
   const unsigned old_limit = set_error_limit(UINT_MAX);

   lib_scan_clear();

   const unsigned base_errors = diag_scan_errors();

   LOCAL_TEXT_BUF units = tb_new();
   unsigned nunits = 0, nprovided = 0;
   ident_t *provided = NULL;
   const char *language = "vhdl";

   input_from_file(file);

   switch (source_kind()) {
   case SOURCE_VHDL:
      {
         tree_t unit;
         while ((unit = parse())) {
            lib_put(work, unit);
            scan_vhdl_unit(unit, units, &nunits, &provided, &nprovided);
         }
      }
      break;

   case SOURCE_VERILOG:
      {
         language = "verilog";

         LOCAL_TEXT_BUF tb = tb_new();
         input_record_includes(true);
         vlog_preprocess(tb, true);
         input_record_includes(false);

         file_ref_t file_ref = loc_file_ref(file, NULL);
         input_from_buffer(tb_get(tb), tb_len(tb), file_ref, SOURCE_VERILOG);

         vlog_node_t unit;
         while ((unit = vlog_parse())) {
            if (is_top_level(unit))
               lib_put_vlog(work, unit);
            scan_vlog_unit(unit, units, &nunits, &provided, &nprovided);
         }
      }
      break;

   case SOURCE_SDF:
      language = "sdf";
      break;
   }

   tb_cat(json, "{\"file\":");
   scan_json_string(json, file);
   tb_printf(json, ",\"language\":\"%s\",\"units\":[%s],\"requires\":[",
             language, tb_get(units));

   unsigned count, nreq = 0;
   const ident_t *refs = lib_scan_refs(&count);
   ident_t *done = xmalloc_array(MAX(count, 1), sizeof(ident_t));
   for (unsigned i = 0; i < count; i++) {
      ident_t ref = scan_unit_name(refs[i]);
      if (ref == well_known(W_STD_STANDARD))
         continue;

      bool skip = false;
      for (unsigned j = 0; j < nprovided && !skip; j++)
         skip = provided[j] == ref;
      for (unsigned j = 0; j < nreq && !skip; j++)
         skip = done[j] == ref;

      if (!skip) {
         done[nreq] = ref;
         scan_json_list_item(json, &nreq, istr(ref));
      }
   }
   free(done);
   free(provided);

   tb_cat(json, "],\"includes\":[");

   unsigned nincl = 0;
   if (!strcmp(language, "verilog")) {
      unsigned n;
      const char *const *files = input_included_files(&n);
      for (unsigned i = 0; i < n; i++)
         scan_json_list_item(json, &nincl, files[i]);
   }

   tb_cat(json, "]");

   const unsigned errors = diag_scan_errors() - base_errors;
   if (errors > 0)
      tb_printf(json, ",\"errors\":%u", errors);

   tb_cat(json, "}\n");

   set_error_limit(old_limit);
   diag_set_scan_filter(false);
   lib_set_scan_mode(false);

   lib_set_work(prev);
   lib_free(work);

   return errors;
}
