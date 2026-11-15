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
#include "common.h"
#include "diag.h"
#include "ident.h"
#include "conn-priv.h"
#include "vlog/vlog-node.h"

#include <ctype.h>

//
// Clocking evidence of Verilog processes and attributes of nets and
// modules, for --netlist --evidence.
//

void vlog_proc_evidence(conn_design_t *cd, conn_proc_t *proc, vlog_node_t v)
{
   conn_evidence_t *ev = dalloc(cd, sizeof(conn_evidence_t));
   ev->cls = CONN_EV_OTHER;
   proc->ev = ev;
}

////////////////////////////////////////////////////////////////////////////////
// Attributes

static char *lower_name(const char *s)
{
   char *r = xstrdup(s);
   for (char *p = r; *p; p++)
      *p = tolower((unsigned char)*p);
   return r;
}

static void add_pragmas(conn_design_t *cd, conn_attr_t **list,
                        unsigned *count, vlog_node_t decl)
{
   const int n = vlog_pragmas(decl);
   for (int i = 0; i < n; i++) {
      vlog_node_t a = vlog_pragma(decl, i);
      if (!vlog_has_ident(a))
         continue;

      char *name LOCAL = lower_name(istr(vlog_ident(a)));
      const char *value = vlog_has_ident2(a) ? istr(vlog_ident2(a)) : "1";
      conn_add_attr(cd, list, count, name, value);
   }
}

void vlog_net_attrs(conn_design_t *cd, conn_net_t *net, vlog_node_t decl)
{
   switch (vlog_kind(decl)) {
   case V_NET_DECL:
   case V_VAR_DECL:
   case V_PORT_DECL:
      add_pragmas(cd, &net->attrs, &net->nattrs, decl);
      break;
   default:
      break;
   }
}

void vlog_block_attrs(conn_design_t *cd, conn_block_t *blk, vlog_node_t body)
{
   switch (vlog_kind(body)) {
   case V_MODULE:
   case V_INST_BODY:
      add_pragmas(cd, &blk->attrs, &blk->nattrs, body);
      break;
   default:
      break;
   }

   // A Verilog top level has no instance to take the location from
   if (blk->src == NULL && !loc_invalid_p(vlog_loc(body))) {
      const char *f = loc_file_str(vlog_loc(body));
      if (f != NULL) {
         blk->src = dstr(cd, f);
         blk->line = vlog_loc(body)->first_line;
      }
   }
}
