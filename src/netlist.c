//
//  Copyright (C) 2025  Nick Gasson
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
#include "conn.h"
#include "hash.h"
#include "netlist.h"

#include <assert.h>
#include <inttypes.h>
#include <stdlib.h>
#include <string.h>

// A bit is a net number (at least two) or a negated constant character
typedef int32_t bit_t;

#define CONST_BIT(c) (-(bit_t)(unsigned char)(c))

typedef struct {
   FILE          *f;
   conn_block_t  *block;
   bit_t          next;
   bit_t         *base;       // First bit number of each net
   bool           first;      // No comma needed before next item
   shash_t       *names;      // Cell names already used
   shash_t       *boxes;      // Black box modules already written
   A(char *)      owned;      // Strings to free with the name table
   int            depth;
   bool           evidence;   // Write the evidence of processes
} wctx_t;

typedef A(bit_t) bit_list_t;

static bool valid_utf8(const char *s)
{
   for (const unsigned char *p = (const unsigned char *)s; *p; ) {
      int extra;
      if (*p < 0x80)
         extra = 0;
      else if ((*p & 0xe0) == 0xc0 && *p >= 0xc2)
         extra = 1;
      else if ((*p & 0xf0) == 0xe0)
         extra = 2;
      else if ((*p & 0xf8) == 0xf0)
         extra = 3;
      else
         return false;

      p++;
      for (; extra > 0; extra--, p++) {
         if ((*p & 0xc0) != 0x80)
            return false;
      }
   }

   return true;
}

static void json_string(FILE *f, const char *s)
{
   // Identifiers in VHDL are ISO 8859-1 so convert them unless the string
   // is already valid UTF-8 as a file name might be
   const bool latin1 = !valid_utf8(s);

   fputc('"', f);
   for (; *s; s++) {
      const unsigned char c = *s;
      switch (c) {
      case '"': fputs("\\\"", f); break;
      case '\\': fputs("\\\\", f); break;
      case '\n': fputs("\\n", f); break;
      case '\t': fputs("\\t", f); break;
      case '\r': fputs("\\r", f); break;
      default:
         if (c < 0x20)
            fprintf(f, "\\u%04x", c);
         else if (c >= 0x80 && latin1) {
            fputc(0xc0 | (c >> 6), f);
            fputc(0x80 | (c & 0x3f), f);
         }
         else
            fputc(c, f);
      }
   }
   fputc('"', f);
}

static void comma(wctx_t *w)
{
   if (!w->first)
      fputc(',', w->f);
   w->first = false;
}

static void key(wctx_t *w, const char *k)
{
   comma(w);
   fputc('\n', w->f);
   json_string(w->f, k);
   fputc(':', w->f);
}

static void write_bit(FILE *f, bit_t b)
{
   if (b >= 2)
      fprintf(f, "%d", b);
   else
      fprintf(f, "\"%c\"", (char)-b);
}

static void write_bits(FILE *f, const bit_t *bits, unsigned n)
{
   fputc('[', f);
   for (unsigned i = 0; i < n; i++) {
      if (i > 0)
         fputc(',', f);
      write_bit(f, bits[i]);
   }
   fputc(']', f);
}

static bit_t net_bit(wctx_t *w, const conn_net_t *net, unsigned pos)
{
   // Nets of other blocks cannot be connected within this module: they
   // are left unconnected rather than shared with an unrelated net
   unsigned idx = 0;
   for (; idx < w->block->nnets; idx++) {
      if (w->block->nets[idx] == net)
         return w->base[idx] + pos;
   }

   return w->next++;
}

// Bits of positions lo..hi of NET in the order used by Yosys: least
// significant first, which is the right-most element for a descending
// range
static void net_bits(wctx_t *w, const conn_net_t *net, unsigned lo,
                     unsigned hi, bit_list_t *out)
{
   if (net->descending) {
      for (int p = hi; p >= (int)lo; p--)
         APUSH(*out, net_bit(w, net, p));
   }
   else {
      for (unsigned p = lo; p <= hi; p++)
         APUSH(*out, net_bit(w, net, p));
   }
}

static const char *dir_string(conn_dir_t dir)
{
   switch (dir) {
   case CONN_IN: return "input";
   case CONN_OUT: return "output";
   default: return "inout";
   }
}

static const char *lang_string(conn_lang_t lang)
{
   return lang == CONN_VHDL ? "vhdl" : "verilog";
}

static char *src_string(const char *file, unsigned line)
{
   if (file == NULL)
      return NULL;
   return xasprintf("%s:%u", file, line);
}

static void write_attr_str(wctx_t *w, const char *k, const char *v)
{
   if (v == NULL)
      return;
   key(w, k);
   json_string(w->f, v);
}

static void begin_obj(wctx_t *w)
{
   fputc('{', w->f);
   w->first = true;
}

static void end_obj(wctx_t *w)
{
   fputc('\n', w->f);
   fputc('}', w->f);
   w->first = false;
}

static char *unique_name(wctx_t *w, const char *base)
{
   char *name = xstrdup(base);
   for (int n = 2; shash_get(w->names, name) != NULL; n++) {
      free(name);
      name = xasprintf("%s#%d", base, n);
   }

   shash_put(w->names, name, name);
   APUSH(w->owned, name);
   return name;
}

static const char *proc_kind_string(conn_proc_kind_t kind)
{
   switch (kind) {
   case CONN_PROC_VHDL: return "process";
   case CONN_PROC_ALWAYS: return "always";
   case CONN_PROC_ALWAYS_FF: return "always_ff";
   case CONN_PROC_ALWAYS_COMB: return "always_comb";
   case CONN_PROC_ALWAYS_LATCH: return "always_latch";
   case CONN_PROC_INITIAL: return "initial";
   case CONN_PROC_FINAL: return "final";
   case CONN_PROC_ASSIGN: return "assign";
   case CONN_PROC_GATE: return "gate";
   default: return "other";
   }
}

// Printable name of a range of a net such as "q[3:2]"
static char *range_label(const conn_net_t *net, unsigned lo, unsigned hi)
{
   if (lo == 0 && hi == net->width - 1)
      return xstrdup(net->name);

   for (unsigned i = 0; i < net->nfields; i++) {
      if (net->fields[i].lo == lo && net->fields[i].hi == hi)
         return xasprintf("%s%s", net->name, net->fields[i].name);
   }

   if (net->has_bounds) {
      const int64_t a = net->descending ? net->left - lo : net->left + lo;
      const int64_t b = net->descending ? net->left - hi : net->left + hi;
      if (a == b)
         return xasprintf("%s[%"PRIi64"]", net->name, a);
      else
         return xasprintf("%s[%"PRIi64":%"PRIi64"]", net->name, a, b);
   }
   else if (lo == hi)
      return xasprintf("%s[%u]", net->name, lo);
   else
      return xasprintf("%s[%u:%u]", net->name, lo, hi);
}

typedef struct {
   char       *name;
   bool        output;
   bit_list_t  bits;
} pin_t;

typedef A(pin_t) pin_list_t;

static void free_pins(pin_list_t *pins)
{
   for (unsigned i = 0; i < pins->count; i++) {
      free(pins->items[i].name);
      ACLEAR(pins->items[i].bits);
   }
   ACLEAR(*pins);
}

typedef struct {
   const char *key;
   const char *value;
} attr_t;

// Short form of an expression to draw in a box: at most 24 characters
static char *short_text(const char *text)
{
   if (strlen(text) <= 24)
      return xstrdup(text);

   // Do not cut a multi-byte character
   size_t n = 21;
   while (n > 0 && ((unsigned char)text[n] & 0xc0) == 0x80)
      n--;
   return xasprintf("%.*s...", (int)n, text);
}

typedef struct {
   const conn_evidence_t *ev;
   const conn_proc_t     *proc;
   char                 **rnames;   // Pin name of each read range
   char                 **dnames;   // ... and of each driven range
} evid_t;

static void write_evidence(wctx_t *w, const evid_t *e);

static void write_cell(wctx_t *w, const char *name, const char *type,
                       const pin_list_t *pins, const char *src,
                       const char *label, const attr_t *extra, int nextra,
                       bool approx, const evid_t *evid)
{
   key(w, name);
   begin_obj(w);

   write_attr_str(w, "type", type);

   key(w, "attributes");
   begin_obj(w);
   write_attr_str(w, "src", src);
   write_attr_str(w, "label", label);
   for (int i = 0; i < nextra; i++)
      write_attr_str(w, extra[i].key, extra[i].value);
   if (approx) {
      key(w, "approx");
      fputs("1", w->f);
   }
   end_obj(w);

   if (evid != NULL)
      write_evidence(w, evid);

   key(w, "port_directions");
   begin_obj(w);
   for (unsigned i = 0; i < pins->count; i++) {
      key(w, pins->items[i].name);
      json_string(w->f, pins->items[i].output ? "output" : "input");
   }
   end_obj(w);

   key(w, "connections");
   begin_obj(w);
   for (unsigned i = 0; i < pins->count; i++) {
      key(w, pins->items[i].name);
      write_bits(w->f, pins->items[i].bits.items, pins->items[i].bits.count);
   }
   end_obj(w);

   end_obj(w);
}

// netlistsvg builds identifiers from cell and pin names with dots as the
// separator and fails to draw a pin with a dot in its name, so pins and
// ports use a slash instead: b/data for the signal data of interface port b
static char *pin_name(char *name)
{
   for (char *p = name; *p; p++) {
      if (*p == '.')
         *p = '/';
   }
   return name;
}

static const char *add_pin(pin_list_t *pins, char *name, bool output)
{
   pin_name(name);

   // A process that reads and drives the same signal has an input pin
   // named for the signal and an output pin with a prime appended: count'
   for (unsigned i = 0; i < pins->count; i++) {
      if (strcmp(pins->items[i].name, name) == 0) {
         if (pins->items[i].output == output)
            return pins->items[i].name;
         char *n2 = xasprintf("%s'", name);
         free(name);
         name = n2;
         break;
      }
   }

   pin_t p = { .name = name, .output = output };
   APUSH(*pins, p);
   return name;
}

// Write the names of the input pins that carry the ranges, once each
static void write_pin_list(wctx_t *w, const evid_t *e,
                           const conn_range_t *ranges, unsigned n)
{
   const conn_proc_t *p = e->proc;
   A(const char *) seen = AINIT;

   fputc('[', w->f);
   for (unsigned j = 0; j < p->nreads; j++) {
      const conn_range_t *pr = &(p->reads[j]);
      if (e->rnames[j] == NULL)
         continue;

      bool hit = false;
      for (unsigned i = 0; i < n && !hit; i++)
         hit = pr->net == ranges[i].net && pr->lo <= ranges[i].hi
            && ranges[i].lo <= pr->hi;
      if (!hit)
         continue;

      bool dup = false;
      for (unsigned k = 0; k < seen.count; k++)
         dup |= seen.items[k] == e->rnames[j];
      if (dup)
         continue;

      if (seen.count > 0)
         fputc(',', w->f);
      json_string(w->f, e->rnames[j]);
      APUSH(seen, e->rnames[j]);
   }
   fputc(']', w->f);
   ACLEAR(seen);
}

static const char *evidence_class(conn_ev_class_t cls)
{
   switch (cls) {
   case CONN_EV_SEQ: return "seq";
   case CONN_EV_COMB: return "comb";
   case CONN_EV_LATCH: return "latch";
   case CONN_EV_MIXED: return "mixed";
   default: return "other";
   }
}

// The pin of the process that holds a range of one of its reads
static const char *read_pin(const evid_t *e, const conn_range_t *r)
{
   const conn_proc_t *p = e->proc;
   for (unsigned i = 0; i < p->nreads; i++) {
      const conn_range_t *pr = &(p->reads[i]);
      if (e->rnames[i] != NULL && pr->net == r->net && pr->lo <= r->lo
          && pr->hi >= r->hi)
         return e->rnames[i];
   }

   return NULL;
}

static void write_why(wctx_t *w, unsigned why)
{
   static const struct { unsigned bit; const char *name; } names[] = {
      { CONN_WHY_CALL, "call" }, { CONN_WHY_VARIABLE, "variable" },
      { CONN_WHY_LOOP, "loop" }, { CONN_WHY_DYNAMIC, "dynamic-index" },
      { CONN_WHY_OTHER, "other" },
   };

   key(w, "reasons");
   fputc('[', w->f);
   bool first = true;
   for (unsigned i = 0; i < ARRAY_LEN(names); i++) {
      if (why & names[i].bit) {
         if (!first)
            fputc(',', w->f);
         json_string(w->f, names[i].name);
         first = false;
      }
   }
   fputc(']', w->f);
}

// The netlist bits of a signal: the bits of the pin that carry it, which a
// pin that groups several elements or fields does not tell
static void write_sig_bits(wctx_t *w, const conn_range_t *r)
{
   fputs(",\"bits\":", w->f);
   bit_list_t bits = AINIT;
   net_bits(w, r->net, r->lo, r->hi, &bits);
   write_bits(w->f, bits.items, bits.count);
   ACLEAR(bits);
}

static void write_evidence(wctx_t *w, const evid_t *e)
{
   const conn_evidence_t *ev = e->ev;
   const conn_proc_t *p = e->proc;

   key(w, "evidence");
   begin_obj(w);

   key(w, "class");
   json_string(w->f, evidence_class(ev->cls));

   key(w, "confidence");
   json_string(w->f, ev->approx || p->approx ? "approx" : "exact");

   if (ev->approx && ev->reasons != 0)
      write_why(w, ev->reasons);

   key(w, "clocks");
   fputc('[', w->f);
   bool first = true;
   for (unsigned i = 0; i < ev->nclocks; i++) {
      const char *pin = read_pin(e, &(ev->clocks[i].sig));
      if (pin == NULL)
         continue;
      static const char *const edges[] = { "rise", "fall", "both" };
      fprintf(w->f, "%s{\"pin\":", first ? "" : ",");
      json_string(w->f, pin);
      fprintf(w->f, ",\"edge\":\"%s\"", edges[ev->clocks[i].edge]);
      write_sig_bits(w, &(ev->clocks[i].sig));
      fputc('}', w->f);
      first = false;
   }
   fputc(']', w->f);

   key(w, "async");
   fputc('[', w->f);
   first = true;
   for (unsigned i = 0; i < ev->nasync; i++) {
      const char *pin = read_pin(e, &(ev->async[i].sig));
      if (pin == NULL)
         continue;
      static const char *const levels[] = { "low", "high", "unknown" };
      static const char *const roles[] = { "reset", "set", "unknown" };
      fprintf(w->f, "%s{\"pin\":", first ? "" : ",");
      json_string(w->f, pin);
      fprintf(w->f, ",\"level\":\"%s\",\"role\":\"%s\"",
              levels[ev->async[i].level], roles[ev->async[i].role]);
      write_sig_bits(w, &(ev->async[i].sig));
      fputc('}', w->f);
      first = false;
   }
   fputc(']', w->f);

   if (ev->nsync_reset > 0) {
      key(w, "sync_reset");
      write_pin_list(w, e, ev->sync_reset, ev->nsync_reset);
   }

   if (ev->cls != CONN_EV_OTHER) {
      key(w, "outputs");
      begin_obj(w);
      for (unsigned i = 0; i < ev->nouts; i++) {
         const conn_out_ev_t *o = &(ev->outs[i]);
         if (o->drive >= p->ndrives || e->dnames[o->drive] == NULL)
            continue;

         bool dup = false;
         for (unsigned j = 0; j < i; j++)
            dup |= ev->outs[j].drive == o->drive;
         if (dup)
            continue;

         key(w, e->dnames[o->drive]);
         begin_obj(w);
         key(w, "class");
         json_string(w->f, evidence_class(o->cls));
         if (o->reasons != 0)
            write_why(w, o->reasons);
         if (o->cls == CONN_EV_SEQ && o->clock >= 0) {
            key(w, "clock");
            fprintf(w->f, "%d", o->clock);
         }
         key(w, "data");
         write_pin_list(w, e, o->data, o->ndata);
         key(w, "ctrl");
         write_pin_list(w, e, o->ctrl, o->nctrl);
         if (o->has_copy) {
            const char *pin = read_pin(e, &(o->copy));
            if (pin != NULL) {
               key(w, "copy");
               json_string(w->f, pin);
            }
         }
         end_obj(w);
      }
      end_obj(w);
   }

   end_obj(w);
}

static void write_process(wctx_t *w, const conn_proc_t *p)
{
   pin_list_t pins = AINIT;
   LOCAL_TEXT_BUF hier = tb_new();
   char **rnames = NULL, **dnames = NULL;
   if (w->evidence) {
      rnames = xcalloc_array(MAX(p->nreads, 1), sizeof(char *));
      dnames = xcalloc_array(MAX(p->ndrives, 1), sizeof(char *));
   }

   for (unsigned i = 0; i < p->nreads + p->ndrives; i++) {
      const bool is_read = i < p->nreads;
      const conn_range_t *r = is_read
         ? &(p->reads[i]) : &(p->drives[i - p->nreads]);

      if (r->net->block != w->block) {
         // Hierarchical reference to another block
         char *l = range_label(r->net, r->lo, r->hi);
         if (tb_len(hier) > 0)
            tb_append(hier, ',');
         tb_printf(hier, "%s%s.%s", is_read ? "" : "->",
                   r->net->block->name, l);
         free(l);
         continue;
      }

      const char *pn = add_pin(&pins, range_label(r->net, r->lo, r->hi),
                               !is_read);
      net_bits(w, r->net, r->lo, r->hi, &(pins.items[pins.count - 1].bits));

      if (w->evidence) {
         if (is_read)
            rnames[i] = (char *)pn;
         else
            dnames[i - p->nreads] = (char *)pn;
      }
   }

   const char *label = p->name;

   char *src = src_string(p->src, p->line);
   char *name = unique_name(w, label);
   attr_t attrs[] = {
      { "kind", proc_kind_string(p->kind) },
      { "hier", tb_len(hier) > 0 ? tb_get(hier) : NULL },
      { "cell", "$process" },
   };
   evid_t evid = { .ev = p->ev, .proc = p, .rnames = rnames,
                   .dnames = dnames };

   // The label is the cell type as that is what is drawn inside the box
   write_cell(w, name, label, &pins, src, label, attrs, 3, p->approx,
              w->evidence && p->ev != NULL ? &evid : NULL);
   free(src);
   free_pins(&pins);
   free(rnames);
   free(dnames);
}

static void write_expr_cell(wctx_t *w, const conn_seg_t *s, const char *src,
                            unsigned *serial, const bit_t *out, bool reverse)
{
   // An expression normally reads nets of this block and drives the formal
   // of the instance through pin Y.  If the formal is an output the
   // expression is a conversion in the other direction: it reads the
   // formal through pin A and drives the nets.
   pin_list_t pins = AINIT;

   for (unsigned i = 0; i < s->nreads; i++) {
      const conn_range_t *r = &(s->reads[i]);
      add_pin(&pins, range_label(r->net, r->lo, r->hi), reverse);
      net_bits(w, r->net, r->lo, r->hi, &(pins.items[pins.count - 1].bits));
   }

   add_pin(&pins, xstrdup(reverse ? "A" : "Y"), !reverse);
   for (unsigned i = 0; i < s->width; i++)
      APUSH(pins.items[pins.count - 1].bits, out[i]);

   char *base = xasprintf("expr#%u", ++*serial);
   char *name = unique_name(w, base);
   attr_t attrs[] = { { "expr", s->text }, { "cell", "$expr" } };
   char *type = short_text(s->text);
   write_cell(w, name, type, &pins, src, s->text, attrs, 2, false, NULL);
   free(type);
   free(base);
   free_pins(&pins);
}

static void collect_conv(const conn_inst_t *inst, text_buf_t *tb)
{
   for (unsigned i = 0; i < inst->nconns; i++) {
      const conn_pconn_t *c = &(inst->conns[i]);
      for (unsigned j = 0; j < c->nsegs; j++) {
         const char *conv = c->segs[j].conv;
         if (conv == NULL)
            continue;

         // Avoid repeating a name
         const char *have = tb_get(tb);
         const size_t n = strlen(conv);
         bool found = false;
         for (const char *p = have; (p = strstr(p, conv)) != NULL; p += n) {
            if ((p == have || p[-1] == ',') && (p[n] == ',' || p[n] == '\0')) {
               found = true;
               break;
            }
         }

         if (!found) {
            if (tb_len(tb) > 0)
               tb_append(tb, ',');
            tb_cat(tb, conv);
         }
      }
   }
}

// Generics or parameters of an instance with their values as text
// Attributes of a net or unit that do not clash with a key written above
static void write_user_attrs(wctx_t *w, const conn_attr_t *attrs,
                             unsigned n)
{
   static const char *const reserved[] = {
      "src", "record", "interface", "field", "language", "unit", "top",
      "blackbox", "inferred_ports", "approx", "label", "kind", "hier", "cell",
      "expr", "conversions",
   };

   for (unsigned i = 0; i < n; i++) {
      bool skip = false;
      for (unsigned j = 0; j < ARRAY_LEN(reserved); j++)
         skip |= strcmp(attrs[i].name, reserved[j]) == 0;
      if (!skip)
         write_attr_str(w, attrs[i].name, attrs[i].value);
   }
}

static void write_params(wctx_t *w, const conn_block_t *b)
{
   if (b->nparams == 0)
      return;

   key(w, "parameters");
   begin_obj(w);
   for (unsigned i = 0; i < b->nparams; i++)
      write_attr_str(w, b->params[i].name, b->params[i].value);
   end_obj(w);
}

static void write_instance(wctx_t *w, const conn_inst_t *inst,
                           unsigned *serial)
{
   const conn_block_t *child = inst->child;
   pin_list_t pins = AINIT;
   char *src = src_string(inst->src, inst->line);

   for (unsigned i = 0; i < child->nports; i++) {
      conn_net_t *formal = child->nets[i];

      bit_t *byposition = xmalloc_array(formal->width, sizeof(bit_t));
      for (unsigned p = 0; p < formal->width; p++)
         byposition[p] = 0;

      for (unsigned j = 0; j < inst->nconns; j++) {
         const conn_pconn_t *c = &(inst->conns[j]);
         if (c->formal != formal)
            continue;

         unsigned pos = c->lo;
         for (unsigned k = 0; k < c->nsegs; k++) {
            const conn_seg_t *s = &(c->segs[k]);
            bit_t *dst = &(byposition[pos]);

            switch (s->kind) {
            case CONN_SEG_NET:
               for (unsigned b = 0; b < s->width; b++)
                  dst[b] = net_bit(w, s->range.net, s->range.lo + b);
               break;
            case CONN_SEG_CONST:
               for (unsigned b = 0; b < s->width; b++)
                  dst[b] = CONST_BIT(s->bits[b]);
               break;
            case CONN_SEG_EXPR:
               {
                  bit_t *out = xmalloc_array(s->width, sizeof(bit_t));
                  for (unsigned b = 0; b < s->width; b++) {
                     out[b] = w->next++;
                     dst[b] = out[b];
                  }
                  // The expression cell outputs in position order
                  char *esrc = src_string(c->src, c->line);
                  write_expr_cell(w, s, esrc, serial, out, formal->dir == CONN_OUT);
                  free(esrc);
                  free(out);
               }
               break;
            case CONN_SEG_OPEN:
               break;
            }

            pos += s->width;
         }
      }

      for (unsigned p = 0; p < formal->width; p++) {
         if (byposition[p] == 0)
            byposition[p] = w->next++;   // Not connected
      }

      add_pin(&pins, xstrdup(formal->name), formal->dir == CONN_OUT);

      bit_list_t *bits = &(pins.items[pins.count - 1].bits);
      if (formal->descending) {
         for (int p = formal->width - 1; p >= 0; p--)
            APUSH(*bits, byposition[p]);
      }
      else {
         for (unsigned p = 0; p < formal->width; p++)
            APUSH(*bits, byposition[p]);
      }

      free(byposition);
   }

   // The direction of each pin is written below as it is for the child
   // module's ports: input, output or inout
   key(w, inst->label);
   begin_obj(w);
   write_attr_str(w, "type", child->blackbox && child->unit
                  ? child->unit : child->name);

   LOCAL_TEXT_BUF convs = tb_new();
   collect_conv(inst, convs);

   key(w, "attributes");
   begin_obj(w);
   write_attr_str(w, "src", src);
   write_attr_str(w, "language", lang_string(child->lang));
   write_attr_str(w, "unit", child->unit);
   if (child->blackbox) {
      key(w, "blackbox");
      fputs("1", w->f);
      if (child->inferred) {
         key(w, "inferred_ports");
         fputs("1", w->f);
      }
   }
   if (tb_len(convs) > 0)
      write_attr_str(w, "conversions", tb_get(convs));
   end_obj(w);

   write_params(w, child);

   key(w, "port_directions");
   begin_obj(w);
   for (unsigned i = 0; i < child->nports; i++) {
      key(w, pins.items[i].name);
      json_string(w->f, dir_string(child->nets[i]->dir));
   }
   end_obj(w);

   key(w, "connections");
   begin_obj(w);
   for (unsigned i = 0; i < child->nports; i++) {
      key(w, pins.items[i].name);
      write_bits(w->f, pins.items[i].bits.items, pins.items[i].bits.count);
   }
   end_obj(w);

   end_obj(w);

   free(src);
   free_pins(&pins);
}

static bool hidden_interface(const conn_block_t *b);

static void write_module(wctx_t *w, conn_block_t *b, bool is_top)
{
   w->block = b;
   w->next = 2;
   w->base = xmalloc_array(MAX(b->nnets, 1), sizeof(bit_t));
   w->names = shash_new(64);

   for (unsigned i = 0; i < b->nnets; i++) {
      w->base[i] = w->next;
      w->next += b->nets[i]->width;
   }

   // A black box is a module of the name of the missing unit, which all its
   // instances are cells of
   key(w, b->blackbox && b->unit ? b->unit : b->name);
   begin_obj(w);

   key(w, "attributes");
   begin_obj(w);
   if (b->blackbox) {
      key(w, "blackbox");
      fputs("1", w->f);
      if (b->inferred) {
         key(w, "inferred_ports");
         fputs("1", w->f);
      }
   }
   if (is_top) {
      key(w, "top");
      fputs("1", w->f);
   }
   write_attr_str(w, "language", lang_string(b->lang));
   write_attr_str(w, "unit", b->unit);
   char *src = src_string(b->src, b->line);
   write_attr_str(w, "src", src);
   free(src);
   if (w->evidence)
      write_user_attrs(w, b->attrs, b->nattrs);
   end_obj(w);

   write_params(w, b);

   key(w, "ports");
   begin_obj(w);
   for (unsigned i = 0; i < b->nports; i++) {
      conn_net_t *n = b->nets[i];
      char *pname = pin_name(xstrdup(n->name));
      key(w, pname);
      free(pname);
      begin_obj(w);
      write_attr_str(w, "direction", dir_string(n->dir));
      key(w, "bits");
      bit_list_t bits = AINIT;
      net_bits(w, n, 0, n->width - 1, &bits);
      write_bits(w->f, bits.items, bits.count);
      ACLEAR(bits);
      end_obj(w);
   }
   end_obj(w);

   key(w, "cells");
   begin_obj(w);

   // Reserve instance names first so process names cannot collide
   for (unsigned i = 0; i < b->ninsts; i++)
      shash_put(w->names, b->insts[i]->label, (void *)b->insts[i]->label);

   unsigned serial = 0;
   for (unsigned i = 0; i < b->ninsts; i++) {
      if (!hidden_interface(b->insts[i]->child))
         write_instance(w, b->insts[i], &serial);
   }

   for (unsigned i = 0; i < b->nprocs; i++)
      write_process(w, b->procs[i]);

   end_obj(w);

   key(w, "netnames");
   begin_obj(w);
   for (unsigned i = 0; i < b->nnets; i++) {
      conn_net_t *n = b->nets[i];
      key(w, n->name);
      begin_obj(w);
      key(w, "hide_name");
      fputs("0", w->f);
      key(w, "bits");
      bit_list_t bits = AINIT;
      net_bits(w, n, 0, n->width - 1, &bits);
      write_bits(w->f, bits.items, bits.count);
      ACLEAR(bits);
      key(w, "attributes");
      begin_obj(w);
      char *nsrc = src_string(n->src, n->line);
      write_attr_str(w, "src", nsrc);
      free(nsrc);
      if (n->record) {
         key(w, "record");
         fputs("1", w->f);
      }
      if (n->iface) {
         key(w, "interface");
         fputs("1", w->f);
      }
      if (w->evidence)
         write_user_attrs(w, n->attrs, n->nattrs);
      end_obj(w);
      end_obj(w);

      // Fields of a record as nets of their own
      for (unsigned j = 0; j < n->nfields; j++) {
         const conn_field_t *f = &(n->fields[j]);
         char *fname = xasprintf("%s%s", n->name, f->name);
         key(w, fname);
         begin_obj(w);
         key(w, "hide_name");
         fputs("0", w->f);
         key(w, "bits");
         bit_list_t fbits = AINIT;
         net_bits(w, n, f->lo, f->hi, &fbits);
         write_bits(w->f, fbits.items, fbits.count);
         ACLEAR(fbits);
         key(w, "attributes");
         begin_obj(w);
         key(w, "field");
         fputs("1", w->f);
         end_obj(w);
         end_obj(w);
         free(fname);
      }
   }
   end_obj(w);

   end_obj(w);

   free(w->base);
   w->base = NULL;

   // The name table borrows the instance labels
   shash_free(w->names);
   w->names = NULL;
   for (unsigned i = 0; i < w->owned.count; i++)
      free(w->owned.items[i]);
   ACLEAR(w->owned);
}

// An interface instance whose signals are all nets of the enclosing module
// would only be a box with dangling pins: it is not drawn unless it has
// processes or instances of its own
static bool hidden_interface(const conn_block_t *b)
{
   return b->interface && b->parent != NULL && b->nprocs == 0
      && b->ninsts == 0;
}

static int block_depth(const conn_block_t *b)
{
   int d = 0;
   for (; b->parent != NULL; b = b->parent)
      d++;
   return d;
}

typedef struct {
   wctx_t *w;
   int     max_depth;
} walk_ctx_t;

static void module_cb(conn_block_t *b, void *ctx)
{
   walk_ctx_t *wc = ctx;

   if (hidden_interface(b))
      return;

   if (b->blackbox && b->unit != NULL) {
      // One module for all the instances of the unit
      if (wc->w->boxes == NULL)
         wc->w->boxes = shash_new(16);
      else if (shash_get(wc->w->boxes, b->unit) != NULL)
         return;

      shash_put(wc->w->boxes, b->unit, (void *)b);
   }

   const int depth = block_depth(b);
   if (wc->max_depth >= 0 && depth > wc->max_depth)
      return;

   write_module(wc->w, b, b->parent == NULL);
}

void netlist_write(const conn_design_t *cd, const netlist_opts_t *opts,
                   FILE *f)
{
   wctx_t w = { .f = f, .first = true, .evidence = opts && opts->evidence };

   fputs("{", f);
   key(&w, "creator");
   json_string(f, "nvc --netlist");

   if (w.evidence) {
      key(&w, "evidence_version");
      fputs("1", f);
   }

   key(&w, "modules");
   begin_obj(&w);

   walk_ctx_t wc = {
      .w = &w,
      .max_depth = opts ? opts->max_depth : -1,
   };
   conn_walk(cd, module_cb, &wc);

   if (w.boxes != NULL)
      shash_free(w.boxes);

   end_obj(&w);
   fputs("\n}\n", f);
}
