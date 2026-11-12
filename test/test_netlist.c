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

#include "test_util.h"
#include "conn.h"
#include "netlist.h"
#include "option.h"
#include "rt/model.h"
#include "rt/structs.h"
#include "scan.h"
#include "tree.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

////////////////////////////////////////////////////////////////////////////////
// Helpers

static conn_inst_t *get_inst(conn_block_t *b, const char *label)
{
   for (unsigned i = 0; i < b->ninsts; i++) {
      if (strcmp(b->insts[i]->label, label) == 0)
         return b->insts[i];
   }

   ck_abort_msg("no instance %s in %s", label, b->name);
   return NULL;
}

static const conn_pconn_t *get_conn(const conn_inst_t *inst, const char *port)
{
   for (unsigned i = 0; i < inst->nconns; i++) {
      if (strcmp(inst->conns[i].formal->name, port) == 0)
         return &(inst->conns[i]);
   }

   ck_abort_msg("no connection for %s of %s", port, inst->label);
   return NULL;
}

static conn_proc_t *get_proc(conn_block_t *b, const char *name)
{
   for (unsigned i = 0; i < b->nprocs; i++) {
      if (strcmp(b->procs[i]->name, name) == 0)
         return b->procs[i];
   }

   ck_abort_msg("no process %s in %s", name, b->name);
   return NULL;
}

static conn_net_t *get_net(conn_block_t *b, const char *name)
{
   for (unsigned i = 0; i < b->nnets; i++) {
      if (strcmp(b->nets[i]->name, name) == 0)
         return b->nets[i];
   }

   ck_abort_msg("no net %s in %s", name, b->name);
   return NULL;
}

typedef struct {
   conn_end_t ends[16];
   unsigned   count;
} ends_t;

static void collect_cb(const conn_end_t *end, void *ctx)
{
   ends_t *e = ctx;
   ck_assert_uint_lt(e->count, ARRAY_LEN(e->ends));
   e->ends[e->count++] = *end;
}

static const conn_end_t *find_end(const ends_t *e, conn_end_kind_t kind,
                                  const char *name)
{
   for (unsigned i = 0; i < e->count; i++) {
      const conn_end_t *end = &(e->ends[i]);
      if (end->kind != kind)
         continue;

      switch (kind) {
      case CONN_END_PROC:
         if (strcmp(end->proc->name, name) == 0)
            return end;
         break;
      case CONN_END_CHILD:
      case CONN_END_EXPR:
         if (strcmp(end->inst->label, name) == 0)
            return end;
         break;
      case CONN_END_PARENT:
         if (strcmp(end->port->name, name) == 0)
            return end;
         break;
      }
   }

   return NULL;
}

// Minimal JSON syntax check: returns pointer after the value or NULL
static const char *json_skip_ws(const char *p)
{
   while (*p == ' ' || *p == '\n' || *p == '\t' || *p == '\r')
      p++;
   return p;
}

static const char *json_value(const char *p, int depth);

static const char *json_string(const char *p)
{
   if (*p != '"')
      return NULL;

   for (p++; *p != '"'; p++) {
      if (*p == '\0' || (unsigned char)*p < 0x20)
         return NULL;
      else if (*p == '\\')
         p++;
   }

   return p + 1;
}

static const char *json_value(const char *p, int depth)
{
   if (depth > 32)
      return NULL;

   p = json_skip_ws(p);

   if (*p == '{') {
      p = json_skip_ws(p + 1);
      if (*p == '}')
         return p + 1;

      for (;;) {
         p = json_skip_ws(p);
         if ((p = json_string(p)) == NULL)
            return NULL;
         p = json_skip_ws(p);
         if (*p++ != ':')
            return NULL;
         if ((p = json_value(p, depth + 1)) == NULL)
            return NULL;
         p = json_skip_ws(p);
         if (*p == ',')
            p++;
         else if (*p == '}')
            return p + 1;
         else
            return NULL;
      }
   }
   else if (*p == '[') {
      p = json_skip_ws(p + 1);
      if (*p == ']')
         return p + 1;

      for (;;) {
         if ((p = json_value(p, depth + 1)) == NULL)
            return NULL;
         p = json_skip_ws(p);
         if (*p == ',')
            p++;
         else if (*p == ']')
            return p + 1;
         else
            return NULL;
      }
   }
   else if (*p == '"')
      return json_string(p);
   else if ((*p >= '0' && *p <= '9') || *p == '-') {
      while ((*p >= '0' && *p <= '9') || *p == '-' || *p == '.')
         p++;
      return p;
   }
   else
      return NULL;
}

static char *write_json(const conn_design_t *cd, int max_depth)
{
   netlist_opts_t opts = { .max_depth = max_depth };

   FILE *f = tmpfile();
   ck_assert_ptr_nonnull(f);

   netlist_write(cd, &opts, f);

   const long size = ftell(f);
   rewind(f);

   char *buf = xmalloc(size + 1);
   ck_assert_uint_eq(fread(buf, 1, size, f), size);
   buf[size] = '\0';
   fclose(f);

   const char *end = json_value(buf, 0);
   ck_assert_msg(end != NULL && *json_skip_ws(end) == '\0',
                 "malformed JSON:\n%s", buf);

   return buf;
}

#define ck_has(buf, text) \
   ck_assert_msg(strstr((buf), (text)) != NULL, "missing '%s' in:\n%s", \
                 (text), (buf))
#define ck_has_not(buf, text) \
   ck_assert_msg(strstr((buf), (text)) == NULL, "unexpected '%s' in:\n%s", \
                 (text), (buf))

static unsigned count_substr(const char *buf, const char *text)
{
   unsigned n = 0;
   for (const char *p = buf; (p = strstr(p, text)) != NULL; p += strlen(text))
      n++;
   return n;
}

////////////////////////////////////////////////////////////////////////////////
// VHDL

START_TEST(test_vhdl1)
{
   set_standard(STD_08);
   input_from_file(TESTDIR "/netlist/vhdl1.vhd");

   tree_t e = run_elab();
   fail_if_errors();

   conn_design_t *cd = conn_build(e);
   ck_assert_ptr_nonnull(cd);

   ck_assert_uint_eq(conn_roots(cd), 1);
   conn_block_t *top = conn_root(cd, 0);
   ck_assert_str_eq(top->name, "VHDL1");
   ck_assert_int_eq(top->lang, CONN_VHDL);
   ck_assert_uint_eq(top->nports, 0);
   ck_assert_uint_eq(top->ninsts, 4);   // Generate blocks are inlined

   conn_net_t *q = get_net(top, "Q");
   ck_assert_uint_eq(q->width, 4);
   ck_assert(q->descending);
   ck_assert_int_eq(q->left, 3);
   ck_assert_int_eq(q->right, 0);
   conn_net_t *e_net = get_net(top, "E");
   ck_assert(!e_net->descending);
   ck_assert_uint_eq(get_net(top, "CLK")->width, 1);

   // Slice depending on the generate index
   conn_inst_t *g0 = get_inst(top, "G(0).U_LEAF");
   conn_inst_t *g1 = get_inst(top, "G(1).U_LEAF");
   ck_assert_str_eq(g0->child->name, "VHDL1.G(0).U_LEAF");
   ck_assert_uint_eq(g0->child->nports, 3);
   ck_assert_uint_eq(g0->child->nnets, 4);   // Ports and TMP

   const conn_pconn_t *a0 = get_conn(g0, "A");
   ck_assert_uint_eq(a0->nsegs, 1);
   ck_assert_int_eq(a0->segs[0].kind, CONN_SEG_NET);
   ck_assert_ptr_eq(a0->segs[0].range.net, q);
   ck_assert_uint_eq(a0->segs[0].range.lo, 2);   // Q(1 downto 0)
   ck_assert_uint_eq(a0->segs[0].range.hi, 3);
   ck_assert(!a0->segs[0].approx);

   const conn_pconn_t *a1 = get_conn(g1, "A");
   ck_assert_uint_eq(a1->segs[0].range.lo, 0);   // Q(3 downto 2)
   ck_assert_uint_eq(a1->segs[0].range.hi, 1);

   const conn_pconn_t *io = get_conn(g0, "IO");
   ck_assert_int_eq(io->formal->dir, CONN_INOUT);
   ck_assert_ptr_eq(io->segs[0].range.net, get_net(top, "X"));

   // Constant and open
   conn_inst_t *uc = get_inst(top, "U_CONST");
   const conn_pconn_t *ca = get_conn(uc, "A");
   ck_assert_uint_eq(ca->nsegs, 1);
   ck_assert_int_eq(ca->segs[0].kind, CONN_SEG_CONST);
   ck_assert_str_eq(ca->segs[0].bits, "1010");
   const conn_pconn_t *cb = get_conn(uc, "B");
   ck_assert_int_eq(cb->segs[0].kind, CONN_SEG_OPEN);
   ck_assert_uint_eq(cb->segs[0].width, 4);

   // Expression
   conn_inst_t *ue = get_inst(top, "U_EXPR");
   const conn_pconn_t *ea = get_conn(ue, "A");
   ck_assert_int_eq(ea->segs[0].kind, CONN_SEG_EXPR);
   ck_assert_uint_eq(ea->segs[0].width, 4);
   ck_assert_uint_eq(ea->segs[0].nreads, 1);
   ck_assert_ptr_eq(ea->segs[0].reads[0].net, get_net(top, "D"));
   ck_assert_str_eq(ea->segs[0].text, "D and \"0011\"");

   // Process sets
   conn_proc_t *pc = get_proc(top, "P_COUNT");
   ck_assert_int_eq(pc->kind, CONN_PROC_VHDL);
   ck_assert_uint_eq(pc->nreads, 2);
   ck_assert_uint_eq(pc->ndrives, 1);
   ck_assert_ptr_eq(pc->drives[0].net, q);
   ck_assert_uint_eq(pc->drives[0].lo, 0);   // Q(3 downto 2)
   ck_assert_uint_eq(pc->drives[0].hi, 1);
   ck_assert_ptr_nonnull(pc->src);
   ck_assert_uint_eq(pc->line, 60);
   ck_assert(!pc->approx);

   conn_proc_t *po = get_proc(g1->child, "P_OUT");
   ck_assert_uint_eq(po->nreads, 1);
   ck_assert_str_eq(po->reads[0].net->name, "TMP");
   ck_assert_uint_eq(po->ndrives, 1);
   ck_assert_str_eq(po->drives[0].net->name, "B");

   // Lookup by path
   ck_assert_ptr_eq(conn_find_net(cd, "vhdl1.q"), q);
   ck_assert_ptr_eq(conn_find_net(cd, ":VHDL1:Q"), q);
   ck_assert_ptr_eq(conn_find_block(cd, "vhdl1.g(1).u_leaf"), g1->child);
   ck_assert_ptr_eq(conn_find_net(cd, "vhdl1.G(1).U_LEAF.TMP"),
                    get_net(g1->child, "TMP"));
   ck_assert_ptr_eq(conn_find_net(cd, ":vhdl1:u_expr:a"),
                    get_net(ue->child, "A"));
   ck_assert_ptr_null(conn_find_net(cd, "vhdl1.nothere"));
   ck_assert_ptr_null(conn_find_net(cd, "other.q"));
   ck_assert_str_eq(conn_net_path(cd, q), "VHDL1.Q");

   conn_free(cd);
}
END_TEST

START_TEST(test_m5_vhdl_params)
{
   // Generics of each instance with the value it was elaborated with, in
   // declaration order and including those left at their default

   set_standard(STD_08);
   input_from_file(TESTDIR "/netlist/m5v_params.vhd");

   tree_t e = run_elab();
   fail_if_errors();

   conn_design_t *cd = conn_build(e);
   ck_assert_ptr_nonnull(cd);

   conn_block_t *top = conn_root(cd, 0);
   ck_assert_uint_eq(top->nparams, 0);

   conn_block_t *u1 = get_inst(top, "U1")->child;
   ck_assert_uint_eq(u1->nparams, 10);
   ck_assert_str_eq(u1->params[0].name, "W");
   ck_assert_str_eq(u1->params[0].value, "8");
   ck_assert_str_eq(u1->params[1].name, "NAME");
   ck_assert_str_eq(u1->params[1].value, "\"def\"");
   ck_assert_str_eq(u1->params[2].value, "0.5");
   ck_assert_str_eq(u1->params[3].value, "true");
   ck_assert_str_eq(u1->params[4].value, "green");
   ck_assert_str_eq(u1->params[5].value, "\"1010\"");
   ck_assert_str_eq(u1->params[6].value, "10 ns");
   ck_assert_str_eq(u1->params[7].value, "'x'");
   ck_assert_str_eq(u1->params[9].name, "NEG");
   ck_assert_str_eq(u1->params[9].value, "-1");

   // Values that depend on the generate index
   conn_block_t *g0 = get_inst(top, "G(0).U2")->child;
   conn_block_t *g1 = get_inst(top, "G(1).U2")->child;
   ck_assert_str_eq(g0->params[0].value, "5");
   ck_assert_str_eq(g1->params[0].value, "6");
   ck_assert_str_eq(g0->params[1].value, "\"xy\"");
   ck_assert_str_eq(g0->params[2].value, "1e-09");
   ck_assert_str_eq(g0->params[3].value, "false");
   ck_assert_str_eq(g0->params[4].value, "blue");
   ck_assert_str_eq(g0->params[5].value, "\"0011\"");
   ck_assert_str_eq(g0->params[6].value, "2 us");
   ck_assert_str_eq(g0->params[8].name, "IV");
   ck_assert_str_eq(g0->params[8].value, "(i, i + 1)");   // As written
   ck_assert_str_eq(g0->params[9].value, "0");
   ck_assert_str_eq(g1->params[9].value, "-1");

   // Generic of an if generate
   conn_block_t *u3 = get_inst(top, "C.U3")->child;
   ck_assert_str_eq(u3->params[0].value, "6");
   ck_assert_str_eq(u3->params[1].value, "\"def\"");

   // A component bound to an entity shows the generics of the entity
   conn_block_t *uc = get_inst(top, "UC")->child;
   ck_assert_uint_eq(uc->nparams, 10);
   ck_assert_str_eq(uc->params[0].value, "8");

   // Passed down two levels
   conn_block_t *um = get_inst(top, "UM")->child;
   ck_assert_uint_eq(um->nparams, 2);
   ck_assert_str_eq(um->params[0].value, "3");
   ck_assert_str_eq(um->params[1].value, "\"mid\"");
   conn_block_t *umu = get_inst(um, "U")->child;
   ck_assert_str_eq(umu->params[0].value, "6");
   ck_assert_str_eq(umu->params[1].value, "\"mid!\"");

   char *json = write_json(cd, 1);
   ck_has(json, "\"type\":\"M5V_TOP.G(0).U2\"");
   ck_has(json, "\"parameters\":{\n\"W\":\"5\",\n\"NAME\":\"\\\"xy\\\"\"");
   ck_has(json, "\"M5V_TOP.G(1).U2\":{");
   free(json);

   // At the depth cut the cell still has them
   json = write_json(cd, 0);
   ck_has_not(json, "\"M5V_TOP.G(0).U2\":{");
   ck_has(json, "\"parameters\":{\n\"W\":\"5\",");
   free(json);

   conn_free(cd);
}
END_TEST

START_TEST(test_m5_vhdl_mixed)
{
   // Generics of a VHDL entity instantiated from Verilog with parameters,
   // in an instance array and a generate loop, and of a component bound to
   // a Verilog module under VHDL

   set_standard(STD_08);

   input_from_file(TESTDIR "/netlist/vhdl1.vhd");
   analyse_file();

   input_from_file(TESTDIR "/netlist/mixed1.v");
   analyse_file();

   input_from_file(TESTDIR "/netlist/mixed1.vhd");
   analyse_file();

   input_from_file(TESTDIR "/netlist/mixed3.v");

   tree_t e = run_elab();
   fail_if_errors();

   conn_design_t *cd = conn_build(e);
   ck_assert_ptr_nonnull(cd);

   conn_block_t *top = conn_root(cd, 0);

   const char *labels[] = { "ua[0]", "ua[1]", "g[0].u", "g[1].u" };
   for (int i = 0; i < ARRAY_LEN(labels); i++) {
      conn_block_t *b = get_inst(top, labels[i])->child;
      ck_assert_int_eq(b->lang, CONN_VHDL);
      ck_assert_uint_eq(b->nparams, 1);
      ck_assert_str_eq(b->params[0].name, "N");
      ck_assert_str_eq(b->params[0].value, "4");
   }

   conn_block_t *mid = get_inst(top, "u_mid")->child;
   ck_assert_uint_eq(mid->nparams, 0);

   conn_free(cd);
}
END_TEST



START_TEST(test_vhdl1_query)
{
   set_standard(STD_08);
   input_from_file(TESTDIR "/netlist/vhdl1.vhd");

   tree_t e = run_elab();
   fail_if_errors();

   conn_design_t *cd = conn_build(e);
   ck_assert_ptr_nonnull(cd);

   conn_block_t *top = conn_root(cd, 0);
   conn_net_t *q = get_net(top, "Q");
   conn_net_t *d = get_net(top, "D");

   // Q: driven by P_COUNT for bits 3 downto 2 only
   ends_t ends = {};
   ck_assert_uint_eq(conn_drivers(cd, q, 0, 3, collect_cb, &ends), 1);
   const conn_end_t *end = find_end(&ends, CONN_END_PROC, "P_COUNT");
   ck_assert_ptr_nonnull(end);
   ck_assert_uint_eq(end->lo, 0);
   ck_assert_uint_eq(end->hi, 1);
   ck_assert_uint_eq(end->line, 60);

   // Nothing drives the other half
   ends = (ends_t){};
   ck_assert_uint_eq(conn_drivers(cd, q, 2, 3, collect_cb, &ends), 0);

   // Readers of Q: the two leaves through port A and the process
   // for LO.  Only the low half is read by the process
   ends = (ends_t){};
   ck_assert_uint_eq(conn_readers(cd, q, 0, 3, collect_cb, &ends), 3);
   end = find_end(&ends, CONN_END_CHILD, "G(0).U_LEAF");
   ck_assert_ptr_nonnull(end);
   ck_assert_str_eq(end->port->name, "A");
   ck_assert_uint_eq(end->lo, 2);
   ck_assert_uint_eq(end->hi, 3);
   ck_assert_uint_eq(end->other.lo, 0);
   ck_assert_uint_eq(end->other.hi, 1);
   ck_assert_ptr_eq(end->other.net, end->port);
   end = find_end(&ends, CONN_END_CHILD, "G(1).U_LEAF");
   ck_assert_ptr_nonnull(end);
   ck_assert_uint_eq(end->lo, 0);
   ck_assert_uint_eq(end->hi, 1);

   // Only the second leaf reads Q(3 downto 2)
   ends = (ends_t){};
   ck_assert_uint_eq(conn_readers(cd, q, 0, 1, collect_cb, &ends), 1);
   ck_assert_ptr_nonnull(find_end(&ends, CONN_END_CHILD, "G(1).U_LEAF"));

   // D is driven by the output ports of the leaves and read by an
   // expression and the process
   ends = (ends_t){};
   ck_assert_uint_eq(conn_drivers(cd, d, 0, 3, collect_cb, &ends), 2);
   ck_assert_ptr_nonnull(find_end(&ends, CONN_END_CHILD, "G(0).U_LEAF"));
   ck_assert_ptr_nonnull(find_end(&ends, CONN_END_CHILD, "G(1).U_LEAF"));

   ends = (ends_t){};
   ck_assert_uint_eq(conn_readers(cd, d, 0, 3, collect_cb, &ends), 2);
   ck_assert_ptr_nonnull(find_end(&ends, CONN_END_PROC, "P_COUNT"));
   end = find_end(&ends, CONN_END_EXPR, "U_EXPR");
   ck_assert_ptr_nonnull(end);
   ck_assert_str_eq(end->port->name, "A");

   // From inside a leaf: the driver of input A is the connection in
   // the parent, which names Q
   conn_block_t *leaf = get_inst(top, "G(0).U_LEAF")->child;
   conn_net_t *a = get_net(leaf, "A");
   ends = (ends_t){};
   ck_assert_uint_eq(conn_drivers(cd, a, 0, 1, collect_cb, &ends), 1);
   end = find_end(&ends, CONN_END_PARENT, "A");
   ck_assert_ptr_nonnull(end);
   ck_assert_ptr_eq(end->block, top);
   ck_assert_ptr_eq(end->other.net, q);
   ck_assert_uint_eq(end->other.lo, 2);
   ck_assert_uint_eq(end->other.hi, 3);
   ck_assert_ptr_nonnull(strstr(conn_end_describe(cd, end),
                                "connection of port A in VHDL1 ("));
   ck_assert_ptr_nonnull(strstr(conn_end_describe(cd, end),
                                "/netlist/vhdl1.vhd:45)"));

   // ... and the reader is the process inside
   ends = (ends_t){};
   ck_assert_uint_eq(conn_readers(cd, a, 0, 1, collect_cb, &ends), 1);
   ck_assert_ptr_nonnull(find_end(&ends, CONN_END_PROC, "process @14"));

   // Output B is read by the parent connection (D) and driven by the process
   conn_net_t *b = get_net(leaf, "B");
   ends = (ends_t){};
   ck_assert_uint_eq(conn_readers(cd, b, 0, 1, collect_cb, &ends), 1);
   end = find_end(&ends, CONN_END_PARENT, "B");
   ck_assert_ptr_nonnull(end);
   ck_assert_ptr_eq(end->other.net, d);
   ends = (ends_t){};
   ck_assert_uint_eq(conn_drivers(cd, b, 0, 1, collect_cb, &ends), 1);
   ck_assert_ptr_nonnull(find_end(&ends, CONN_END_PROC, "P_OUT"));

   // The inout port of a leaf is driven by its process and by the
   // connection of X in the parent which has several instances
   conn_net_t *x = get_net(top, "X");
   ends = (ends_t){};
   ck_assert_uint_eq(conn_drivers(cd, x, 0, 0, collect_cb, &ends), 2);

   conn_free(cd);
}
END_TEST

START_TEST(test_vhdl1_json)
{
   set_standard(STD_08);
   input_from_file(TESTDIR "/netlist/vhdl1.vhd");

   tree_t e = run_elab();
   fail_if_errors();

   conn_design_t *cd = conn_build(e);
   ck_assert_ptr_nonnull(cd);

   char *json = write_json(cd, -1);

   ck_has(json, "\"VHDL1\":{");
   ck_has(json, "\"VHDL1.G(0).U_LEAF\":{");
   ck_has(json, "\"VHDL1.G(1).U_LEAF\":{");
   ck_has(json, "\"VHDL1.U_CONST\":{");
   ck_has(json, "\"VHDL1.U_EXPR\":{");
   ck_has(json, "\"top\":1");
   ck_has(json, "\"type\":\"VHDL1.G(0).U_LEAF\"");
   ck_has(json, "\"cell\":\"$process\"");
   ck_has(json, "\"cell\":\"$expr\"");
   ck_has(json, "\"label\":\"P_COUNT\"");
   ck_has(json, "\"IO\":\"inout\"");
   ck_has(json, "\"language\":\"vhdl\"");

   // Constant actual, least significant bit first
   ck_has(json, "\"A\":[\"0\",\"1\",\"0\",\"1\"]");

   // Q(1 downto 0) of the first leaf is Q[0], Q[1] and Q(3 downto 2) of
   // the second; Q is "Q":[bits] with bit 0 first
   ck_has(json, "\"Q[3:2]\":");
   ck_has(json, "\"process @");

   ck_assert_uint_eq(count_substr(json, "\"top\":1"), 1);

   free(json);

   // Depth limit
   json = write_json(cd, 0);
   ck_has(json, "\"VHDL1\":{");
   ck_has_not(json, "\"VHDL1.G(0).U_LEAF\":{");
   ck_has(json, "\"type\":\"VHDL1.G(0).U_LEAF\"");   // Still a cell
   free(json);

   json = write_json(cd, 1);
   ck_has(json, "\"VHDL1.G(0).U_LEAF\":{");
   free(json);

   conn_free(cd);
}
END_TEST

START_TEST(test_runtime)
{
   // The elaborated tree is available to a running simulation as the
   // `where` of the root scope
   set_standard(STD_08);
   input_from_file(TESTDIR "/netlist/vhdl1.vhd");

   rt_model_t *m = model_new(get_jit(), NULL);
   tree_t e = run_elab_with_model(m);
   fail_if_errors();
   ck_assert_ptr_nonnull(e);

   rt_scope_t *root = root_scope(m);
   ck_assert_ptr_eq(root->where, e);

   conn_design_t *cd = conn_build(root->where);
   ck_assert_ptr_nonnull(cd);

   // Query by the path of a signal as the model names it
   conn_net_t *q = conn_find_net(cd, ":vhdl1:q");
   ck_assert_ptr_nonnull(q);

   ends_t ends = {};
   ck_assert_uint_eq(conn_drivers(cd, q, 0, 3, collect_cb, &ends), 1);
   ck_assert_ptr_nonnull(find_end(&ends, CONN_END_PROC, "P_COUNT"));

   conn_free(cd);
   model_free(m);
}
END_TEST

////////////////////////////////////////////////////////////////////////////////
// Verilog

START_TEST(test_sv1)
{
   input_from_file(TESTDIR "/netlist/sv1.sv");

   tree_t e = run_elab();
   fail_if_errors();

   conn_design_t *cd = conn_build(e);
   ck_assert_ptr_nonnull(cd);

   ck_assert_uint_eq(conn_roots(cd), 1);
   conn_block_t *top = conn_root(cd, 0);
   ck_assert_str_eq(top->name, "sv1");
   ck_assert_int_eq(top->lang, CONN_VERILOG);
   ck_assert_uint_eq(top->ninsts, 5);

   conn_net_t *w = get_net(top, "w");
   ck_assert_uint_eq(w->width, 8);
   ck_assert(w->descending);

   conn_inst_t *i0 = get_inst(top, "i0");
   ck_assert_uint_eq(i0->child->nports, 3);
   ck_assert_int_eq(get_net(i0->child, "a")->dir, CONN_IN);
   ck_assert_int_eq(get_net(i0->child, "y")->dir, CONN_OUT);
   ck_assert_int_eq(get_net(i0->child, "io")->dir, CONN_INOUT);
   ck_assert_uint_eq(get_net(i0->child, "a")->width, 4);

   // w[3:0] is the low half: positions 4 to 7 as position 0 is the MSB
   const conn_pconn_t *a = get_conn(i0, "a");
   ck_assert_uint_eq(a->nsegs, 1);
   ck_assert_ptr_eq(a->segs[0].range.net, w);
   ck_assert_uint_eq(a->segs[0].range.lo, 4);
   ck_assert_uint_eq(a->segs[0].range.hi, 7);
   ck_assert_ptr_eq(get_conn(i0, "y")->segs[0].range.net,
                    get_net(top, "lo"));
   ck_assert_ptr_eq(get_conn(i0, "io")->segs[0].range.net,
                    get_net(top, "t"));

   // Concatenation of single bits is four segments
   const conn_pconn_t *a1 = get_conn(get_inst(top, "i1"), "a");
   ck_assert_uint_eq(a1->nsegs, 4);
   ck_assert_uint_eq(a1->segs[0].range.lo, 0);   // w[7]
   ck_assert_uint_eq(a1->segs[3].range.lo, 3);   // w[4]

   // Constant
   const conn_pconn_t *a2 = get_conn(get_inst(top, "i2"), "a");
   ck_assert_int_eq(a2->segs[0].kind, CONN_SEG_CONST);
   ck_assert_str_eq(a2->segs[0].bits, "1010");

   // An empty connection leaves the port without an entry
   conn_inst_t *i2 = get_inst(top, "i2");
   for (unsigned i = 0; i < i2->nconns; i++)
      ck_assert_str_ne(i2->conns[i].formal->name, "y");

   // Indexed part select in a generate loop
   const conn_pconn_t *g0 = get_conn(get_inst(top, "gg[0].ig"), "a");
   ck_assert_uint_eq(g0->segs[0].range.lo, 4);   // w[0 +: 4] is w[3:0]
   ck_assert_uint_eq(g0->segs[0].range.hi, 7);
   const conn_pconn_t *g1 = get_conn(get_inst(top, "gg[1].ig"), "a");
   ck_assert_uint_eq(g1->segs[0].range.lo, 3);   // w[1 +: 4] is w[4:1]
   ck_assert_uint_eq(g1->segs[0].range.hi, 6);
   ck_assert(!g1->segs[0].approx);

   // Processes
   conn_proc_t *ff = get_proc(top, "always_ff @15");
   ck_assert_int_eq(ff->kind, CONN_PROC_ALWAYS_FF);
   ck_assert_uint_eq(ff->nreads, 2);   // clk and w[3:0]
   ck_assert_uint_eq(ff->ndrives, 1);
   ck_assert_ptr_eq(ff->drives[0].net, get_net(top, "r"));
   ck_assert_uint_eq(ff->line, 15);
   ck_assert(!ff->approx);

   conn_proc_t *as = get_proc(i0->child, "assign @3");
   ck_assert_int_eq(as->kind, CONN_PROC_ASSIGN);
   ck_assert_uint_eq(as->nreads, 1);
   ck_assert_str_eq(as->reads[0].net->name, "a");
   ck_assert_uint_eq(as->ndrives, 1);
   ck_assert_str_eq(as->drives[0].net->name, "y");

   // Queries
   ends_t ends = {};
   conn_net_t *lo = get_net(top, "lo");
   ck_assert_uint_eq(conn_drivers(cd, lo, 0, 3, collect_cb, &ends), 1);
   const conn_end_t *end = find_end(&ends, CONN_END_CHILD, "i0");
   ck_assert_ptr_nonnull(end);
   ck_assert_str_eq(end->port->name, "y");

   ends = (ends_t){};
   ck_assert_uint_eq(conn_readers(cd, w, 4, 4, collect_cb, &ends), 4);
   ck_assert_ptr_nonnull(find_end(&ends, CONN_END_PROC, "always_ff @15"));
   ck_assert_ptr_nonnull(find_end(&ends, CONN_END_CHILD, "i0"));
   ck_assert_ptr_nonnull(find_end(&ends, CONN_END_CHILD, "gg[0].ig"));
   ck_assert_ptr_nonnull(find_end(&ends, CONN_END_CHILD, "gg[1].ig"));

   // Inside the instance, the input is driven from w[3:0] in the parent
   ends = (ends_t){};
   ck_assert_uint_eq(conn_drivers(cd, get_net(i0->child, "a"), 0, 3,
                                  collect_cb, &ends), 1);
   end = find_end(&ends, CONN_END_PARENT, "a");
   ck_assert_ptr_nonnull(end);
   ck_assert_ptr_eq(end->other.net, w);
   ck_assert_uint_eq(end->other.lo, 4);

   ck_assert_ptr_eq(conn_find_net(cd, "sv1.gg[1].ig.a"),
                    get_net(get_inst(top, "gg[1].ig")->child, "a"));

   char *json = write_json(cd, -1);
   ck_has(json, "\"sv1.gg[1].ig\":{");
   ck_has(json, "\"language\":\"verilog\"");
   ck_has(json, "\"kind\":\"always_ff\"");
   ck_has(json, "\"kind\":\"assign\"");
   ck_has(json, "\"a\":[\"0\",\"1\",\"0\",\"1\"]");
   ck_has(json, "\"w[3:0]\":");
   free(json);

   conn_free(cd);
}
END_TEST

START_TEST(test_vhdl2)
{
   set_standard(STD_08);
   input_from_file(TESTDIR "/netlist/vhdl2.vhd");

   tree_t e = run_elab();
   fail_if_errors();

   conn_design_t *cd = conn_build(e);
   ck_assert_ptr_nonnull(cd);

   conn_block_t *top = conn_root(cd, 0);

   // A record is one net with its fields kept for display
   conn_net_t *r = get_net(top, "R");
   ck_assert_uint_eq(r->width, 5);
   ck_assert(r->record);
   ck_assert_uint_eq(r->nfields, 2);
   ck_assert_str_eq(r->fields[0].name, ".A");
   ck_assert_uint_eq(r->fields[0].lo, 0);
   ck_assert_uint_eq(r->fields[0].hi, 3);
   ck_assert_str_eq(r->fields[1].name, ".B");
   ck_assert_uint_eq(r->fields[1].lo, 4);

   conn_net_t *arr = get_net(top, "ARR");
   ck_assert_uint_eq(arr->width, 10);
   ck_assert_ptr_nonnull(arr->fields);

   // Port of record type connected to a whole record and to an element
   // of an array of records
   const conn_pconn_t *p0 = get_conn(get_inst(top, "U0"), "P");
   ck_assert_ptr_eq(p0->segs[0].range.net, r);
   ck_assert_uint_eq(p0->segs[0].range.hi, 4);
   const conn_pconn_t *p1 = get_conn(get_inst(top, "U1"), "P");
   ck_assert_ptr_eq(p1->segs[0].range.net, arr);
   ck_assert_uint_eq(p1->segs[0].range.lo, 0);
   ck_assert_uint_eq(p1->segs[0].range.hi, 4);

   // Assignment to a field is a partial driver
   ends_t ends = {};
   ck_assert_uint_eq(conn_drivers(cd, r, 0, 4, collect_cb, &ends), 2);
   ends = (ends_t){};
   ck_assert_uint_eq(conn_drivers(cd, r, 4, 4, collect_cb, &ends), 1);

   char *json = write_json(cd, -1);
   ck_has(json, "\"R.A\":{");
   ck_has(json, "\"ARR(1).B\":{");
   ck_has(json, "\"R/B\":\"output\"");
   free(json);

   conn_free(cd);
}
END_TEST

START_TEST(test_iface1)
{
   input_from_file(TESTDIR "/netlist/iface1.sv");

   tree_t e = run_elab();
   fail_if_errors();

   conn_design_t *cd = conn_build(e);
   ck_assert_ptr_nonnull(cd);

   conn_block_t *top = conn_root(cd, 0);
   ck_assert_uint_eq(top->ninsts, 6);

   // An interface instance has all of its signals as ports
   conn_inst_t *bi0 = get_inst(top, "bi0");
   ck_assert(bi0->child->interface);
   ck_assert_uint_eq(bi0->child->nports, 3);   // clk, data and valid
   ck_assert(!get_net(bi0->child, "clk")->iface);
   ck_assert(get_net(bi0->child, "data")->iface);

   // ... and nets of the enclosing block with the instance as prefix
   conn_net_t *data0 = get_net(top, "bi0.data");
   conn_net_t *data1 = get_net(top, "bi1.data");
   ck_assert_uint_eq(data0->width, 8);
   ck_assert(data0->iface);
   ck_assert_ptr_eq(get_conn(bi0, "data")->segs[0].range.net, data0);
   ck_assert_ptr_eq(get_conn(bi0, "clk")->segs[0].range.net,
                    get_net(top, "clk"));

   // A module with an interface port has a port for each signal in the
   // modport, with the direction it gives
   conn_inst_t *p0 = get_inst(top, "p0"), *p1 = get_inst(top, "p1");
   conn_inst_t *c0 = get_inst(top, "c0");
   ck_assert_uint_eq(p0->child->nports, 3);
   ck_assert_int_eq(get_net(p0->child, "b.data")->dir, CONN_OUT);
   ck_assert_int_eq(get_net(p0->child, "b.clk")->dir, CONN_IN);
   ck_assert_int_eq(get_net(c0->child, "b.data")->dir, CONN_IN);
   ck_assert_uint_eq(c0->child->nports, 4);   // And o

   // Each instance is connected to its own interface instance although
   // the declarations are shared
   ck_assert_ptr_eq(get_conn(p0, "b.data")->segs[0].range.net, data0);
   ck_assert_ptr_eq(get_conn(p1, "b.data")->segs[0].range.net, data1);
   ck_assert_ptr_eq(get_conn(get_inst(top, "c1"), "b.valid")->segs[0].range.net,
                    get_net(top, "bi1.valid"));
   ck_assert_ptr_eq(get_conn(p1, "b.clk")->segs[0].range.net,
                    get_net(top, "clk"));

   // The process in the module reads and writes through the port
   conn_proc_t *pp = get_proc(p0->child, "always @14");
   ck_assert_uint_eq(pp->ndrives, 2);
   ck_assert_ptr_eq(pp->drives[0].net, get_net(p0->child, "b.data"));
   ck_assert_ptr_eq(pp->drives[1].net, get_net(p0->child, "b.valid"));
   ck_assert_uint_eq(pp->nreads, 2);   // b.clk and b.data

   // A hierarchical name in the parent reads the signal of each instance
   conn_proc_t *as = get_proc(top, "assign @36");
   ck_assert_uint_eq(as->nreads, 2);
   ck_assert_ptr_eq(as->reads[0].net, get_net(top, "bi0.valid"));
   ck_assert_ptr_eq(as->reads[1].net, get_net(top, "bi1.valid"));

   // Packed structure
   conn_net_t *hdr = get_net(top, "hdr");
   ck_assert_uint_eq(hdr->width, 5);
   ck_assert_uint_eq(hdr->nfields, 2);
   ck_assert_str_eq(hdr->fields[0].name, ".kind");
   ck_assert_uint_eq(hdr->fields[0].hi, 3);
   ck_assert_str_eq(hdr->fields[1].name, ".last");
   ck_assert_ptr_eq(as->drives[0].net, hdr);
   ck_assert_uint_eq(as->drives[0].lo, 4);

   // Queries see the producer, the consumer and the interface instance
   ends_t ends = {};
   ck_assert_uint_eq(conn_drivers(cd, data0, 0, 7, collect_cb, &ends), 2);
   ck_assert_ptr_nonnull(find_end(&ends, CONN_END_CHILD, "p0"));
   ck_assert_ptr_nonnull(find_end(&ends, CONN_END_CHILD, "bi0"));

   ends = (ends_t){};
   ck_assert_uint_eq(conn_readers(cd, data0, 0, 7, collect_cb, &ends), 2);
   ck_assert_ptr_nonnull(find_end(&ends, CONN_END_CHILD, "c0"));

   // Inside the producer: the process drives the port, read by the parent
   ends = (ends_t){};
   ck_assert_uint_eq(conn_drivers(cd, get_net(p0->child, "b.data"), 0, 7,
                                  collect_cb, &ends), 1);
   ck_assert_ptr_nonnull(find_end(&ends, CONN_END_PROC, "always @14"));
   ends = (ends_t){};
   ck_assert_uint_eq(conn_readers(cd, get_net(p0->child, "b.data"), 0, 7,
                                  collect_cb, &ends), 2);
   const conn_end_t *end = find_end(&ends, CONN_END_PARENT, "b.data");
   ck_assert_ptr_nonnull(end);
   ck_assert_ptr_eq(end->other.net, data0);

   ck_assert_ptr_eq(conn_find_net(cd, "iface1.bi1.data"), data1);

   char *json = write_json(cd, -1);
   ck_has_not(json, "\"iface1.bi0\":{");   // Only nets of the parent
   ck_has(json, "\"b/data\":\"output\"");
   ck_has(json, "\"hdr.kind\":{");
   ck_has(json, "\"interface\":1");
   free(json);

   conn_free(cd);
}
END_TEST

START_TEST(test_mixed_vlog_top)
{
   // The wrapper elaboration puts around a VHDL entity instantiated from
   // Verilog and around a Verilog module bound to a component is merged
   // with the unit inside: one block for each instance

   set_standard(STD_08);

   input_from_file(TESTDIR "/netlist/vhdl1.vhd");
   analyse_file();

   input_from_file(TESTDIR "/netlist/mixed1.v");
   analyse_file();

   input_from_file(TESTDIR "/netlist/mixed1.vhd");
   analyse_file();

   input_from_file(TESTDIR "/netlist/mixed3.v");

   tree_t e = run_elab();
   fail_if_errors();

   conn_design_t *cd = conn_build(e);
   ck_assert_ptr_nonnull(cd);

   conn_block_t *top = conn_root(cd, 0);
   ck_assert_str_eq(top->name, "mixed3");
   ck_assert_uint_eq(top->ninsts, 5);

   // Instance array element and generate loop
   const char *labels[] = { "ua[0]", "ua[1]", "g[0].u", "g[1].u" };
   for (int i = 0; i < ARRAY_LEN(labels); i++) {
      conn_inst_t *u = get_inst(top, labels[i]);
      conn_block_t *b = u->child;
      ck_assert_str_eq(b->label, labels[i]);
      ck_assert_str_eq(b->unit, "LEAF");
      ck_assert_int_eq(b->lang, CONN_VHDL);
      ck_assert_int_eq(b->label_lang, CONN_VERILOG);
      ck_assert_uint_eq(b->ninsts, 0);
      ck_assert_uint_eq(b->nports, 3);
      ck_assert_ptr_nonnull(get_proc(b, "P_OUT"));
      ck_assert_uint_eq(u->nconns, 3);
      ck_assert_int_eq(get_conn(u, "A")->segs[0].kind, CONN_SEG_NET);
      if (labels[i][0] == 'g')
         ck_assert_ptr_eq(get_conn(u, "A")->segs[0].range.net,
                          get_net(top, "a"));
      else
         ck_assert_ptr_nonnull(get_conn(u, "A"));

      // The instantiation is the source location of the instance
      ck_assert_ptr_nonnull(strstr(u->src, "mixed3.v"));
      ck_assert_ptr_nonnull(strstr(b->src, "vhdl1.vhd"));

      char *path LOCAL = xasprintf("mixed3.%s", labels[i]);
      ck_assert_ptr_eq(conn_find_block(cd, path), b);
      char *inner LOCAL = xasprintf("%s.LEAF", path);
      ck_assert_ptr_null(conn_find_block(cd, inner));
   }

   // Verilog above VHDL above a component bound to a Verilog module
   conn_inst_t *mid = get_inst(top, "u_mid");
   ck_assert_str_eq(mid->child->unit, "MIXED1");
   ck_assert_uint_eq(mid->child->ninsts, 1);

   conn_inst_t *sub = get_inst(mid->child, "U_SUB");
   ck_assert_str_eq(sub->child->name, "mixed3.u_mid.U_SUB");
   ck_assert_str_eq(sub->child->unit, "vsub");
   ck_assert_int_eq(sub->child->lang, CONN_VERILOG);
   ck_assert_int_eq(sub->child->label_lang, CONN_VHDL);
   ck_assert(!sub->child->component);
   ck_assert_uint_eq(sub->child->ninsts, 0);
   ck_assert_ptr_nonnull(get_net(sub->child, "clk"));
   ck_assert_ptr_eq(get_conn(sub, "clk")->segs[0].range.net,
                    get_net(mid->child, "CLK"));
   ck_assert_ptr_nonnull(get_conn(sub, "d")->segs[0].conv);

   char *json = write_json(cd, -1);
   ck_has(json, "\"mixed3.ua[0]\":{");
   ck_has(json, "\"mixed3.g[1].u\":{");
   ck_has(json, "\"mixed3.u_mid.U_SUB\":{");
   ck_has(json, "\"conversions\":\"TO_VHDL,TO_VERILOG\"");
   ck_has(json, "\"unit\":\"LEAF\"");
   ck_has_not(json, "leaf$");
   ck_has_not(json, ".LEAF\":{");
   ck_has_not(json, "\"mixed3.u_mid.U_SUB.vsub\"");
   free(json);

   conn_free(cd);
}
END_TEST

START_TEST(test_mixed_vhdl_top)
{
   set_standard(STD_08);

   input_from_file(TESTDIR "/netlist/mixed1.v");
   analyse_file();

   input_from_file(TESTDIR "/netlist/mixed1.vhd");

   tree_t e = run_elab();
   fail_if_errors();

   conn_design_t *cd = conn_build(e);
   ck_assert_ptr_nonnull(cd);

   conn_block_t *top = conn_root(cd, 0);
   ck_assert_int_eq(top->lang, CONN_VHDL);
   ck_assert_uint_eq(top->ninsts, 1);

   conn_inst_t *sub = get_inst(top, "U_SUB");
   ck_assert_str_eq(sub->child->name, "MIXED1.U_SUB");
   ck_assert_str_eq(sub->child->unit, "vsub");
   ck_assert_int_eq(sub->child->lang, CONN_VERILOG);
   ck_assert_uint_eq(sub->child->nports, 4);
   ck_assert_ptr_nonnull(get_proc(sub->child, "always @5"));
   ck_assert_ptr_eq(conn_find_block(cd, "MIXED1.U_SUB"), sub->child);
   ck_assert_ptr_null(conn_find_block(cd, "MIXED1.U_SUB.vsub"));

   conn_free(cd);
}
END_TEST

static void check_param(conn_block_t *b, const char *name,
                        const char *value)
{
   for (unsigned i = 0; i < b->nparams; i++) {
      if (strcmp(b->params[i].name, name) == 0) {
         ck_assert_str_eq(b->params[i].value, value);
         return;
      }
   }

   ck_abort_msg("no parameter %s in %s", name, b->name);
}

START_TEST(test_m5_vlog_params)
{
   set_standard(STD_19);
   input_from_file(TESTDIR "/netlist/m5g_params.sv");

   tree_t e = run_elab();
   fail_if_errors();

   conn_design_t *cd = conn_build(e);
   ck_assert_ptr_nonnull(cd);

   conn_block_t *top = conn_root(cd, 0);
   ck_assert_uint_eq(top->nparams, 0);

   // Defaults, in declaration order, without the local parameter
   conn_block_t *lu = conn_find_block(cd, "top.lu");
   ck_assert_ptr_nonnull(lu);
   ck_assert_uint_eq(lu->nparams, 8);
   ck_assert_str_eq(lu->params[0].name, "W");
   ck_assert_str_eq(lu->params[7].name, "UT");
   check_param(lu, "W", "4");
   check_param(lu, "INIT", "4'ha");
   check_param(lu, "SV", "8'sh80");
   check_param(lu, "XZ", "4'b10xz");
   check_param(lu, "S", "\"hi\"");
   check_param(lu, "R", "0.5");
   check_param(lu, "T", "logic [1:0]");
   check_param(lu, "UT", "7");

   // Overrides by position
   conn_block_t *lo = conn_find_block(cd, "top.lo");
   check_param(lo, "W", "3");
   check_param(lo, "INIT", "4'h3");
   check_param(lo, "SV", "8'shff");
   check_param(lo, "XZ", "4'bzzzz");
   check_param(lo, "S", "\"abc\"");
   check_param(lo, "R", "1e-09");
   check_param(lo, "T", "int");
   check_param(lo, "UT", "12");

   // Overrides by name through two levels
   check_param(conn_find_block(cd, "top.mu"), "N", "2");
   check_param(conn_find_block(cd, "top.mu.u"), "W", "2");

   // defparam
   check_param(conn_find_block(cd, "top.dp"), "W", "5");

   // Instances in a generate loop have their own values
   check_param(conn_find_block(cd, "top.g[1].u"), "W", "1");
   check_param(conn_find_block(cd, "top.g[2].u"), "W", "2");

   conn_free(cd);
}
END_TEST

START_TEST(test_m5_vlog_params_mixed)
{
   set_standard(STD_08);

   input_from_file(TESTDIR "/netlist/m5g_mixed.v");
   analyse_file();

   input_from_file(TESTDIR "/netlist/m5g_mixed.vhd");

   tree_t e = run_elab();
   fail_if_errors();

   conn_design_t *cd = conn_build(e);
   ck_assert_ptr_nonnull(cd);

   // A Verilog module instantiated with a generic map from VHDL
   conn_block_t *u = conn_find_block(cd, "M5G_MIXED.U_VP");
   ck_assert_ptr_nonnull(u);
   ck_assert_uint_eq(u->nparams, 2);
   check_param(u, "W", "6");
   check_param(u, "K", "1");

   conn_free(cd);
}
END_TEST

Suite *get_netlist_tests(void)
{
   Suite *s = suite_create("netlist");

   TCase *tc = nvc_unit_test();
   tcase_add_test(tc, test_vhdl1);
   tcase_add_test(tc, test_m5_vhdl_params);
   tcase_add_test(tc, test_m5_vhdl_mixed);
   tcase_add_test(tc, test_vhdl1_query);
   tcase_add_test(tc, test_vhdl1_json);
   tcase_add_test(tc, test_runtime);
   tcase_add_test(tc, test_sv1);
   tcase_add_test(tc, test_vhdl2);
   tcase_add_test(tc, test_iface1);
   tcase_add_test(tc, test_mixed_vlog_top);
   tcase_add_test(tc, test_mixed_vhdl_top);
   tcase_add_test(tc, test_m5_vlog_params);
   tcase_add_test(tc, test_m5_vlog_params_mixed);
   suite_add_tcase(s, tc);

   return s;
}
