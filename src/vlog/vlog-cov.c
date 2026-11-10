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
#include "ident.h"
#include "scan.h"
#include "vlog/vlog-cov.h"

#include <assert.h>
#include <string.h>

// A covergroup is lowered to ordinary classes written in SystemVerilog:
// a runtime class holding the bins and counters which is the same for
// every covergroup, and functions that evaluate the expressions of the
// covergroup in the scope where it was declared and pass the results to it.

// Type coverage with type_option.merge_instances = 0 is the average of the
// instances weighted by their option.weight (19.7)
static const char tcov_text[] =
   "%sfunction real get_coverage();\n"
   "  int c, t;\n"
   "  return get_coverage_n(c, t);\n"
   "endfunction\n"
   "%sfunction real get_coverage_n(ref int cov, ref int tot);\n"
   "  int c, t;\n"
   "  real s, ws, w, p;\n"
   "  cov = 0; tot = 0; s = 0.0; ws = 0.0;\n"
   "  for (int i = 0; i < %s; i++) begin\n"
   "    p = %s.get_inst_coverage_n(c, t);\n"
   "    cov += c; tot += t;\n"
   "    w = %s.option.weight;\n"
   "    if (w > 0) begin s += w * p; ws += w; end\n"
   "  end\n"
   "  if (ws == 0.0) return 0.0;\n"
   "  return s / ws;\n"
   "endfunction\n";

static const char core_template[] =
#include "vlog/vlog-cov-core.inc"
   ;

typedef struct {
   text_buf_t       *tb;
   cov_text_t       *out;
   const cov_group_t *g;
   int               nsel;
   int               nset;
   const char       *cg;      // Runtime class
   char              cpo[64];
   char              opt[64];
   char              base[64];
} gen_t;

static void put_slot(gen_t *s, cov_toks_t t)
{
   cov_text_t *out = s->out;
   out->slots = xrealloc_array(out->slots, out->nslots + 1,
                               sizeof(cov_toks_t));
   out->slots[out->nslots] = t;
   tb_printf(s->tb, "%s%d", cov_slot_prefix(), out->nslots++);
}

static void put_val(gen_t *s, cov_toks_t t)
{
   tb_cat(s->tb, "64'((");
   put_slot(s, t);
   tb_cat(s->tb, "))");
}

static void put_item(gen_t *s, const char *fn, const cov_item_t *it,
                     bool wildcard)
{
   int fl = wildcard ? 4 : 0;

   tb_printf(s->tb, "h.%s(", fn);

   if (it->lo_dollar) {
      tb_cat(s->tb, "64'(0)");
      fl |= 1;
   }
   else
      put_val(s, it->lo);

   tb_cat(s->tb, ", ");

   if (it->hi_dollar) {
      tb_cat(s->tb, "64'(0)");
      fl |= 2;
   }
   else if (it->range)
      put_val(s, it->hi);
   else
      put_val(s, it->lo);

   tb_printf(s->tb, ", %d);\n", fl);
}

static void put_option(gen_t *s, const char *target, const cov_option_t *o)
{
   if (o->type_option && s->g->owner == NULL)
      tb_printf(s->tb, "%s::type_option.%s = (", s->cg, istr(o->name));
   else if (o->type_option)
      tb_printf(s->tb, "h.type_option.%s = (", istr(o->name));
   else
      tb_printf(s->tb, "%s.option.%s = (", target, istr(o->name));
   put_slot(s, o->value);
   tb_cat(s->tb, ");\n");
}

static int find_cp(const cov_group_t *g, ident_t name)
{
   for (int i = 0; i < g->ncps; i++) {
      if (g->cps[i].label == name)
         return i;
   }

   return -1;
}

static bool cp_has_regular(const cov_cp_t *cp)
{
   for (int i = 0; i < cp->nbins; i++) {
      if (cp->bins[i].kind == COV_REGULAR)
         return true;
   }

   return false;
}

static void put_enum_type(gen_t *s, const cov_cp_t *cp)
{
   if (cp->enum_cast != NULL)
      tb_printf(s->tb, "typedef %s __cv_et;\n", istr(cp->enum_cast));
   else {
      tb_cat(s->tb, "typedef type(");
      put_slot(s, cp->expr);
      tb_cat(s->tb, ") __cv_et;\n");
   }
}

static void gen_cp_init(gen_t *s, int i)
{
   const cov_cp_t *cp = &(s->g->cps[i]);
   text_buf_t *tb = s->tb;

   tb_printf(tb, "h.__cp_new(%d, \"%s\", $bits(", i, istr(cp->label));
   put_slot(s, cp->expr);
   tb_cat(tb, "), ");
   if (cp->is_enum)
      tb_cat(tb, "1'b1");
   else {
      tb_cat(tb, "(1'b0 ? (");
      put_slot(s, cp->expr);
      tb_cat(tb, ") : -1) < 0");
   }
   tb_cat(tb, ");\n");

   for (int j = 0; j < cp->nopts; j++) {
      char target[32];
      checked_sprintf(target, sizeof(target), "h.__cpo[%d]", i);
      put_option(s, target, &(cp->opts[j]));
   }

   for (int j = 0; j < cp->nbins; j++) {
      const cov_bin_t *b = &(cp->bins[j]);
      const int kind = b->kind;

      if (b->kind == COV_DEFAULT) {
         tb_printf(tb, "h.__bin(%d, 3, -2, \"%s\");\n", i, istr(b->name));
         continue;
      }

      if (b->trans) {
         for (int k = 0; k < b->nseqs; k++) {
            const cov_seq_t *q = &(b->seqs[k]);
            for (int m = 0; m < q->nsteps; m++) {
               const cov_step_t *st = &(q->steps[m]);
               for (int n = 0; n < st->nitems; n++)
                  put_item(s, "__tq_item", &(st->items[n]), b->wildcard);

               if (st->rep) {
                  tb_cat(tb, "h.__tq_step(int'(");
                  put_slot(s, st->rmin);
                  tb_cat(tb, "), int'(");
                  put_slot(s, st->rmax);
                  tb_printf(tb, "), %d);\n", st->rkind);
               }
               else
                  tb_cat(tb, "h.__tq_step(1, 1, 0);\n");
            }
            tb_cat(tb, "h.__tq_end();\n");
         }

         tb_printf(tb, "h.__tbin(%d, %d, %d, \"%s.%s\");\n", i, kind,
                   b->array == 1, istr(cp->label), istr(b->name));
         continue;
      }

      bool set_is_call = false;
      for (int k = 0; b->has_set && k < b->set_expr.n; k++)
         set_is_call |= b->set_expr.t[k].token == tLPAREN;

      if (b->has_set && !set_is_call) {
         tb_cat(tb, "foreach (");
         put_slot(s, b->set_expr);
         tb_cat(tb, "[__si]) h.__item(64'(");
         put_slot(s, b->set_expr);
         tb_cat(tb, "[__si]), 64'(");
         put_slot(s, b->set_expr);
         tb_cat(tb, "[__si]), 0);\n");
      }
      else if (b->has_set) {
         // The array is the result of a function call so is evaluated once
         // into a variable (19.5.1)
         tb_cat(tb, "begin\ntype(");
         put_slot(s, b->set_expr);
         tb_cat(tb, ") __sx;\n__sx = ");
         put_slot(s, b->set_expr);
         tb_cat(tb, ";\nforeach (__sx[__si]) h.__item(64'(__sx[__si]), "
                "64'(__sx[__si]), 0);\nend\n");
      }

      for (int k = 0; k < b->nitems; k++) {
         if (!b->has_with) {
            put_item(s, "__item", &(b->items[k]), b->wildcard);
            continue;
         }

         // Only the values for which the with expression is true
         const cov_item_t *it = &(b->items[k]);
         tb_cat(tb, "begin\nlongint __lo, __hi;\n__lo = ");
         if (it->lo_dollar)
            tb_printf(tb, "h.__cp_mn[%d]", i);
         else {
            tb_cat(tb, "longint'(");
            put_slot(s, it->lo);
            tb_cat(tb, ")");
         }
         tb_cat(tb, ";\n__hi = ");
         if (it->hi_dollar)
            tb_printf(tb, "h.__cp_mx[%d]", i);
         else if (it->range) {
            tb_cat(tb, "longint'(");
            put_slot(s, it->hi);
            tb_cat(tb, ")");
         }
         else
            tb_cat(tb, "__lo");
         tb_cat(tb, ";\nfor (longint item = __lo; item <= __hi; "
                "item = item + 1) if (");
         put_slot(s, b->with_expr);
         tb_cat(tb, ") h.__item(item, item, 0);\nend\n");
      }

      tb_printf(tb, "h.__bin(%d, %d, ", i, kind);
      if (b->array == 0)
         tb_cat(tb, "-2");
      else if (b->array == 1)
         tb_cat(tb, "-1");
      else {
         tb_cat(tb, "int'(");
         put_slot(s, b->size);
         tb_cat(tb, ")");
      }
      tb_printf(tb, ", \"%s.%s\");\n", istr(cp->label), istr(b->name));
   }

   int nauto = 0;
   if (cp->is_enum && !cp_has_regular(cp)) {
      tb_cat(tb, "begin\n");
      put_enum_type(s, cp);
      tb_cat(tb, "__cv_et e;\ne = e.first();\n");
      tb_printf(tb, "do begin h.__auto_val(%d, 64'(e)); e = e.next(); end "
                "while (e != e.first());\n", i);
      tb_cat(tb, "end\n");
      nauto = 1;
   }

   tb_printf(tb, "h.__cp_end(%d, %d);\n", i, nauto);
}

static int sel_leaf_cp(const cov_group_t *g, const cov_cross_t *x,
                       ident_t name, int *cpidx)
{
   for (int d = 0; d < x->ncps; d++) {
      if (x->cps[d] == name) {
         *cpidx = find_cp(g, name);
         return d;
      }
   }

   return -1;
}

static int bin_group(const cov_group_t *g, int cp, ident_t bin)
{
   // Groups are numbered in the order the bins are declared

   int n = 0;
   for (int i = 0; i < g->ncps; i++) {
      for (int j = 0; j < g->cps[i].nbins; j++, n++) {
         if (i == cp && g->cps[i].bins[j].name == bin)
            return n;
      }
   }

   return -1;
}

static void sel_sets(gen_t *s, const cov_sel_t *sel)
{
   switch (sel->kind) {
   case COV_SEL_AND:
   case COV_SEL_OR:
      sel_sets(s, sel->left);
      sel_sets(s, sel->right);
      break;
   case COV_SEL_NOT:
   case COV_SEL_WITH:
      sel_sets(s, sel->left);
      break;
   case COV_SEL_BINSOF:
      if (sel->has_isect) {
         const int id = sel->set;
         for (int i = 0; i < sel->nisect; i++) {
            const cov_item_t *it = &(sel->isect[i]);
            // A $ is the smallest or largest value of the coverpoint (19.5.1)
            const int cp = find_cp(s->g, sel->cp);
            tb_printf(s->tb, "h.__xset(%d, ", id);
            if (it->lo_dollar && cp >= 0)
               tb_printf(s->tb, "h.__cp_mn[%d]", cp);
            else
               put_val(s, it->lo);
            tb_cat(s->tb, ", ");
            if (it->hi_dollar && cp >= 0)
               tb_printf(s->tb, "h.__cp_mx[%d]", cp);
            else
               put_val(s, it->range ? it->hi : it->lo);
            tb_cat(s->tb, ");\n");
         }
      }
      break;
   }
}

static void sel_pred(gen_t *s, const cov_cross_t *x, const cov_sel_t *sel)
{
   const cov_group_t *g = s->g;
   text_buf_t *tb = s->tb;

   switch (sel->kind) {
   case COV_SEL_AND:
   case COV_SEL_OR:
      tb_cat(tb, "(");
      sel_pred(s, x, sel->left);
      tb_cat(tb, sel->kind == COV_SEL_AND ? " && " : " || ");
      sel_pred(s, x, sel->right);
      tb_cat(tb, ")");
      break;
   case COV_SEL_NOT:
      tb_cat(tb, "(!");
      sel_pred(s, x, sel->left);
      tb_cat(tb, ")");
      break;
   case COV_SEL_WITH:
      tb_cat(tb, "(");
      sel_pred(s, x, sel->left);
      tb_printf(tb, " && __w%d(c))", sel->wid);
      break;
   case COV_SEL_BINSOF:
      {
         int cpidx = -1;
         const int d = sel_leaf_cp(g, x, sel->cp, &cpidx);
         const int set = sel->set;
         if (d < 0) {
            tb_cat(tb, "1'b0");
            break;
         }

         tb_cat(tb, "(1'b1");
         if (sel->has_bin) {
            tb_printf(tb, " && __bo(c[%d], %d)", d,
                      bin_group(g, cpidx, sel->bin));
         }
         if (sel->has_isect)
            tb_printf(tb, " && __boi(%d, c[%d], %d)", cpidx, d, set);
         tb_cat(tb, ")");
      }
      break;
   }
}

static void sel_check(const cov_group_t *g, const cov_cross_t *x,
                      const cov_sel_t *sel, const char **err)
{
   switch (sel->kind) {
   case COV_SEL_AND:
   case COV_SEL_OR:
      sel_check(g, x, sel->left, err);
      sel_check(g, x, sel->right, err);
      break;
   case COV_SEL_NOT:
   case COV_SEL_WITH:
      sel_check(g, x, sel->left, err);
      break;
   case COV_SEL_BINSOF:
      {
         int cpidx = -1;
         if (sel_leaf_cp(g, x, sel->cp, &cpidx) < 0)
            *err = "binsof does not refer to a coverpoint of the cross";
         else if (sel->has_bin && bin_group(g, cpidx, sel->bin) < 0)
            *err = "binsof refers to an unknown bin";
      }
      break;
   }
}

static void sel_withs(gen_t *s, const cov_cross_t *x, const cov_sel_t *sel);

static void gen_with(gen_t *s, const cov_cross_t *x, const cov_sel_t *sel)
{
   // A bin is selected if some combination of values in the products it
   // is in makes the expression true

   text_buf_t *tb = s->tb;

   bool used[16] = {};
   for (int i = 0; i < sel->with.n; i++) {
      if (sel->with.t[i].token != tID)
         continue;

      for (int d = 0; d < x->ncps && d < 16; d++)
         used[d] |= sel->with.t[i].lval.ident == x->cps[d];
   }

   tb_printf(tb, "function bit __w%d(ref int c[$]);\nlongint __n;\n", sel->wid);
   for (int d = 0; d < x->ncps && d < 16; d++) {
      if (used[d])
         tb_printf(tb, "longint %s;\nint __k%d;\n", istr(x->cps[d]), d);
   }

   tb_cat(tb, "__n = 0;\n");

   int depth = 0;
   for (int d = 0; d < x->ncps && d < 16; d++) {
      if (!used[d])
         continue;

      const int cp = find_cp(s->g, x->cps[d]);
      tb_printf(tb, "for (__k%d = __cp_i0[%d]; __k%d < __cp_i1[%d]; __k%d++) "
                "if (__it_bn[__k%d] == c[%d]) for (%s = __it_lo[__k%d]; "
                "%s <= __it_hi[__k%d]; %s = %s + 1) begin\n",
                d, cp, d, cp, d, d, d, istr(x->cps[d]), d, istr(x->cps[d]), d,
                istr(x->cps[d]), istr(x->cps[d]));
      depth++;
   }

   tb_cat(tb, "if (++__n > 4000000) $fatal(1, \"sorry, too many values to "
          "evaluate a with clause of a cross bin\");\nif (");
   put_slot(s, sel->with);
   tb_cat(tb, ") return 1'b1;\n");

   for (int i = 0; i < depth; i++)
      tb_cat(tb, "end\n");

   tb_cat(tb, "return 1'b0;\nendfunction\n");
}

static void sel_withs(gen_t *s, const cov_cross_t *x, const cov_sel_t *sel)
{
   switch (sel->kind) {
   case COV_SEL_AND:
   case COV_SEL_OR:
      sel_withs(s, x, sel->left);
      sel_withs(s, x, sel->right);
      break;
   case COV_SEL_NOT:
      sel_withs(s, x, sel->left);
      break;
   case COV_SEL_WITH:
      sel_withs(s, x, sel->left);
      gen_with(s, x, sel);
      break;
   case COV_SEL_BINSOF:
      break;
   }
}

static void gen_cross_init(gen_t *s, int x)
{
   const cov_cross_t *cx = &(s->g->crosses[x]);
   text_buf_t *tb = s->tb;
   const int ncp = s->g->ncps;

   tb_cat(tb, "h.__x_new();\n");
   for (int d = 0; d < cx->ncps; d++)
      tb_printf(tb, "h.__x_dim(%d);\n", find_cp(s->g, cx->cps[d]));
   tb_printf(tb, "h.__x_begin(%d, \"%s\");\n", x, istr(cx->label));

   for (int j = 0; j < cx->nopts; j++) {
      char target[32];
      checked_sprintf(target, sizeof(target), "h.__cpo[%d]", ncp + x);
      put_option(s, target, &(cx->opts[j]));
   }

   for (int j = 0; j < cx->nbins; j++) {
      const cov_xbin_t *b = &(cx->bins[j]);
      sel_sets(s, b->sel);
      tb_printf(tb, "h.__x_bin(%d, %d, %d, \"%s.%s\");\n", x, (int)b->kind,
                s->nsel++, istr(cx->label), istr(b->name));
   }

   tb_printf(tb, "h.__x_end(%d);\n", x);
}

static void gen_eval(gen_t *s)
{
   text_buf_t *tb = s->tb;

   int group = 0;
   for (int i = 0; i < s->g->ncps; i++) {
      const cov_cp_t *cp = &(s->g->cps[i]);

      for (int j = 0; j < cp->nbins; j++, group++) {
         if (!cp->bins[j].has_iff)
            continue;

         tb_printf(tb, "h.__giff(%d, (", group);
         put_slot(s, cp->bins[j].iff);
         tb_cat(tb, ") ? 1'b1 : 1'b0);\n");
      }
   }

   int nreg = 0;
   for (int x = 0; x < s->g->ncrosses; x++) {
      const cov_cross_t *cx = &(s->g->crosses[x]);

      if (cx->has_iff) {
         tb_printf(tb, "h.__xiff(%d, (", x);
         put_slot(s, cx->iff);
         tb_cat(tb, ") ? 1'b1 : 1'b0);\n");
      }

      for (int j = 0; j < cx->nbins; j++) {
         if (cx->bins[j].kind != COV_REGULAR)
            continue;

         if (cx->bins[j].has_iff) {
            tb_printf(tb, "h.__xbiff(%d, %d, (", x, nreg);
            put_slot(s, cx->bins[j].iff);
            tb_cat(tb, ") ? 1'b1 : 1'b0);\n");
         }

         nreg++;
      }
   }

   for (int i = 0; i < s->g->ncps; i++) {
      const cov_cp_t *cp = &(s->g->cps[i]);

      tb_printf(tb, "h.__cp(%d, ", i);
      put_val(s, cp->expr);
      tb_cat(tb, ", ");
      if (cp->has_iff) {
         tb_cat(tb, "(");
         put_slot(s, cp->iff);
         tb_cat(tb, ") ? 1'b1 : 1'b0");
      }
      else
         tb_cat(tb, "1'b1");
      tb_cat(tb, ");\n");
   }

   tb_cat(tb, "h.__endsample();\n");
}

static void put_names(gen_t *s, ident_t *names, int n, bool lead)
{
   for (int i = 0; i < n; i++)
      tb_printf(s->tb, "%s%s", (i > 0 || lead) ? ", " : "", istr(names[i]));
}

static void subst(text_buf_t *tb, const char *tmpl, const gen_t *s,
                  const char *key[], const char *val[], int nkeys)
{
   (void)s;
   for (const char *p = tmpl; *p != '\0'; ) {
      if (*p == '@') {
         const char *end = strchr(p + 1, '@');
         bool found = false;
         for (int i = 0; end != NULL && i < nkeys; i++) {
            const size_t len = strlen(key[i]);
            if ((size_t)(end - p - 1) == len && strncmp(p + 1, key[i], len) == 0) {
               tb_cat(tb, val[i]);
               p = end + 1;
               found = true;
               break;
            }
         }
         if (found)
            continue;
      }

      tb_append(tb, *p++);
   }
}

static void number_sets(cov_sel_t *sel, int *n, int *nwith)
{
   switch (sel->kind) {
   case COV_SEL_AND:
   case COV_SEL_OR:
      number_sets(sel->left, n, nwith);
      number_sets(sel->right, n, nwith);
      break;
   case COV_SEL_NOT:
      number_sets(sel->left, n, nwith);
      break;
   case COV_SEL_WITH:
      number_sets(sel->left, n, nwith);
      sel->wid = (*nwith)++;
      break;
   case COV_SEL_BINSOF:
      sel->set = sel->has_isect ? (*n)++ : -1;
      break;
   }
}

static void put_arg_list(gen_t *s, cov_toks_t args, const char *lead)
{
   if (args.n > 0) {
      tb_cat(s->tb, lead);
      put_slot(s, args);
   }
}

const char *cov_generate(cov_group_t *g, cov_text_t *out)
{
   gen_t gen = { .g = g, .out = out };
   gen_t *s = &gen;

   const bool class_mode = g->owner != NULL;
   const char *name = istr(g->name);

   char core[96];
   if (class_mode)
      checked_sprintf(core, sizeof(core), "__cvk_%s", name);
   else
      checked_sprintf(core, sizeof(core), "%s", name);

   s->cg = core;
   checked_sprintf(s->cpo, sizeof(s->cpo), "__cvc_%s", name);
   checked_sprintf(s->opt, sizeof(s->opt), "__cvo_%s", name);
   checked_sprintf(s->base, sizeof(s->base), "__cvb_%s", name);

   const char *err = NULL;
   int nset = 0, nwith = 0;
   for (int x = 0; x < g->ncrosses; x++) {
      for (int j = 0; j < g->crosses[x].nbins; j++) {
         sel_check(g, &(g->crosses[x]), g->crosses[x].bins[j].sel, &err);
         number_sets(g->crosses[x].bins[j].sel, &nset, &nwith);
      }
   }

   if (err != NULL)
      return err;

   LOCAL_TEXT_BUF init = tb_new();
   LOCAL_TEXT_BUF eval = tb_new();
   LOCAL_TEXT_BUF body = tb_new();

   LOCAL_TEXT_BUF decls = tb_new();
   s->tb = decls;
   for (int i = 0; i < g->nsample_decls; i++) {
      put_slot(s, g->sample_decls[i]);
      tb_cat(decls, ";\n");
   }

   s->tb = init;

   for (int i = 0; i < g->nopts; i++)
      put_option(s, "h", &(g->opts[i]));

   for (int i = 0; i < g->ncps; i++)
      gen_cp_init(s, i);

   for (int x = 0; x < g->ncrosses; x++)
      gen_cross_init(s, x);

   s->tb = eval;
   gen_eval(s);

   s->tb = body;
   for (int i = 0; i < g->ncps; i++) {
      if (!g->cps[i].implicit && g->cps[i].label != NULL)
         tb_printf(body, "%s %s;\n", s->cpo, istr(g->cps[i].label));
   }
   for (int x = 0; x < g->ncrosses; x++)
      tb_printf(body, "%s %s;\n", s->cpo, istr(g->crosses[x].label));

   // A copy of each argument is visible to the expressions and a
   // reference stays bound to its variable
   s->tb = body;
   for (int i = 0; i < g->nctor_decls; i++) {
      const bool is_ref = i < g->nctor_ref && g->ctor_ref[i];
      if (class_mode && !is_ref)
         continue;

      tb_cat(body, is_ref ? "ref " : "");
      put_slot(s, g->ctor_decls[i]);
      tb_cat(body, ";\n");
   }

   tb_cat(body, "function void __mkobjs();\n");
   for (int i = 0; i < g->ncps; i++) {
      if (!g->cps[i].implicit && g->cps[i].label != NULL)
         tb_printf(body, "%s = __cpo[%d];\n", istr(g->cps[i].label), i);
   }
   for (int x = 0; x < g->ncrosses; x++)
      tb_printf(body, "%s = __cpo[%d];\n", istr(g->crosses[x].label),
                g->ncps + x);
   tb_cat(body, "endfunction\n");

   tb_cat(body, "function bit __xp(int sel, ref int c[$]);\ncase (sel)\n");
   {
      int sel = 0;
      for (int x = 0; x < g->ncrosses; x++) {
         const cov_cross_t *cx = &(g->crosses[x]);
         for (int j = 0; j < cx->nbins; j++, sel++) {
            tb_printf(body, "%d: return ", sel);
            sel_pred(s, cx, cx->bins[j].sel);
            tb_cat(body, ";\n");
         }
      }
   }
   tb_cat(body, "default: return 1'b0;\nendcase\nendfunction\n");

   for (int x = 0; x < g->ncrosses; x++) {
      const cov_cross_t *cx = &(g->crosses[x]);
      for (int j = 0; j < cx->nbins; j++)
         sel_withs(s, cx, cx->bins[j].sel);
   }

   // Statements evaluating the expressions run in the scope of the
   // covergroup with the runtime object in h
   text_buf_t *tb = out->text;

   if (!class_mode) {
      tb_cat(body, "function void __init(");
      s->tb = body;
      put_arg_list(s, g->ctor_args, "");
      tb_printf(body, ");\n%s h;\n%sh = this;\n", core, tb_get(decls));
      for (int i = 0; i < g->nctor; i++)
         tb_printf(body, "h.%s = %s;\n", istr(g->ctor_names[i]),
                   istr(g->ctor_names[i]));
      tb_cat(body, tb_get(init));
      tb_cat(body, "endfunction\n");

      tb_printf(body, "function void sample(");
      put_arg_list(s, g->sample_args, "");
      tb_printf(body, ");\n%s h;\nh = this;\nif (!__on) return;\n%s",
                core, tb_get(eval));
      tb_cat(body, "endfunction\n");
   }

   LOCAL_TEXT_BUF cargs = tb_new();
   LOCAL_TEXT_BUF icall = tb_new();
   if (!class_mode) {
      s->tb = cargs;
      put_arg_list(s, g->ctor_args, "");
      s->tb = icall;
      tb_cat(icall, "__init(");
      put_names(s, g->ctor_names, g->nctor, false);
      tb_cat(icall, ");\n__fin();\n");
   }

   LOCAL_TEXT_BUF statics = tb_new();
   LOCAL_TEXT_BUF reg = tb_new();
   LOCAL_TEXT_BUF tcov = tb_new();
   char na[64], ai[64];
   if (class_mode) {
      tb_printf(statics, "%s type_option = new;\n", s->opt);
      tb_cat(reg, "");
      tb_printf(tcov, tcov_text, "", "", "__na()", "__ai(i)", "__ai(i)");
      checked_sprintf(na, sizeof(na), "1");
      checked_sprintf(ai, sizeof(ai), "this");
   }
   else {
      tb_printf(statics, "static %s __all[$];\nstatic %s type_option = new;\n",
                core, s->opt);
      tb_cat(reg, "__all.push_back(this);\n");
      tb_printf(tcov, tcov_text, "static ", "static ", "__all.size()",
                "__all[i]", "__all[i]");
      checked_sprintf(na, sizeof(na), "__all.size()");
      checked_sprintf(ai, sizeof(ai), "__all[i]");
   }

   const char *keys[] = { "OPT", "BASE", "CPO", "CG", "CGNAME", "CARGS",
                          "INITCALL", "BODY", "STATICS", "REG", "TCOV", "NA",
                          "AI" };
   const char *vals[] = { s->opt, s->base, s->cpo, core, name,
                          tb_get(cargs), tb_get(icall), tb_get(body),
                          tb_get(statics), tb_get(reg), tb_get(tcov), na, ai };
   subst(tb, core_template, s, keys, vals, ARRAY_LEN(keys));

   if (class_mode) {
      const char *owner = istr(g->owner);
      s->tb = tb;

      tb_printf(tb, "static %s __cv_all_%s[$];\n", core, name);
      tb_printf(tb, "function void __cv_init_%s(%s h", name, core);
      put_arg_list(s, g->ctor_args, ", ");
      tb_cat(tb, ");\n");
      tb_cat(tb, tb_get(decls));
      tb_printf(tb, "__cv_all_%s.push_back(h);\n", name);
      for (int i = 0; i < g->nctor_decls && i < g->nctor_ref; i++) {
         if (g->ctor_ref[i])
            tb_printf(tb, "h.%s = %s;\n", istr(g->ctor_names[i]),
                      istr(g->ctor_names[i]));
      }
      tb_cat(tb, tb_get(init));
      if (g->has_event) {
         tb_cat(tb, "fork\nforever begin\n@(");
         put_slot(s, g->event);
         tb_printf(tb, ");\nif (h.__on) __cv_eval_%s(h);\nend\njoin_none\n", name);
      }
      tb_cat(tb, "endfunction\n");

      tb_printf(tb, "function void __cv_eval_%s(%s h", name, core);
      put_arg_list(s, g->sample_args, ", ");
      tb_cat(tb, ");\n");
      for (int i = 0; i < g->nctor_decls && i < g->nctor_ref; i++) {
         if (g->ctor_ref[i]) {
            put_slot(s, g->ctor_decls[i]);
            tb_printf(tb, " = h.%s;\n", istr(g->ctor_names[i]));
         }
      }
      tb_cat(tb, tb_get(eval));
      tb_cat(tb, "endfunction\n");

      tb_printf(tb, "class __cvp_%s extends %s;\n%s __owner;\n", name, core,
                owner);
      tb_printf(tb, "function new(%s owner", owner);
      put_arg_list(s, g->ctor_args, ", ");
      tb_printf(tb, ");\nsuper.new();\n__owner = owner;\n"
                "owner.__cv_init_%s(this", name);
      put_names(s, g->ctor_names, g->nctor, true);
      tb_cat(tb, ");\n__fin();\nendfunction\n");
      tb_printf(tb, "virtual function int __na();\nreturn __owner.__cv_all_%s.size();\n"
                "endfunction\n", name);
      tb_printf(tb, "virtual function %s __ai(int i);\nreturn __owner.__cv_all_%s[i];\n"
                "endfunction\n", core, name);
      tb_cat(tb, "function void sample(");
      put_arg_list(s, g->sample_args, "");
      tb_printf(tb, ");\nif (!__on) return;\n__owner.__cv_eval_%s(this", name);
      put_names(s, g->sample_names, g->nsample, true);
      tb_cat(tb, ");\nendfunction\nendclass\n");
      tb_printf(tb, "__cvp_%s %s;\n", name, name);
   }
   else if (g->has_event) {
      s->tb = tb;
      tb_printf(tb, "always @(");
      put_slot(s, g->event);
      tb_printf(tb, ") for (int __cvi = 0; __cvi < %s::__all.size(); "
                "__cvi++) %s::__all[__cvi].sample();\n", name, name);
   }

   return NULL;
}

const char *cov_slot_prefix(void)
{
   return "__cvslot_";
}
