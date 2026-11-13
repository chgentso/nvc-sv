//
//  Copyright (C) 2024-2026  Nick Gasson
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

#include "type.h"
#include "tree.h"
#include "common.h"
#include "hash.h"
#include "util.h"
#include "diag.h"
#include "ident.h"
#include "lib.h"
#include "object.h"
#include "parse.h"
#include "scan.h"
#include "vlog/vlog-node.h"
#include "vlog/vlog-phase.h"
#include "vlog/vlog-symtab.h"
#include "vlog/vlog-cov.h"
#include "vlog/vlog-util.h"

#include <ctype.h>
#include <string.h>
#include <stdlib.h>
#include <assert.h>
#include <inttypes.h>

static parse_state_t    state;
static vlog_kind_t      param_kind;
static int              systf_depth;
static vlog_net_kind_t  implicit_kind;
static vlog_symtab_t   *symtab;
static vlog_node_t      last_attr;
static vlog_node_t      last_modport;
static vlog_node_t      atom_types[DT_BIT + 1];
static unsigned         next_namespace;
static uint64_t         time_scale;
static unsigned         time_precision_log10;
static bool             time_unscaled;
static uint64_t         default_time_scale = 1000000;
static unsigned         default_precision_log10 = 6;
static bool             default_unscaled = true;

static const struct {
   const char *name;
   uint64_t value;
} time_units[] = {
   { "s", UINT64_C(1000000000000000) },
   { "ms", 1000000000000 },
   { "us", 1000000000 },
   { "ns", 1000000 },
   { "ps", 1000 },
   { "fs", 1 },
};

extern loc_t yylloc;

#define BEGIN_WITH_HEAD(s, t)                            \
   EXTEND(s);                                            \
   state.start_loc = (t) ? *vlog_loc(t) : LOC_INVALID;   \

#define BEGIN(s)  BEGIN_WITH_HEAD(s, NULL)

static vlog_node_t p_statement_or_null(void);
static void p_tf_port_list(vlog_node_t tf);
static vlog_node_t p_expression(void);
static vlog_node_t p_constant_expression(void);
static vlog_node_t p_mintypmax_expression(void);
static vlog_node_t p_primary(void);
static vlog_node_t p_data_type(void);
static bool is_type_name(vlog_node_t d);
static vlog_node_t p_typedef_dimensions(vlog_node_t dt);
static vlog_node_t package_by_name(ident_t id);
static A(vlog_node_t) typedef_dims = AINIT;
static bool struct_dims_deferred = false;   // Left to the variable declaration
static vlog_node_t p_variable_dimensions(vlog_node_t dt, vlog_node_t decl);
static bool at_checker(void);
static vlog_node_t p_config_declaration(void);
static bool at_checker_call(void);
static vlog_node_t p_checker_call(void);
static void register_checker(const tokenq_t *t, int n);
static bool is_fixed_unpacked(vlog_node_t decl, vlog_node_t dt);
static vlog_node_t foreach_number(int64_t value, const loc_t *loc);
static vlog_node_t rewrite_monitor(vlog_node_t call);
static vlog_node_t expand_let(vlog_node_t decl, vlog_node_t call);
static vlog_node_t lookup_interface(ident_t name);
static vlog_node_t p_modport_name(vlog_node_t iface);
static void set_decl_scopes(vlog_node_t container, ident_t prefix);
static void register_user_net(vlog_node_t decl, vlog_node_t nettype);
static vlog_node_t nettype_lookup(void);
static vlog_node_t user_net_driver(vlog_node_t target);
static void check_user_net_procedural(vlog_node_t lhs);
static void expand_user_nets(vlog_node_t mod);
static vlog_node_t p_class_declaration(void);
static vlog_node_t p_class_new(vlog_node_t dt);
static vlog_node_t p_dynamic_array_new(vlog_node_t dt);
static vlog_node_t p_assignment_pattern_expression(vlog_node_t dt);
static vlog_node_t p_tagged_union_expression(vlog_node_t dt);
static vlog_node_t p_matches(vlog_node_t head);
static vlog_node_t p_nonbinary_expression(void);
static vlog_node_t p_binary_expression(vlog_node_t lhs, int min_prec);
static vlog_node_t p_case_matches(vlog_case_kind_t kind, vlog_node_t expr);
static int match_depth = 0;
static vlog_node_t p_expression_of_type(vlog_node_t dt);
static vlog_node_t arm_type = NULL;   // Type of patterns in a conditional
static vlog_node_t p_constant_expression(void);
static vlog_node_t p_call_result_chain(vlog_node_t head);
static void name_anonymous_type(vlog_node_t v, bool is_enum);
static int typedef_depth = 0;
static bool is_real_view(vlog_node_t v);
static vlog_node_t real_view_call(const char *name, vlog_node_t arg);
static vlog_node_t p_method_call_tail(vlog_node_t prefix);
static vlog_node_t p_operator_assignment(vlog_node_t lhs);
static vlog_node_t p_blocking_assignment(vlog_node_t lhs);
static vlog_node_t p_expression_or_dist(void);
static vlog_node_t p_constraint_expression(void);
static void p_list_of_variable_decl_assignments(vlog_node_t parent,
                                                vlog_node_t datatype,
                                                vlog_kind_t kind);
static vlog_node_t p_variable_lvalue(void);
static vlog_node_t p_select(ident_t id);
static void p_list_of_arguments(vlog_node_t call);
static vlog_net_kind_t p_net_type(void);
static void p_module_or_generate_item(vlog_node_t mod);
static void p_block_item_declaration(vlog_node_t parent);
static int push_base_scopes(vlog_node_t class);
static void put_enum_names(vlog_node_t type_decl);
// Is the name before a scope resolution operator a type, class or
// parameter declared in the file being scanned rather than a package that
// is to be found elsewhere?
static bool is_scope_declaration(vlog_node_t d)
{
   if (d == NULL)
      return false;

   switch (vlog_kind(d)) {
   case V_TYPE_DECL:
   case V_CLASS_DECL:
   case V_PARAM_DECL:
   case V_LOCALPARAM:
   case V_ENUM_DECL:
   case V_STRUCT_DECL:
   case V_UNION_DECL:
      return true;
   default:
      return false;
   }
}

static vlog_node_t p_scope_prefix(void);
static void p_class_constructor_arg_list(vlog_node_t parent);
static vlog_node_t p_attribute_instance(void);
static vlog_node_t  p_drive_strength(void);
static vlog_strength_t p_strength0(void);
static vlog_strength_t p_strength1(void);
static void p_action_block(vlog_node_t parent);
static vlog_node_t p_concurrent_assertion_statement(ident_t label);
static vlog_node_t p_property_spec(void);
static vlog_node_t p_sva_trigger(vlog_node_t decl);
static bool sva_is_decl(vlog_node_t d);
static void sva_apply_defaults(vlog_node_t spec);
static void p_action_block(vlog_node_t parent);
static vlog_node_t p_assertion_process(vlog_node_t v);
static A(vlog_node_t) proc_asserts = AINIT;
static A(vlog_node_t) module_asserts = AINIT;
static int always_depth = 0;
static void p_assertion_item_declaration(vlog_node_t parent);
static void p_clocking_declaration(vlog_node_t parent, bool is_default);
static vlog_node_t global_clock_ref(const loc_t *loc);
static vlog_node_t global_clock_control(const loc_t *loc);
static void p_default_declaration(vlog_node_t parent);

static hset_t *seen_units = NULL;

// The scope of the caller while parsing the constraint block of randomize
static vlog_scope_t *local_scope = NULL;

// Set to stop p_select before the index of the array in a foreach
static bool foreach_array = false;
static vlog_node_t iter_decl = NULL;     // Array locator iterator
static vlog_node_t iter_index = NULL;    // Its index

// Lifetime of variables declared in the procedure being parsed, which is
// static in an initial or always block even in an automatic module, and the
// default lifetime of a task or function in the enclosing scope
static bool        proc_automatic = false;
static bool        subr_automatic = false;
static vlog_node_t current_func = NULL;
static vlog_node_t current_module = NULL;
static int gen_dead = 0;   // Inside a generate branch which is not taken
static vlog_node_t param_target;   // Module whose parameters are assigned
static vlog_node_t module_root = NULL;
static unsigned    gen_constructs = 0;    // Generate constructs in this scope
static unsigned    gen_construct = 0;     // Number of the current construct
static vlog_node_t current_package = NULL;

// Variable toggled when the clock of concurrent assertions in a scope ticks
typedef struct {
   vlog_node_t scope;
   vlog_node_t tick;
   ident_t     clock;
   unsigned    kind;
} shared_tick_t;

static A(shared_tick_t) shared_ticks;
static vlog_node_t default_clock = NULL;
static vlog_node_t last_clocking_ctrl = NULL;
static vlog_node_t default_disable = NULL;
static hash_t     *clocking_blocks = NULL;
static hset_t     *clocking_names = NULL;
static vlog_node_t default_cycle = NULL;
static unsigned    monitor_count = 0;
static bool        in_subroutine = false;

static A(vlog_node_t) pending_statics = AINIT;
static A(vlog_node_t) pending_disables = AINIT;
static A(cov_toks_t) pending_binds = AINIT;
static A(ident_t) pending_bind_scopes = AINIT;
static ident_t bind_scope = NULL;   // Module a replayed bind directive is in
static A(cov_toks_t) pending_units = AINIT;
static vlog_node_t p_description(void);
static hset_t        *block_names = NULL;
static void collect_disable_cb(vlog_node_t v, void *ctx)
{
   if (vlog_kind(v) == V_DISABLE)
      hset_insert(ctx, vlog_ident(v));
}

static void mark_blocks_cb(vlog_node_t v, void *ctx)
{
   if (vlog_kind(v) == V_SEQ_BLOCK && vlog_has_ident(v)
       && hset_contains(ctx, vlog_ident(v)))
      vlog_set_flags(v, VLOG_F_NAMED_DISABLE);
}

static void mark_disabled_blocks(vlog_node_t mod)
{
   // A named block that a disable statement may name from another process
   // is noted so the process records it while suspended inside (9.6.2)

   if (block_names == NULL)
      return;

   hset_t *names = hset_new(16);
   vlog_visit(mod, collect_disable_cb, names);
   vlog_visit(mod, mark_blocks_cb, names);
   hset_free(names);
}

static void check_pending_disables(void);
static void note_block_name(ident_t name);

static void add_pending_statics(vlog_node_t unit)
{
   for (int i = 0; i < pending_statics.count; i++)
      vlog_add_decl(unit, pending_statics.items[i]);

   ACLEAR(pending_statics);
}

static vlog_node_t peek_reference(void)
{
   assert(peek() == tID);
   ident_t id = state.tokenq[state.tokenq_tail].lval.ident;
   return vlog_symtab_query(symtab, id);
}

static ident_t peek_ident_nth(int n)
{
   assert(peek_nth(n) == tID);
   const int pos = (state.tokenq_tail + n - 1) & (TOKENQ_SIZE - 1);
   return state.tokenq[pos].lval.ident;
}

static bool is_scope_randomize(void)
{
   // The std:: prefix may be omitted outside a class

   return peek() == tID && peek_nth(2) == tLPAREN
      && peek_ident_nth(1) == ident_new("randomize")
      && vlog_symtab_class(symtab) == NULL
      && vlog_symtab_query(symtab, ident_new("randomize")) == NULL;
}

static bool is_std_randomize(void)
{
   // [ std :: ] randomize (

   if (is_scope_randomize())
      return true;

   return peek() == tID && peek_nth(2) == tSCOPE && peek_nth(3) == tID
      && peek_nth(4) == tLPAREN && peek_ident_nth(1) == ident_new("std")
      && peek_ident_nth(3) == ident_new("randomize");
}

static vlog_node_t hier_owner(vlog_node_t mr);
static vlog_node_t p_instance_index(vlog_node_t mr);
static bool at_instance_index(bool dot);
static vlog_node_t hier_type_decl(int len);

static bool is_hier_type_name(int *len)
{
   // The name of an interface instance or port followed by the names of
   // nested instances and a type declared in the interface

   if (peek() != tID)
      return false;
   else if (peek_nth(2) == tDOT ? peek_nth(3) != tID
            : peek_nth(2) != tLSQUARE)
      return false;

   vlog_node_t d = peek_reference();
   if (d == NULL)
      return false;

   switch (vlog_kind(d)) {
   case V_MOD_INST:
   case V_IFACE_PORT:
      break;
   default:
      return false;
   }

   int n = 1;
   for (;;) {
      if (peek_nth(n + 1) == tLSQUARE) {
         // The element of an array of instances
         int depth = 1, m = n + 2;
         for (; depth > 0 && m < TOKENQ_SIZE - 2; m++) {
            const token_t tok = peek_nth(m);
            if (tok == tLSQUARE)
               depth++;
            else if (tok == tRSQUARE)
               depth--;
            else if (tok == tEOF || tok == tSEMI)
               return false;
         }

         if (depth > 0)
            return false;
         else if (peek_nth(m) != tDOT || peek_nth(m + 1) != tID)
            break;   // Packed dimensions after the type

         n = m + 1;
      }
      else if (peek_nth(n + 1) == tDOT && peek_nth(n + 2) == tID)
         n += 2;
      else
         break;
   }

   *len = n;
   return n > 1;
}

static bool is_scoped_type_start(void)
{
   // Scope resolution followed by the name of a variable: this is the type
   // in a declaration like pkg::type_t name

   int hlen;
   if (is_hier_type_name(&hlen) && peek_nth(hlen + 1) == tID
       && hier_type_decl(hlen) != NULL)
      return true;   // A type declared in an interface: ifc.type_t name

   if (peek() != tID || peek_nth(2) != tSCOPE)
      return false;
   else if (peek_ident_nth(1) == ident_new("process")
            && peek_reference() == NULL)
      return false;   // Built-in class

   int n = 1;
   while (peek_nth(n + 1) == tSCOPE && peek_nth(n + 2) == tID)
      n += 2;

   if (n == 3 && peek_nth(n + 1) == tLSQUARE) {
      // Packed dimensions after the name of a type in a package
      vlog_node_t pack = NULL;
      if (!lib_scan_active()
          || !is_scope_declaration(vlog_symtab_query(symtab,
                                                     peek_ident_nth(1))))
         pack = package_by_name(peek_ident_nth(1));
      vlog_node_t d = pack ? vlog_scope_member(pack, peek_ident_nth(3)) : NULL;
      return d != NULL && is_data_type(d);
   }

   return n > 1 && (peek_nth(n + 1) == tID || peek_nth(n + 1) == tHASH);
}

static bool is_specialised_scope(void)
{
   // A name in a specialisation of a class like foo #(int)::bar but not a
   // declaration using a type in the class like foo #(int)::type_t x

   if (peek() != tID || peek_nth(2) != tHASH || peek_nth(3) != tLPAREN)
      return false;

   int n = 4;
   for (int depth = 1; depth > 0; n++) {
      if (n >= TOKENQ_SIZE - 8)
         return false;

      const token_t tok = peek_nth(n);
      if (tok == tEOF)
         return false;
      else if (tok == tLPAREN)
         depth++;
      else if (tok == tRPAREN)
         depth--;
   }

   if (peek_nth(n) != tSCOPE)
      return false;

   while (peek_nth(n) == tSCOPE && peek_nth(n + 1) == tID) {
      n += 2;
      if (n >= TOKENQ_SIZE - 8)
         return false;
   }

   return peek_nth(n) != tID;
}

static bool is_process_name(void)
{
   // The built-in process class unless the design declares something with
   // the same name

   return peek() == tID && peek_ident_nth(1) == ident_new("process")
      && peek_reference() == NULL;
}

static bool is_rand_method(ident_t id)
{
   return id == ident_new("randomize") || id == ident_new("rand_mode")
      || id == ident_new("constraint_mode");
}

static bool is_implicit_this_call(void)
{
   // A call to a built-in randomization method in a class without a prefix

   if (peek() != tID || peek_nth(2) != tLPAREN)
      return false;

   ident_t id = peek_ident_nth(1);
   return is_rand_method(id) && vlog_symtab_query(symtab, id) == NULL
      && vlog_symtab_class(symtab) != NULL;
}

typedef struct {
   ident_t     name;
   bool        is_type;
   bool        has_def;
   ident_t     tname;    // Name of the data type of a value parameter
   int         ndef;
   tokenq_t   *def;
} generic_param_t;

typedef struct _generic_class {
   ident_t          name;
   vlog_flags_t     flags;
   vlog_scope_t    *scope;
   int              nparams;
   generic_param_t *params;
   int              ntokens;
   tokenq_t        *tokens;
   int              nexterns;
   struct _generic_class **externs;
   int              nspecs;
   vlog_node_t     *specs;
   vlog_scope_t   **spec_scopes;
   vlog_node_t      home;
   vlog_node_t      module;
} generic_class_t;

static hash_t          *generic_classes = NULL;
static generic_class_t *replay_class = NULL;
static int              replay_pos = 0;
static vlog_node_t      current_namespace = NULL;
static vlog_node_t      spec_container = NULL;

// A class can be used before it is declared after "typedef class name" so
// bodies of subroutines are only parsed once the classes they use are known
typedef struct {
   vlog_node_t      subr;
   vlog_scope_t    *scope;
   generic_class_t  body;
   token_t          end_token;
   bool             proc_automatic;
   bool             subr_automatic;
   bool             in_subroutine;
   bool             is_init;
   bool             is_constraint;
   vlog_node_t      func;
   vlog_node_t      container;
} deferred_body_t;

// A specialisation of a class where only the forward declaration is known
typedef struct {
   vlog_node_t      forward;
   vlog_node_t      placeholder;
   text_buf_t      *key;
   generic_class_t  actuals;
   vlog_scope_t    *scope;
   vlog_node_t      container;
} forward_spec_t;

static A(deferred_body_t) deferred_bodies = AINIT;
static A(forward_spec_t)  forward_specs = AINIT;
static A(vlog_node_t)     forward_classes = AINIT;

typedef struct {
   ident_t id;
   loc_t   loc;
} forward_type_t;

static A(forward_type_t)  forward_types = AINIT;
static A(vlog_node_t)     unit_specs = AINIT;
static hash_t            *spec_modules = NULL;
static A(vlog_node_t)     unit_generics = AINIT;
static hash_t            *generic_homes = NULL;
static A(ident_t)         file_namespaces = AINIT;
static int                class_depth = 0;
static bool               flushing_bodies = false;
static vlog_node_t        spec_into = NULL;
static vlog_node_t        spec_extern_class = NULL;

static bool capture_generic_extern(token_t end_token);
static bool capture_generic_constraint(void);
static vlog_node_t p_task_declaration(void);
static vlog_node_t p_function_declaration(void);
static vlog_node_t specialise_class(vlog_node_t generic);

#define MAX_SPEC_DEPTH 50
static int spec_depth = 0;
static void flush_deferred_bodies(bool end_of_unit);
static void p_timeunits_declaration(void);
static void p_parameter_value_assignment(vlog_node_t inst);
static bool is_nonansi_interface_port(vlog_node_t mod);
static void p_nonansi_interface_port_declaration(vlog_node_t mod);
static bool defer_subr_body(vlog_node_t subr, token_t end_token);
static vlog_node_t specialise_forward(vlog_node_t forward);
static bool want_defer_body(void);
static vlog_node_t resolve_forward_class(ident_t name);
static void resolve_forward_specs(vlog_node_t class);
static vlog_node_t p_data_type_or_implicit(void);

static hash_t *interfaces = NULL;
static hash_t *inst_modules = NULL;
static hash_t *inst_lists = NULL;   // Instance to the list declaring it
static bool parsing_prototype = false;
static bool parsing_dpi_import = false;

// Blocks which break and return leave in the code block of a production
// and the number of loops inside it
static ident_t rs_break_name = NULL;
static ident_t rs_return_name = NULL;
static int     rs_loop_depth = 0;

// Package or class which qualifies the name that is about to be looked up
static vlog_node_t qualify_scope = NULL;
static bool pattern_array_elems = false;   // The next pattern is for the elements of an unpacked array
static vlog_scope_t *select_scope = NULL;
static bool        qualify_failed = false;

// Set after parsing the definition of a method outside of its class
static bool extern_definition = false;

static bool accept_interface(object_t *obj)
{
   vlog_node_t v = vlog_from_object(obj);
   return v != NULL && vlog_kind(v) == V_MODULE
      && (vlog_flags(v) & VLOG_F_INTERFACE);
}

static bool accept_package(object_t *obj)
{
   vlog_node_t v = vlog_from_object(obj);
   return v != NULL && vlog_kind(v) == V_PACKAGE;
}

static bool accept_vhdl_package(object_t *obj)
{
   tree_t t = tree_from_object(obj);
   return t != NULL && tree_kind(t) == T_PACKAGE;
}

static object_t *search_other_libs(ident_t unit, lib_accept_fn_t accept)
{
   // Find a design unit which is not in the work library in any other
   // library on the search path

   // Misses are cached as the same names are often looked up repeatedly
   static hset_t *misses[3];
   const int slot = accept == accept_interface ? 0
      : accept == accept_package ? 1 : 2;
   if (misses[slot] == NULL)
      misses[slot] = hset_new(16);
   else if (hset_contains(misses[slot], unit))
      return NULL;

   lib_t where;
   object_t *obj = lib_search_generic(unit, accept, &where);
   if (obj == NULL)
      hset_insert(misses[slot], unit);

   return obj;
}

static void scan_note_name(ident_t name)
{
   // When scanning for dependencies a name without a library is recorded
   // for the caller to resolve
   if (!lib_scan_active())
      return;

   LOCAL_TEXT_BUF tb = tb_new();
   tb_cat(tb, "?");
   tb_istr(tb, name);
   lib_scan_note(ident_new(tb_get(tb)));
}

static void scan_skip_parameters(void)
{
   // When scanning for dependencies the class being specialised may be
   // in a package which is not available: skip the parameter value list
   consume(tHASH);

   if (peek() != tLPAREN)
      return;

   int depth = 0;
   do {
      const token_t tok = peek();
      if (tok == tEOF)
         break;
      else if (tok == tLPAREN)
         depth++;
      else if (tok == tRPAREN)
         depth--;
      consume(tok);
   } while (depth > 0);
}

static vlog_node_t lookup_interface(ident_t name)
{
   if (interfaces != NULL) {
      vlog_node_t v = hash_get(interfaces, name);
      if (v != NULL)
         return v;
   }

   LOCAL_TEXT_BUF tb = tb_new();
   tb_istr(tb, name);
   tb_upcase(tb);

   ident_t unit = ident_new(tb_get(tb));
   ident_t qual = ident_prefix(lib_name(lib_work()), unit, '.');
   object_t *obj = lib_get_generic(lib_work(), qual, NULL);
   if (obj == NULL)
      obj = search_other_libs(unit, accept_interface);

   if (obj == NULL)
      return NULL;

   return vlog_from_object(obj);
}

static void vlog_parse_error_cb(void)
{
   vlog_symtab_suppress(symtab);
}

static uint64_t time_pow10(unsigned n)
{
   uint64_t r = 1;
   while (n-- > 0)
      r *= 10;
   return r;
}

static bool valid_time_magnitude(uint64_t value, const char *what)
{
   if (value == 100 || value == 10 || value == 1)
      return true;

   diag_t *d = diag_new(DIAG_ERROR, CURRENT_LOC);
   diag_printf(d, "invalid order of magnitude in %s", what);
   diag_hint(d, NULL, "the valid values are 1, 10, and 100");
   diag_emit(d);
   return false;
}

static void check_time_precision(const char *what)
{
   if (time_pow10(time_precision_log10) > time_scale) {
      diag_t *d = diag_new(DIAG_ERROR, CURRENT_LOC);
      diag_printf(d, "the time precision of a %s cannot be longer than the "
                  "time unit", what);
      diag_emit(d);

      time_precision_log10 = 0;
      for (uint64_t p = time_scale; p >= 10; p /= 10)
         time_precision_log10++;
   }
}

static int64_t time_ival(void)
{
   return VLOG_TIME_ENCODE(time_scale,
                           time_unscaled ? VLOG_TIME_UNSCALED
                           : time_precision_log10);
}

static bool set_time_unit(uint64_t value, uint64_t scale, const char *what)
{
   if (!valid_time_magnitude(value, what))
      return false;

   time_scale = value * scale;
   time_unscaled = false;
   return true;
}

static bool set_time_precision(uint64_t value, uint64_t scale,
                               const char *what)
{
   if (!valid_time_magnitude(value, what))
      return false;

   time_precision_log10 = 0;
   for (uint64_t p = value * scale; p >= 10; p /= 10)
      time_precision_log10++;

   time_unscaled = false;
   return true;
}

static void set_timescale(uint64_t unit_value, uint64_t unit_scale,
                          uint64_t prec_value, uint64_t prec_scale)
{
   // See IEEE 1800-2017 section 22.7 for rules

   const char *what = "`timescale directive";
   const bool unit_ok = set_time_unit(unit_value, unit_scale, what);
   const bool prec_ok = set_time_precision(prec_value, prec_scale, what);

   if (unit_ok && prec_ok)
      check_time_precision(what);
}

static void skip_over_attributes(void)
{
   while (peek() == tATTRBEGIN)
      last_attr = p_attribute_instance();
}

static void optional_attributes(void)
{
   if (peek() == tATTRBEGIN) {
      // If the current token is (* and there is a saved attribute
      // instance from skip_over_attributes() then the attribute must
      // have been placed incorrectly as there were other tokens between
      // the last closing *) and this
      if (last_attr != NULL)
         parse_error(vlog_loc(last_attr), "attribute instance is not "
                     "allowed here");

      do {
         (void)p_attribute_instance();
      } while (peek() == tATTRBEGIN);
   }

   last_attr = NULL;
}

static vlog_node_t dummy_expression(void)
{
   vlog_node_t v = vlog_new(V_NUMBER);
   vlog_set_number(v, number_from_bool(false));
   return v;
}

static vlog_node_t logic_type(void)
{
   vlog_node_t v = vlog_new(V_DATA_TYPE);
   vlog_set_subkind(v, DT_LOGIC);
   return v;
}

static vlog_node_t bit_type(void)
{
   vlog_node_t v = vlog_new(V_DATA_TYPE);
   vlog_set_subkind(v, DT_BIT);
   return v;
}

static vlog_node_t implicit_type(void)
{
   vlog_node_t v = vlog_new(V_DATA_TYPE);
   vlog_set_subkind(v, DT_IMPLICIT);
   return v;
}

static vlog_node_t make_integer_atom_type(data_type_t dt)
{
   if (atom_types[dt] == NULL) {
      int width;
      switch (dt) {
      case DT_BYTE:     width = 8; break;
      case DT_SHORTINT: width = 16; break;
      case DT_INT:      width = 32; break;
      case DT_LONGINT:  width = 64; break;
      case DT_INTEGER:  width = 32; break;
      case DT_TIME:     width = 64; break;
      default: should_not_reach_here();
      }

      vlog_node_t left = vlog_new(V_NUMBER);
      vlog_set_number(left, number_from_int(width - 1));

      vlog_node_t right = vlog_new(V_NUMBER);
      vlog_set_number(right, number_from_int(0));

      vlog_node_t r = vlog_new(V_DIMENSION);
      vlog_set_subkind(r, V_DIM_PACKED);
      vlog_set_left(r, left);
      vlog_set_right(r, right);

      atom_types[dt] = r;
   }

   vlog_node_t v = vlog_new(V_DATA_TYPE);
   vlog_set_subkind(v, dt);
   vlog_add_range(v, atom_types[dt]);

   vlog_set_loc(v, &state.last_loc);
   return v;
}

static hset_t *used_labels = NULL;

static ident_t default_label(const char *prefix)
{
   // Several statements expanded from one macro have the same location

   if (used_labels == NULL)
      used_labels = hset_new(64);

   ident_t base = ident_sprintf("%s#%d#%d", prefix, state.last_loc.first_line,
                                state.last_loc.first_column);

   ident_t id = base;
   for (int n = 0; hset_contains(used_labels, id); )
      id = ident_sprintf("%s#%d", istr(base), ++n);

   hset_insert(used_labels, id);
   return id;
}

static vlog_gate_kind_t get_gate_kind(token_t tok)
{
   switch (tok) {
   case tAND:      return V_GATE_AND;
   case tNAND:     return V_GATE_NAND;
   case tOR:       return V_GATE_OR;
   case tNOR:      return V_GATE_NOR;
   case tXOR:      return V_GATE_XOR;
   case tXNOR:     return V_GATE_XNOR;
   case tNOT:      return V_GATE_NOT;
   case tNOTIF0:   return V_GATE_NOTIF0;
   case tNOTIF1:   return V_GATE_NOTIF1;
   case tBUF:      return V_GATE_BUF;
   case tBUFIF0:   return V_GATE_BUFIF0;
   case tBUFIF1:   return V_GATE_BUFIF1;
   case tTRAN:     return V_GATE_TRAN;
   case tTRANIF0:  return V_GATE_TRANIF0;
   case tTRANIF1:  return V_GATE_TRANIF1;
   case tRTRAN:    return V_GATE_RTRAN;
   case tRTRANIF0: return V_GATE_RTRANIF0;
   case tRTRANIF1: return V_GATE_RTRANIF1;
   default:        should_not_reach_here();
   }
}

static void declare_port(vlog_node_t mod, vlog_node_t port)
{
   ident_t id = vlog_ident(port);

   const int nports = vlog_ports(mod);
   bool found = false;
   for (int i = 0; i < nports; i++) {
      vlog_node_t p = vlog_port(mod, i);
      if (vlog_ident(p) == id) {
         vlog_set_ref(p, port);
         found = true;
      }
   }

   if (!found) {
      diag_t *d = diag_new(DIAG_ERROR, vlog_loc(port));
      diag_printf(d, "'%s' does not appear in module port list", istr(id));
      diag_hint(d, vlog_loc(mod), "module declaration for '%s'",
                istr(vlog_ident2(mod)));
      diag_emit(d);
   }
}

static vlog_node_t get_data_type(ident_t id)
{
   vlog_node_t v = vlog_symtab_query(symtab, id);
   if (v == NULL) {
      error_at(&state.last_loc, "no data type declaration for %pQ", id);
      return logic_type();
   }
   else if (!is_data_type(v)) {
      diag_t *d = diag_new(DIAG_ERROR, &state.last_loc);
      diag_printf(d, "%pQ is not a data type", id);
      diag_hint(d, vlog_loc(v), "%pQ declared here", id);
      diag_emit(d);

      return logic_type();
   }

   return v;
}

static bool scan_type_declaration(void)
{
   switch (peek()) {
   case tREG:       case tSTRUCT:   case tUNION:   case tTYPEDEF:
   case tENUM:      case tSVINT:    case tINTEGER: case tSVREAL:
   case tSHORTREAL: case tREALTIME: case tBIT:     case tLOGIC:
   case tSHORTINT:  case tTIME:     case tLONGINT: case tBYTE:
   case tSTRINGK: case tCHANDLE:   case tEVENT:    case tVIRTUAL: case tAUTOMATIC:
   case tSTATIC:    case tCONST:    case tVAR:     case tTYPE:
      return true;
   case tID:
      {
         if (is_specialised_scope())
            return false;
         else if (is_scoped_type_start())
            return true;
         else if (peek_nth(2) == tSCOPE && !is_process_name())
            return false;   // Name in a class or package
         else if (is_process_name() && (peek_nth(2) == tID
                                        || (peek_nth(2) == tSCOPE
                                            && peek_nth(3) == tID
                                            && peek_ident_nth(3)
                                            == ident_new("state"))))
            return true;

         vlog_node_t dt = peek_reference();
         return (dt != NULL && is_data_type(dt)) || peek_nth(2) == tHASH;
      }
   default:
      return false;
   }
}

static bool scan_block_item_declaration(void)
{
   if (scan_type_declaration())
      return true;

   return scan(tLOCALPARAM, tPARAMETER, tIMPORT);
}

static bool scan_tf_item_declaration(void)
{
   if (scan_type_declaration())
      return true;

   return scan(tINPUT, tOUTPUT, tINOUT, tREF, tLOCALPARAM, tPARAMETER,
               tIMPORT);
}

static ident_t p_identifier(void)
{
   if (consume(tID))
      return state.last_lval.ident;
   else
      return error_marker();
}

static void p_external_identifier(ident_t *id, ident_t *ext)
{
   if (consume(tID)) {
      *id = state.last_lval.ident;

      LOCAL_TEXT_BUF tb = tb_new();
      tb_istr(tb, state.last_lval.ident);
      tb_upcase(tb);

      *ext = ident_new(tb_get(tb));
   }
   else
      *id = *ext = error_marker();
}

static bool is_array_method_name(ident_t id)
{
   static const char *const names[] = {
      "reverse", "sort", "rsort", "shuffle", "sum", "product", "and", "or",
      "xor", "min", "max", "unique", "unique_index", "find", "find_index",
      "find_first", "find_first_index", "find_last", "find_last_index",
   };

   for (int i = 0; i < ARRAY_LEN(names); i++) {
      if (id == ident_new(names[i]))
         return true;
   }

   return false;
}

static bool is_array_method_call(vlog_node_t prefix)
{
   // After the dot: array manipulation methods may omit parentheses
   // and may be named by keywords

   switch (peek()) {
   case tAND:
   case tOR:
   case tXOR:
   case tUNIQUE:
      return true;
   case tID:
      break;
   default:
      return false;
   }

   return vlog_kind(prefix) == V_REF && vlog_has_ref(prefix)
      && vlog_is_fixed_unpacked(vlog_ref(prefix))
      && is_array_method_name(peek_ident_nth(1));
}

static ident_t p_method_name(void)
{
   switch (peek()) {
   case tAND:
      consume(tAND);
      return ident_new("and");
   case tOR:
      consume(tOR);
      return ident_new("or");
   case tXOR:
      consume(tXOR);
      return ident_new("xor");
   case tUNIQUE:
      consume(tUNIQUE);
      return ident_new("unique");
   default:
      return p_identifier();
   }
}

static ident_t p_system_tf_identifier(void)
{
   if (consume(tSYSTASK))
      return state.last_lval.ident;
   else
      return error_marker();
}

static void p_attr_spec(void)
{
   // attr_name [ = constant_expression ]

   BEGIN("attribute specification");

   (void)p_identifier();

   if (optional(tEQ))
      (void)p_constant_expression();
}

static vlog_node_t p_attribute_instance(void)
{
   // (* attr_spec { , attr_spec } *)

   BEGIN("attribute instance");

   consume(tATTRBEGIN);

   vlog_node_t v = vlog_new(V_ATTR_INST);

   do {
      p_attr_spec();
   } while (optional(tCOMMA));

   consume(tATTREND);

   vlog_set_loc(v, CURRENT_LOC);
   return v;
}

static uint64_t p_time_unit(void)
{
   // s | ms | us | ns | ps | fs

   BEGIN("time unit");

   ident_t id = p_identifier();

   for (int j = 0; j < ARRAY_LEN(time_units); j++) {
      if (icmp(id, time_units[j].name))
         return time_units[j].value;
   }

   if (id != error_marker())
      error_at(&state.last_loc, "invalid time unit name %pQ", id);

   return 1;
}

static uint64_t p_unsigned_integer(void)
{
   // decimal_digit { _ | decimal_digit }

   BEGIN("unsigned number");

   if (consume(tUNSNUM)) {
      uint64_t value = 0;
      for (const char *p = state.last_lval.str; *p; p++) {
         if (*p != '_') {
            value *= 10;
            value += (*p - '0');
         }
      }

      free(state.last_lval.str);
      return value;
   }
   else
      return 0;
}

static void p_time_literal(uint64_t *value, uint64_t *scale, unsigned *frac)
{
   // unsigned_number time_unit | fixed_point_number time_unit

   // The value is returned as the digits with the decimal point removed
   // and the number of digits after the decimal point

   BEGIN("time literal");

   consume(tTIMELIT);

   *value = 0;
   *scale = 1;
   *frac = 0;

   bool overflow = false, point = false;
   for (const char *p = state.last_lval.str; *p; p++) {
      if (*p == '_' || isspace_iso88591(*p))
         continue;
      else if (*p == '.')
         point = true;
      else if (!isdigit_iso88591(*p)) {
         bool name_valid = false;
         for (int j = 0; j < ARRAY_LEN(time_units); j++) {
            if (strcmp(time_units[j].name, p) == 0) {
               *scale = time_units[j].value;
               name_valid = true;
               break;
            }
         }

         if (!name_valid)
            error_at(&state.last_loc, "invalid time unit name '%s'", p);

         break;
      }
      else {
         overflow |= __builtin_mul_overflow(*value, 10, value);
         overflow |= __builtin_add_overflow(*value, *p - '0', value);
         if (point)
            (*frac)++;
      }
   }

   if (overflow || *frac > 19) {
      error_at(&state.last_loc, "time value cannot be represented");
      *value = 0;
      *frac = 0;
   }

   free(state.last_lval.str);
}

static uint64_t time_literal_to_fs(uint64_t value, uint64_t scale,
                                   unsigned frac, const loc_t *loc)
{
   // Convert a time literal to femtoseconds rounding to the nearest
   // femtosecond with ties rounding away from zero: the precision of the
   // design is not known until elaboration

   const unsigned __int128 num = (unsigned __int128)value * scale;
   const unsigned __int128 den = time_pow10(frac);
   const unsigned __int128 steps = (2 * num + den) / (2 * den);

   if (steps > INT64_MAX) {
      error_at(loc, "time value cannot be represented");
      return 0;
   }

   return (uint64_t)steps;
}

static vlog_node_t p_unsigned_number(void)
{
   // decimal_digit { _ | decimal_digit }

   BEGIN("unsigned number");

   number_t n;
   if (consume(tUNSNUM)) {
      n = number_new(state.last_lval.str, CURRENT_LOC);
      free(state.last_lval.str);
   }
   else
      n = number_from_int(0);

   vlog_node_t v = vlog_new(V_NUMBER);
   vlog_set_number(v, n);
   vlog_set_loc(v, CURRENT_LOC);
   return v;
}

static vlog_node_t p_integral_number(void)
{
   // decimal_number | octal_number | binary_number | hex_number

   BEGIN("integral number");

   if (peek() == tUNSNUM)
      return p_unsigned_number();
   else {
      number_t n;
      bool fill = false;
      if (consume(tNUMBER)) {
         const char *text = state.last_lval.str;
         if (text[0] == '\'' && text[1] != 's' && text[1] != 'S'
             && strchr("01xXzZ", text[1]) != NULL && text[2] == '\0') {
            // Unbased unsized literal fills the context
            char bit[] = { '1', '\'', 'b', text[1], '\0' };
            n = number_new(bit, &state.last_loc);
            fill = true;
         }
         else
            n = number_new(text, &state.last_loc);
         free(state.last_lval.str);
      }
      else
         n = number_from_int(0);

      vlog_node_t v = vlog_new(V_NUMBER);
      vlog_set_number(v, n);
      if (fill)
         vlog_set_flags(v, VLOG_F_FILL);
      vlog_set_loc(v, CURRENT_LOC);
      return v;
   }
}

static vlog_node_t p_real_number(void)
{
   // fixed_point_number
   //   | unsigned_number [ . unsigned_number ] exp [ sign ] unsigned_number

   BEGIN("real number");

   consume(tREAL);

   vlog_node_t v = vlog_new(V_REAL);
   vlog_set_dval(v, state.last_lval.real);
   vlog_set_loc(v, CURRENT_LOC);
   return v;
}

static vlog_node_t p_number(void)
{
   // integral_number | real_number

   BEGIN("number");

   if (peek() == tREAL)
      return p_real_number();
   else
      return p_integral_number();
}

static void expand_hier_name(text_buf_t *tb)
{
   // Record the names of the enclosing subprograms and blocks in each %m
   // as the instance part is only known at run time

   hier_owner_t owner;
   vlog_node_t pkg;
   ident_t suffix = vlog_symtab_hier_suffix(symtab, &owner, &pkg);

   LOCAL_TEXT_BUF name = tb_new();
   switch (owner) {
   case HIER_INSTANCE:
      if (suffix == NULL)
         return;
      tb_printf(name, ".%s", istr(suffix));
      break;
   case HIER_PACKAGE:
      tb_printf(name, "=%s", istr(vlog_ident2(pkg)));
      if (suffix != NULL)
         tb_printf(name, ".%s", istr(suffix));
      break;
   case HIER_UNIT:
      // The compilation-unit scope is called $unit (3.12.1, 26.3)
      tb_cat(name, "=$unit");
      if (suffix != NULL)
         tb_printf(name, ".%s", istr(suffix));
      break;
   }

   LOCAL_TEXT_BUF out = tb_new();
   const char *p = tb_get(tb);
   for (; *p != '\0'; p++) {
      tb_append(out, *p);
      if (*p != '%' || p[1] == '\0')
         continue;

      for (p++; *p != '\0' && strchr("-.0123456789", *p); p++)
         tb_append(out, *p);

      if (*p == 'm')
         tb_printf(out, "{%s}m", tb_get(name));
      else if (*p != '\0')
         tb_append(out, *p);
      else
         break;
   }

   tb_rewind(tb);
   tb_cat(tb, tb_get(out));
}

static vlog_node_t p_string_literal(void)
{
   // " { Any_ASCII_Characters } "

   BEGIN("string literal");

   number_t n;
   if (consume(tSTRING)) {
      text_buf_t *tb = state.last_lval.text;
      if (systf_depth > 0)
         expand_hier_name(tb);
      n = number_from_string(tb_get(tb), tb_len(tb));
      tb_free(tb);
   }
   else
      should_not_reach_here();

   vlog_node_t v = vlog_new(V_STRING);
   vlog_set_number(v, n);
   vlog_set_loc(v, CURRENT_LOC);
   return v;
}

static vlog_node_t p_primary_literal(void)
{
   // number | time_literal | unbased_unsized_literal | string_literal

   BEGIN("primary literal");

   switch (peek()) {
   case tNUMBER:
   case tUNSNUM:
   case tREAL:
      return p_number();
   case tSTRING:
      return p_string_literal();
   default:
      one_of(tNUMBER, tUNSNUM, tREAL, tSTRING);
      return dummy_expression();
   }
}

static vlog_node_t p_constant_select(ident_t id)
{
   // [ { . member_identifier constant_bit_select } . member_identifier ]
   //    constant_bit_select [ [ constant_part_select_range ] ]

   EXTEND("constant select");

   // Checked for constant-ness later
   return p_select(id);
}

static vlog_node_t p_constant_expression(void)
{
   // constant_primary | unary_operator { attribute_instance } constant_primary
   //   | constant_expression binary_operator { attribute_instance }
   //       constant_expression
   //   | constant_expression ? { attribute_instance }
   //       constant_expression : constant_expression

   BEGIN("constant expression");

   // Checked for constant-ness later
   return p_expression();
}

static void p_constant_range(vlog_node_t *left, vlog_node_t *right)
{
   // constant_expression : constant_expression

   BEGIN("constant range");

   *left = p_constant_expression();

   consume(tCOLON);

   *right = p_constant_expression();
}

static vlog_node_t p_constant_range_expression(void)
{
   // constant_expression | constant_part_select_range

   BEGIN("constant range expression");

   return p_constant_expression();
}

static vlog_node_t p_constant_mintypmax_expression(void)
{
   // constant_expression
   //   | constant_expression : constant_expression : constant_expression

   // Checked for constant-ness later
   return p_mintypmax_expression();
}

static vlog_node_t p_packed_dimension(void)
{
   // [ constant_range ] | unsized_dimension

   BEGIN("packed dimension");

   consume(tLSQUARE);

   if (peek() == tRSQUARE) {
      // The packed dimension of a formal argument of a DPI import takes
      // its size from the actual argument
      consume(tRSQUARE);

      vlog_node_t u = vlog_new(V_DIMENSION);
      vlog_set_subkind(u, V_DIM_UNSIZED);
      vlog_set_flags(u, parsing_dpi_import
                     ? (VLOG_F_OPEN_PACKED | VLOG_F_DPI) : VLOG_F_OPEN_PACKED);
      vlog_set_loc(u, CURRENT_LOC);
      return u;
   }

   vlog_node_t left, right;
   p_constant_range(&left, &right);

   consume(tRSQUARE);

   vlog_node_t v = vlog_new(V_DIMENSION);
   vlog_set_subkind(v, V_DIM_PACKED);
   vlog_set_left(v, left);
   vlog_set_right(v, right);
   vlog_set_loc(v, CURRENT_LOC);

   return v;
}

static vlog_node_t p_unpacked_dimension(void)
{
   // [ constant_range ] | [ constant_expression ]

   BEGIN("unpacked dimension");

   consume(tLSQUARE);

   vlog_node_t left = p_constant_expression(), right;
   if (optional(tCOLON))
      right = p_constant_expression();
   else {
      // A single size is the same as [0:size-1]
      vlog_node_t one = vlog_new(V_NUMBER);
      vlog_set_number(one, number_from_int(1));
      vlog_set_loc(one, CURRENT_LOC);

      right = vlog_new(V_BINARY);
      vlog_set_subkind(right, V_BINARY_MINUS);
      vlog_set_left(right, left);
      vlog_set_right(right, one);
      vlog_set_loc(right, CURRENT_LOC);

      left = vlog_new(V_NUMBER);
      vlog_set_number(left, number_from_int(0));
      vlog_set_loc(left, CURRENT_LOC);
   }

   consume(tRSQUARE);

   vlog_node_t v = vlog_new(V_DIMENSION);
   vlog_set_subkind(v, V_DIM_UNPACKED);
   vlog_set_left(v, left);
   vlog_set_right(v, right);
   vlog_set_loc(v, CURRENT_LOC);

   return v;
}

static vlog_node_t p_data_type_or_void(void)
{
   // data_type | void

   BEGIN("data type or void");

   if (optional(tVOID))
      return NULL;
   else
      return p_data_type();
}

static void p_struct_union_member(vlog_node_t v)
{
   // { attribute_instance } [ random_qualifier ] data_type_or_void
   //    list_of_variable_decl_assignments ;

   BEGIN("struct or union member");

   optional_attributes();

   vlog_flags_t flags = 0;
   if (optional(tRAND))
      flags |= VLOG_F_RAND;
   else if (optional(tRANDC))
      flags |= VLOG_F_RANDC;

   const int first = vlog_decls(v);

   ACLEAR(typedef_dims);

   vlog_node_t dt = p_data_type_or_void();
   if (dt == NULL) {
      // A member of a tagged union that carries no value
      dt = make_integer_atom_type(DT_BYTE);
      flags |= VLOG_F_VOID;
   }

   p_list_of_variable_decl_assignments(v, dt, V_VAR_DECL);
   ACLEAR(typedef_dims);

   const int ndecls = vlog_decls(v);
   for (int i = first; i < ndecls; i++)
      vlog_set_flags(vlog_decl(v, i), vlog_flags(vlog_decl(v, i)) | flags);

   consume(tSEMI);
}

static void p_enum_name_declaration(vlog_node_t parent)
{
   // enum_identifier [ [ integral_number [ : integral_number ] ] ]
   //   [ = constant_expression ]

   BEGIN("enum name declaration");

   ident_t name = p_identifier();

   // A range generates a sequence of names with a numeric suffix
   int first = 0, last = 0;
   bool sequence = false;
   if (optional(tLSQUARE)) {
      sequence = true;

      consume(tUNSNUM);
      first = last = atoi(state.last_lval.str);
      free(state.last_lval.str);

      if (optional(tCOLON)) {
         consume(tUNSNUM);
         last = atoi(state.last_lval.str);
         free(state.last_lval.str);
      }
      else {
         last = first - 1;
         first = 0;
      }

      consume(tRSQUARE);
   }

   vlog_node_t value = NULL;
   if (optional(tEQ))
      value = p_constant_expression();

   const int step = first <= last ? 1 : -1;
   for (int i = first; ; i += step) {
      vlog_node_t v = vlog_new(V_ENUM_NAME);
      vlog_set_ident(v, sequence ? ident_sprintf("%s%d", istr(name), i) : name);
      vlog_set_type(v, parent);

      if (value != NULL && i == first)
         vlog_set_value(v, value);

      vlog_add_decl(parent, v);

      vlog_set_loc(v, CURRENT_LOC);
      vlog_symtab_put(symtab, v);

      if (!sequence || i == last)
         break;
   }
}

static vlog_node_t p_integer_atom_type(void)
{
   // byte | shortint | int | longint | integer | time

   BEGIN("integer atom type");

   data_type_t dt = DT_BYTE;
   switch (one_of(tBYTE, tSHORTINT, tSVINT, tLONGINT, tINTEGER, tTIME)) {
   case tBYTE:     dt = DT_BYTE; break;
   case tSHORTINT: dt = DT_SHORTINT; break;
   case tSVINT:    dt = DT_INT; break;
   case tLONGINT:  dt = DT_LONGINT; break;
   case tINTEGER:  dt = DT_INTEGER; break;
   case tTIME:     dt = DT_TIME; break;
   }

   return make_integer_atom_type(dt);
}

static vlog_node_t p_integer_vector_type(void)
{
   //  bit | logic | reg

   BEGIN("integer vector type");

   data_type_t dt = DT_LOGIC;
   switch (one_of(tBIT, tLOGIC, tREG)) {
   case tBIT:    dt = DT_BIT; break;
   case tLOGIC:
   case tREG:    dt = DT_LOGIC; break;
   }

   vlog_node_t v = vlog_new(V_DATA_TYPE);
   vlog_set_subkind(v, dt);
   vlog_set_loc(v, &state.last_loc);
   return v;
}

static vlog_node_t p_non_integer_type(void)
{
   // shortreal | real | realtime

   BEGIN("non-integer type");

   data_type_t dt = DT_REAL;
   switch (one_of(tSVREAL, tSHORTREAL, tREALTIME)) {
   case tSVREAL:    dt = DT_REAL; break;
   case tSHORTREAL: dt = DT_SHORTREAL; break;
   case tREALTIME:  dt = DT_REALTIME; break;
   }

   vlog_node_t v = vlog_new(V_DATA_TYPE);
   vlog_set_subkind(v, dt);
   vlog_set_loc(v, &state.last_loc);
   return v;
}

static vlog_node_t p_struct_union(void)
{
   // struct | union [ tagged ]

   BEGIN("struct or union");

   switch (one_of(tSTRUCT, tUNION)) {
   case tUNION:
      {
         vlog_node_t v = vlog_new(V_UNION_DECL);
         if (optional(tTAGGED))
            vlog_set_flags(v, VLOG_F_TAGGED);
         return v;
      }
   case tSTRUCT:
   default:
      return vlog_new(V_STRUCT_DECL);
   }
}

static vlog_flags_t p_signing(void)
{
   // signed | unsigned

   BEGIN("signing");

   switch (one_of(tSIGNED, tUNSIGNED)) {
   case tSIGNED: return VLOG_F_SIGNED;
   default: return 0;
   }
}

static vlog_node_t p_enum_base_type(void)
{
   // integer_atom_type [ signing ]
   //   | integer_vector_type [ signing ] [ packed_dimension ]
   //   | type_identifier [ packed_dimension ]

   BEGIN("enum base type");

   switch (peek()) {
   case tBYTE:
   case tSHORTINT:
   case tSVINT:
   case tLONGINT:
   case tINTEGER:
   case tTIME:
      {
         vlog_node_t v = p_integer_atom_type();

         if (scan(tSIGNED, tUNSIGNED))
            vlog_set_flags(v, p_signing());
         else
            vlog_set_flags(v, VLOG_F_SIGNED);

         return v;
      }

   case tBIT:
   case tLOGIC:
   case tREG:
      {
         vlog_node_t v = p_integer_vector_type();

         if (scan(tSIGNED, tUNSIGNED))
            vlog_set_flags(v, p_signing());

         if (scan(tLSQUARE))
            vlog_add_range(v, p_packed_dimension());

         return v;
      }

   default:
      return get_data_type(p_identifier());
   }
}

static bool union_member_elem_width(vlog_node_t d, int *width, bool *two,
                                    bool *is_signed, vlog_node_t *elem)
{
   // The element of a member is a vector or an integer atom

   vlog_node_t t = vlog_type(d);
   while (vlog_kind(t) == V_TYPE_DECL)
      t = vlog_type(t);

   if (vlog_kind(t) == V_ENUM_DECL && vlog_has_type(t))
      t = vlog_get_type(vlog_type(t));

   if (t == NULL || vlog_kind(t) != V_DATA_TYPE || vlog_has_type(t))
      return false;

   int w = 1;
   switch (vlog_subkind(t)) {
   case DT_BYTE:     w = 8; break;
   case DT_SHORTINT: w = 16; break;
   case DT_INT:
   case DT_INTEGER:  w = 32; break;
   case DT_LONGINT:
   case DT_TIME:     w = 64; break;
   case DT_REAL:
   case DT_REALTIME:
      if (vlog_ranges(t) > 0)
         return false;
      *width = 64;
      *two = true;
      *is_signed = false;
      *elem = t;
      return true;
   case DT_BIT:
   case DT_LOGIC:
   case DT_IMPLICIT:
      break;
   default:
      return false;
   }

   const int nranges = vlog_ranges(t);
   for (int i = 0; i < nranges; i++) {
      vlog_node_t r = vlog_range(t, i);
      if (vlog_subkind(r) != V_DIM_PACKED || vlog_size(r) == 0)
         return false;
      w *= vlog_size(r);
   }

   const bool atom = w > 1 && nranges == 0;
   *width = w;
   *two = vlog_subkind(t) == DT_BIT || (atom && vlog_subkind(t) != DT_INTEGER
                                         && vlog_subkind(t) != DT_TIME);
   *is_signed = !!(vlog_flags(t) & VLOG_F_SIGNED)
      || (atom && vlog_subkind(t) != DT_TIME && !(vlog_flags(t) & VLOG_F_SIGNED
                                                   && false));
   *elem = t;
   return true;
}

static void pack_unpacked_union(vlog_node_t v)
{
   // The members of an untagged union overlay each other from the least
   // significant bit so a union of integral types is held as a packed union

   if (vlog_flags(v) & (VLOG_F_PACKED | VLOG_F_TAGGED))
      return;

   const int ndecls = vlog_decls(v);
   bool integral = true;
   for (int i = 0; i < ndecls && integral; i++) {
      vlog_node_t d = vlog_decl(v, i);
      int width;
      bool two, is_signed;
      vlog_node_t elem;
      if (vlog_kind(d) != V_VAR_DECL
          || !union_member_elem_width(d, &width, &two, &is_signed, &elem))
         integral = false;

      const int nranges = integral ? vlog_ranges(d) : 0;
      for (int j = 0; j < nranges; j++) {
         vlog_node_t r = vlog_range(d, j);
         int64_t left, right;
         if (vlog_subkind(r) != V_DIM_UNPACKED
             || !vlog_range_bound(vlog_left(r), &left)
             || !vlog_range_bound(vlog_right(r), &right))
            integral = false;
      }
   }

   if (!integral) {
      if (ndecls > 1)
         warn_at(vlog_loc(v), "sorry, the members of an untagged union with "
                 "a member which is not integral do not share storage");
      return;
   }

   for (int i = 0; i < ndecls; i++) {
      vlog_node_t d = vlog_decl(v, i);
      int width;
      bool two, is_signed;
      vlog_node_t elem;
      union_member_elem_width(d, &width, &two, &is_signed, &elem);

      if (vlog_kind(elem) == V_DATA_TYPE
          && (vlog_subkind(elem) == DT_REAL || vlog_subkind(elem) == DT_REALTIME)) {
         // A real shares the bits of its IEEE representation
         vlog_node_t bits = vlog_new(V_DATA_TYPE);
         vlog_set_subkind(bits, DT_BIT);
         vlog_set_loc(bits, vlog_loc(d));

         vlog_node_t dim = vlog_new(V_DIMENSION);
         vlog_set_subkind(dim, V_DIM_PACKED);
         vlog_set_left(dim, foreach_number(63, vlog_loc(d)));
         vlog_set_right(dim, foreach_number(0, vlog_loc(d)));
         vlog_set_loc(dim, vlog_loc(d));
         vlog_add_range(bits, dim);

         vlog_set_type(d, bits);
         vlog_set_flags(d, VLOG_F_REALVIEW);
         continue;
      }

      const int nranges = vlog_ranges(d);
      if (nranges == 0)
         continue;

      // Index zero is the least significant element
      vlog_node_t t = vlog_new(V_DATA_TYPE);
      vlog_set_subkind(t, two ? DT_BIT : DT_LOGIC);
      vlog_set_loc(t, vlog_loc(d));

      bool ascending = false;
      for (int j = 0; j < nranges; j++) {
         vlog_node_t r = vlog_range(d, j);
         int64_t left, right;
         vlog_range_bound(vlog_left(r), &left);
         vlog_range_bound(vlog_right(r), &right);
         ascending = left < right;

         vlog_node_t dim = vlog_new(V_DIMENSION);
         vlog_set_subkind(dim, V_DIM_PACKED);
         vlog_set_left(dim, foreach_number(MAX(left, right), vlog_loc(d)));
         vlog_set_right(dim, foreach_number(MIN(left, right), vlog_loc(d)));
         vlog_set_loc(dim, vlog_loc(d));
         vlog_add_range(t, dim);
      }

      if (vlog_ranges(elem) == 0) {
         vlog_node_t dim = vlog_new(V_DIMENSION);
         vlog_set_subkind(dim, V_DIM_PACKED);
         vlog_set_left(dim, foreach_number(width - 1, vlog_loc(d)));
         vlog_set_right(dim, foreach_number(0, vlog_loc(d)));
         vlog_set_loc(dim, vlog_loc(d));
         vlog_add_range(t, dim);
      }
      else {
         const int ne = vlog_ranges(elem);
         for (int j = 0; j < ne; j++)
            vlog_add_range(t, vlog_range(elem, j));
      }

      // How the member is shown by %p
      vlog_add_param(t, foreach_number(nranges, vlog_loc(d)));
      vlog_add_param(t, foreach_number(is_signed, vlog_loc(d)));
      vlog_add_param(t, foreach_number(ascending, vlog_loc(d)));

      vlog_set_type(d, t);
      vlog_clear_ranges(d);
   }

   vlog_set_flags(v, vlog_flags(v) | VLOG_F_PACKED | VLOG_F_UNPACKED_VIEW);
}

typedef struct {
   vlog_node_t iface;
   vlog_node_t prefix;
} iface_type_t;

static bool iface_type_pred(vlog_node_t v, void *ctx)
{
   const iface_type_t *it = ctx;

   if (vlog_kind(v) == V_MOD_REF && !vlog_has_value(v)) {
      // The name of an instance inside the interface
      const int nstmts = vlog_stmts(it->iface);
      for (int i = 0; i < nstmts; i++) {
         vlog_node_t list = vlog_stmt(it->iface, i);
         if (vlog_kind(list) != V_INST_LIST)
            continue;

         const int ninst = vlog_stmts(list);
         for (int j = 0; j < ninst; j++) {
            vlog_node_t inst = vlog_stmt(list, j);
            if (vlog_kind(inst) == V_MOD_INST
                && vlog_ident(inst) == vlog_ident(v))
               return true;
         }
      }

      return false;
   }

   if (vlog_kind(v) != V_REF || !vlog_has_ref(v))
      return false;

   vlog_node_t d = vlog_ref(v);
   switch (vlog_kind(d)) {
   case V_PARAM_DECL:
   case V_LOCALPARAM:
      break;
   default:
      return false;
   }

   // The declaration may be a copy
   const int ndecls = vlog_decls(it->iface);
   for (int i = 0; i < ndecls; i++) {
      vlog_node_t p = vlog_decl(it->iface, i);
      if (p == d || (vlog_kind(p) == vlog_kind(d)
                     && vlog_ident(p) == vlog_ident(d)))
         return true;
   }

   return false;
}

static vlog_node_t iface_type_cb(vlog_node_t v, void *ctx)
{
   const iface_type_t *it = ctx;

   if (!iface_type_pred(v, ctx))
      return v;
   else if (vlog_kind(v) == V_MOD_REF) {
      // Relative to the instance which the type is selected from
      vlog_set_value(v, it->prefix);
      return v;
   }

   // The value of the parameter is known once the instance is elaborated
   vlog_node_t h = vlog_new(V_HIER_REF);
   vlog_set_value(h, it->prefix);
   vlog_set_ident(h, vlog_ident(v));
   vlog_set_loc(h, vlog_loc(v));
   vlog_set_global_flags(h, VLOG_GF_HIER_REF);
   return h;
}

static vlog_node_t nested_iface_module(vlog_node_t iface, ident_t id)
{
   const int nstmts = vlog_stmts(iface);
   for (int i = 0; i < nstmts; i++) {
      vlog_node_t list = vlog_stmt(iface, i);
      if (vlog_kind(list) != V_INST_LIST)
         continue;

      const int ninst = vlog_stmts(list);
      for (int j = 0; j < ninst; j++) {
         vlog_node_t inst = vlog_stmt(list, j);
         if (vlog_kind(inst) == V_MOD_INST && vlog_ident(inst) == id)
            return lookup_interface(vlog_ident(list));
      }
   }

   return NULL;
}

static vlog_node_t hier_type_decl(int len)
{
   // Find the type that the tokens name without consuming them

   vlog_node_t root = peek_reference(), iface = NULL;
   if (root == NULL)
      return NULL;
   else if (vlog_kind(root) == V_IFACE_PORT)
      iface = lookup_interface(vlog_ident2(root));
   else if (inst_lists != NULL && vlog_kind(root) == V_MOD_INST) {
      vlog_node_t list = hash_get(inst_lists, root);
      if (list != NULL)
         iface = lookup_interface(vlog_ident(list));
   }

   ident_t last = NULL;
   for (int n = 1; iface != NULL && n <= len; n++) {
      const token_t tok = peek_nth(n);
      if (tok == tLSQUARE) {
         for (int depth = 1; depth > 0 && n < len; ) {
            n++;
            if (peek_nth(n) == tLSQUARE)
               depth++;
            else if (peek_nth(n) == tRSQUARE)
               depth--;
         }
      }
      else if (tok == tID && n > 1 && peek_nth(n - 1) == tDOT) {
         ident_t name = peek_ident_nth(n);
         if (n == len) {
            last = name;
            break;
         }

         iface = nested_iface_module(iface, name);
      }
   }

   if (iface == NULL || last == NULL)
      return NULL;

   const int ndecls = vlog_decls(iface);
   for (int i = 0; i < ndecls; i++) {
      vlog_node_t d = vlog_decl(iface, i);
      if (vlog_has_ident(d) && vlog_ident(d) == last)
         return is_data_type(d) ? d : NULL;
   }

   return NULL;
}

static vlog_node_t p_hier_type(void)
{
   // ifc_name { . nested_instance } . type_identifier

   BEGIN("type declared in an interface");

   ident_t id = p_identifier();

   vlog_node_t prefix = vlog_new(V_MOD_REF);
   vlog_set_ident(prefix, id);
   vlog_set_loc(prefix, &state.last_loc);
   vlog_symtab_lookup(symtab, prefix);

   if (peek() == tLSQUARE)
      p_instance_index(prefix);

   vlog_node_t iface = hier_owner(prefix);
   if (iface == NULL && vlog_has_ref(prefix) && inst_lists != NULL
       && vlog_kind(vlog_ref(prefix)) == V_MOD_INST) {
      vlog_node_t list = hash_get(inst_lists, vlog_ref(prefix));
      if (list != NULL)
         iface = lookup_interface(vlog_ident(list));
   }

   ident_t last = NULL;
   loc_t last_loc = state.last_loc;
   for (;;) {
      consume(tDOT);

      ident_t name = p_identifier();
      last_loc = state.last_loc;

      if (peek() != tDOT && !at_instance_index(true)) {
         last = name;
         break;
      }

      if (iface != NULL)
         iface = nested_iface_module(iface, name);

      vlog_node_t next = vlog_new(V_MOD_REF);
      vlog_set_ident(next, name);
      vlog_set_loc(next, &last_loc);
      vlog_set_value(next, prefix);
      prefix = next;

      if (at_instance_index(true))
         p_instance_index(prefix);
   }

   if (iface == NULL) {
      error_at(&last_loc, "sorry, cannot find the interface that %pQ is "
               "declared in", last);
      return logic_type();
   }

   vlog_node_t decl = NULL;
   const int ndecls = vlog_decls(iface);
   for (int i = 0; i < ndecls && decl == NULL; i++) {
      vlog_node_t d = vlog_decl(iface, i);
      if (vlog_has_ident(d) && vlog_ident(d) == last)
         decl = d;
   }

   if (decl == NULL) {
      error_at(&last_loc, "no type named %pQ in interface %pQ", last,
               vlog_ident2(iface));
      return logic_type();
   }
   else if (!is_data_type(decl)) {
      error_at(&last_loc, "%pQ is not a data type", last);
      return logic_type();
   }

   // Hierarchical references copied from the interface may be in an
   // arena without the flag
   vlog_set_global_flags(prefix, VLOG_GF_HIER_REF);

   iface_type_t it = { .iface = iface, .prefix = prefix };
   vlog_node_t copy = vlog_copy(decl, iface_type_pred, &it);
   if (copy != decl)
      copy = vlog_rewrite(copy, NULL, iface_type_cb, &it);

   if (peek() == tLSQUARE)
      copy = p_typedef_dimensions(copy);

   return copy;
}

static vlog_node_t p_data_type(void)
{
   // integer_vector_type [ signing ] { packed_dimension }
   //   | integer_atom_type [ signing ] | non_integer_type
   //   | struct_union [ packed [ signing ] ]
   //       { struct_union_member { struct_union_member } } { packed_dimension }
   //   | enum [ enum_base_type ] { enum_name_declaration
   //       { , enum_name_declaration } } { packed_dimension }
   //   | string | chandle
   //   | virtual [ interface ] interface_identifier
   //       [ parameter_value_assignment ] [ . modport_identifier ]
   //   | [ class_scope | package_scope ] type_identifier { packed_dimension }
   //   | class_type | event | ps_covergroup_identifier | type_reference

   BEGIN("data type");

   switch (peek()) {
   case tBIT:
   case tLOGIC:
   case tREG:
      {
         vlog_node_t v = p_integer_vector_type();

         if (scan(tSIGNED, tUNSIGNED))
            vlog_set_flags(v, p_signing());

         while (peek() == tLSQUARE)
            vlog_add_range(v, p_packed_dimension());

         return v;
      }

   case tBYTE:
   case tSHORTINT:
   case tSVINT:
   case tLONGINT:
   case tINTEGER:
   case tTIME:
      {
         vlog_node_t v = p_integer_atom_type();

         if (scan(tSIGNED, tUNSIGNED))
            vlog_set_flags(v, p_signing());
         else if (vlog_subkind(v) != DT_TIME)
            vlog_set_flags(v, VLOG_F_SIGNED);

         return v;
      }

   case tSVREAL:
   case tREALTIME:
   case tSHORTREAL:
      return p_non_integer_type();

   case tSTRUCT:
   case tUNION:
      {
         vlog_node_t v = p_struct_union();

         if (optional(tPACKED)) {
            vlog_set_flags(v, VLOG_F_PACKED);

            if (scan(tSIGNED, tUNSIGNED))
               vlog_set_flags(v, p_signing());
         }

         consume(tLBRACE);

         vlog_symtab_push(symtab, v);

         do {
            p_struct_union_member(v);
         } while (not_at_token(tRBRACE));

         vlog_symtab_pop(symtab);

         consume(tRBRACE);

         if (vlog_kind(v) == V_UNION_DECL)
            pack_unpacked_union(v);

         if (typedef_depth == 0)
            name_anonymous_type(v, false);

         if ((vlog_flags(v) & VLOG_F_TAGGED) && !(vlog_flags(v) & VLOG_F_PACKED)) {
            // The tag of an unpacked tagged union is held in an extra member
            vlog_node_t tag = vlog_new(V_VAR_DECL);
            vlog_set_ident(tag, ident_new("$tag"));
            vlog_set_type(tag, make_integer_atom_type(DT_INT));
            vlog_set_loc(tag, &state.last_loc);

            // No member is tagged until the union is assigned
            vlog_node_t none = vlog_new(V_NUMBER);
            vlog_set_number(none, number_from_int(-1));
            vlog_set_loc(none, &state.last_loc);
            vlog_set_value(tag, none);

            vlog_add_decl(v, tag);
         }

         if (peek() == tLSQUARE) {
            error_at(&state.last_loc, "sorry, a packed array of a structure "
                     "or union is not supported");
            while (peek() == tLSQUARE)
               (void)p_packed_dimension();
         }

         vlog_set_loc(v, CURRENT_LOC);
         return v;
      }

   case tENUM:
      {
         consume(tENUM);

         vlog_node_t v = vlog_new(V_ENUM_DECL);

         if (peek() != tLBRACE)
            vlog_set_type(v, p_enum_base_type());
         else {
            vlog_node_t dt = make_integer_atom_type(DT_INT);
            vlog_set_flags(dt, VLOG_F_SIGNED);

            vlog_set_type(v, dt);
         }

         consume(tLBRACE);

         do {
            p_enum_name_declaration(v);
         } while (optional(tCOMMA));

         consume(tRBRACE);

         while (peek() == tLSQUARE)
            vlog_add_range(v, p_packed_dimension());

         if (typedef_depth == 0)
            name_anonymous_type(v, true);

         vlog_set_loc(v, CURRENT_LOC);
         return v;
      }

   case tEVENT:
      {
         consume(tEVENT);

         vlog_node_t v = vlog_new(V_DATA_TYPE);
         vlog_set_subkind(v, DT_EVENT);
         vlog_set_loc(v, &state.last_loc);

         return v;
      }

   case tTYPE:
      {
         // type_reference ::= type ( expression )

         consume(tTYPE);
         consume(tLPAREN);

         if ((scan(tLOGIC, tBIT, tREG, tINTEGER, tSVINT, tSHORTINT,
                   tLONGINT, tBYTE, tTIME, tSVREAL, tREALTIME, tSHORTREAL,
                   tSTRINGK, tSTRUCT, tUNION, tENUM, tEVENT, tCHANDLE)
              && peek_nth(2) != tTICK)
             || (peek() == tID && is_type_name(peek_reference()))
             || (peek() == tID && is_scoped_type_start())) {
            vlog_node_t dt = p_data_type();
            consume(tRPAREN);
            return dt;
         }

         vlog_node_t e = p_expression();

         consume(tRPAREN);

         vlog_node_t type = vlog_get_type(e);
         if (type != NULL)
            return type;

         if (vlog_is_real(e)) {
            vlog_node_t real = vlog_new(V_DATA_TYPE);
            vlog_set_subkind(real, DT_REAL);
            vlog_set_loc(real, &state.last_loc);
            return real;
         }

         // The type of an expression is a vector as wide as the expression
         const int width = vlog_width(e);
         if (width <= 0) {
            error_at(&state.last_loc, "sorry, the type operator is not "
                     "supported for this expression");
            return implicit_type();
         }

         vlog_node_t left = vlog_new(V_NUMBER);
         vlog_set_number(left, number_from_int(width - 1));

         vlog_node_t right = vlog_new(V_NUMBER);
         vlog_set_number(right, number_from_int(0));

         vlog_node_t r = vlog_new(V_DIMENSION);
         vlog_set_subkind(r, V_DIM_PACKED);
         vlog_set_left(r, left);
         vlog_set_right(r, right);

         vlog_node_t vec = vlog_new(V_DATA_TYPE);
         vlog_set_subkind(vec, vlog_is_four_state(e) ? DT_LOGIC : DT_BIT);
         vlog_add_range(vec, r);
         if (vlog_is_signed(e))
            vlog_set_flags(vec, VLOG_F_SIGNED);
         vlog_set_loc(vec, &state.last_loc);
         return vec;
      }

   case tVIRTUAL:
      {
         consume(tVIRTUAL);
         optional(tINTERFACE);

         vlog_node_t v = vlog_new(V_DATA_TYPE);
         vlog_set_subkind(v, DT_VIRTUAL);
         vlog_set_ident(v, p_identifier());
         vlog_set_loc(v, &state.last_loc);
         scan_note_name(vlog_ident(v));

         // An interface from an earlier analysis is found again when the
         // type is used as it may be declared again later in this one
         vlog_node_t iface = lookup_interface(vlog_ident(v));
         if (iface != NULL && interfaces != NULL
             && hash_get(interfaces, vlog_ident(v)) == iface)
            vlog_set_ref(v, iface);

         if (peek() == tHASH) {
            vlog_node_t list = vlog_new(V_INST_LIST);

            // A type parameter of the interface takes a data type
            vlog_node_t saved_target = param_target;
            param_target = iface != NULL && vlog_kind(iface) == V_MODULE
               ? iface : NULL;
            p_parameter_value_assignment(list);
            param_target = saved_target;

            const int nparams = vlog_params(list);
            for (int i = 0; i < nparams; i++)
               vlog_add_param(v, vlog_param(list, i));
         }

         if (optional(tDOT)) {
            vlog_node_t mp = p_modport_name(iface);
            if (mp != NULL && vlog_has_ref(v))
               vlog_set_type(v, mp);
         }

         return v;
      }

   case tID:
      {
         int hlen;
         if (is_hier_type_name(&hlen))
            return p_hier_type();

         if (peek_nth(2) == tSCOPE && !is_process_name()) {
            vlog_node_t scope = p_scope_prefix();
            ident_t id = p_identifier();
            vlog_node_t d = scope ? vlog_scope_member(scope, id) : NULL;
            if (d == NULL && scope != NULL)
               error_at(&state.last_loc, "%pQ is not a member of %s %pQ", id,
                        vlog_kind(scope) == V_PACKAGE ? "package" : "class",
                        vlog_ident(scope));
            else if (d != NULL && !is_data_type(d)) {
               error_at(&state.last_loc, "%pQ is not a data type", id);
               d = NULL;
            }
            else if (d != NULL && vlog_kind(d) == V_CLASS_DECL
                     && (vlog_flags(d) & VLOG_F_GENERIC))
               return specialise_class(d);

            if (d != NULL && peek() == tLSQUARE)
               d = p_typedef_dimensions(d);

            return d ?: logic_type();
         }

         ident_t id = p_identifier();
         if (id == ident_new("process")
             && vlog_symtab_query(symtab, id) == NULL) {
            if (optional(tSCOPE)) {
               // process::state is an integer
               ident_t member = p_identifier();
               if (member != ident_new("state"))
                  error_at(&state.last_loc, "process has no type named %pQ",
                           member);
               return make_integer_atom_type(DT_INT);
            }

            vlog_node_t v = vlog_new(V_DATA_TYPE);
            vlog_set_subkind(v, DT_PROCESS);
            vlog_set_loc(v, &state.last_loc);
            return v;
         }

         if (id == ident_new("mailbox")
             && vlog_symtab_query(symtab, id) == NULL) {
            vlog_node_t v = vlog_new(V_DATA_TYPE);
            vlog_set_subkind(v, DT_MAILBOX);
            vlog_set_loc(v, &state.last_loc);

            if (optional(tHASH)) {
               consume(tLPAREN);
               vlog_set_type(v, p_data_type());
               consume(tRPAREN);
            }
            else
               vlog_set_type(v, make_integer_atom_type(DT_INT));

            return v;
         }

         if (id == ident_new("semaphore")
             && vlog_symtab_query(symtab, id) == NULL) {
            vlog_node_t v = vlog_new(V_DATA_TYPE);
            vlog_set_subkind(v, DT_SEMAPHORE);
            vlog_set_loc(v, &state.last_loc);
            vlog_set_type(v, make_integer_atom_type(DT_INT));
            return v;
         }

         vlog_node_t d = vlog_symtab_query(symtab, id);
         if (d == NULL && peek() == tHASH && lib_scan_active())
            scan_skip_parameters();
         else if (d != NULL && vlog_kind(d) == V_CLASS_DECL
             && (vlog_flags(d) & VLOG_F_GENERIC))
            return specialise_class(d);
         else if (d != NULL && peek() == tHASH && vlog_kind(d) == V_TYPE_DECL
                  && generic_classes != NULL && hash_get(generic_classes, d))
            return specialise_class(d);   // Own name inside class body
         else if (d != NULL && peek() == tHASH && vlog_kind(d) == V_CLASS_DECL
                  && (vlog_flags(d) & VLOG_F_FORWARD))
            return specialise_forward(d);

         if (d != NULL && vlog_kind(d) == V_TYPE_DECL
             && (vlog_flags(d) & VLOG_F_NETTYPE))
            parse_error(&state.last_loc, "sorry, nets of a user-defined net "
                        "type are not supported");

         vlog_node_t dt = get_data_type(id);
         if (peek() == tLSQUARE)
            dt = p_typedef_dimensions(dt);

         return dt;
      }

   case tCHANDLE:
      {
         consume(tCHANDLE);

         vlog_node_t v = vlog_new(V_DATA_TYPE);
         vlog_set_subkind(v, DT_CHANDLE);
         vlog_set_loc(v, &state.last_loc);

         return v;
      }

   case tSTRINGK:
      {
         consume(tSTRINGK);

         vlog_node_t v = vlog_new(V_DATA_TYPE);
         vlog_set_subkind(v, DT_STRING);
         vlog_set_loc(v, &state.last_loc);

         return v;
      }

   default:
      one_of(tBIT, tLOGIC, tREG, tBYTE, tSHORTINT, tSVINT, tLONGINT, tINTEGER,
             tTIME, tSVREAL, tREALTIME, tSHORTREAL, tSTRUCT, tUNION, tENUM,
             tEVENT, tID, tSTRINGK);
      return logic_type();
   }
}

static vlog_node_t p_implicit_data_type(void)
{
   // [ signing ] { packed_dimension }

   BEGIN("implicit data type");

   vlog_node_t v = vlog_new(V_DATA_TYPE);
   vlog_set_subkind(v, DT_IMPLICIT);

   if (scan(tSIGNED, tUNSIGNED))
      vlog_set_flags(v, p_signing());

   while (peek() == tLSQUARE)
      vlog_add_range(v, p_packed_dimension());

   vlog_set_loc(v, CURRENT_LOC);
   return v;
}

static bool is_type_name(vlog_node_t d)
{
   return d != NULL && is_data_type(d);
}

static vlog_node_t p_data_type_or_implicit(void)
{
   // data_type | implicit_data_type

   BEGIN("data type or implicit");

   switch (peek()) {
   case tID:
      if (peek_nth(2) == tID || peek_nth(2) == tHASH || is_scoped_type_start()
          || (is_process_name() && peek_nth(2) == tSCOPE)
          || (peek_nth(2) == tLSQUARE && is_type_name(peek_reference())))
         return p_data_type();
      else
         return p_implicit_data_type();
      break;
   case tVIRTUAL:
   case tREG:
   case tSTRUCT:
   case tUNION:
   case tENUM:
   case tSVINT:
   case tINTEGER:
   case tSVREAL:
   case tSHORTREAL:
   case tREALTIME:
   case tTIME:
   case tLOGIC:
   case tBIT:
   case tSHORTINT:
   case tLONGINT:
   case tBYTE:
   case tEVENT:
   case tTYPE:
   case tSTRINGK: case tCHANDLE:
      return p_data_type();
   default:
      return p_implicit_data_type();
   }
}

static void p_net_port_type(vlog_net_kind_t *kind, vlog_node_t *dt)
{
   // [ net_type ] data_type_or_implicit | net_type_identifier
   //   | interconnect implicit_data_type

   BEGIN("net port type");

   vlog_node_t nettype = nettype_lookup();
   if (nettype != NULL && peek_nth(2) == tID) {
      consume(tID);
      *kind = V_NET_WIRE;
      *dt = nettype;
      return;
   }

   if (optional(tINTERCONNECT)) {
      *kind = V_NET_WIRE;
      *dt = p_implicit_data_type();
      vlog_set_flags(*dt, vlog_flags(*dt) | VLOG_F_USERNET);
      return;
   }

   if (scan(tSUPPLY0, tSUPPLY1, tTRI, tTRIAND, tTRIOR, tTRIREG,
            tTRI0, tTRI1, tUWIRE, tWIRE, tWAND, tWOR))
      *kind = p_net_type();
   else
      *kind = V_NET_WIRE;

   *dt = p_data_type_or_implicit();
}

static vlog_node_t p_var_data_type(void)
{
   // data_type | var data_type_or_implicit

   BEGIN("var data type");

   return p_data_type();
}

static vlog_node_t p_variable_port_type(void)
{
   // var_data_type

   BEGIN("variable port type");

   return p_var_data_type();
}

static void p_list_of_port_identifiers(vlog_node_t mod, v_port_kind_t kind,
                                       vlog_node_t datatype)
{
   // port_identifier { unpacked_dimension }
   //    { , port_identifier { unpacked_dimension } }

   BEGIN("list of port identifiers");

   do {
      ident_t id, ext;
      p_external_identifier(&id, &ext);

      vlog_node_t v = vlog_new(V_PORT_DECL);
      vlog_set_subkind(v, kind);
      vlog_set_ident(v, id);
      vlog_set_ident2(v, ext);
      vlog_set_type(v, datatype);
      vlog_set_loc(v, &state.last_loc);

      vlog_add_decl(mod, v);
      vlog_symtab_put(symtab, v);

      declare_port(mod, v);
   } while (optional(tCOMMA));
}

static void p_list_of_variable_port_identifiers(vlog_node_t mod,
                                                v_port_kind_t kind,
                                                vlog_node_t datatype)
{
   // port_identifier { variable_dimension } [ = constant_expression ]
   //   { , port_identifier { variable_dimension } [ = constant_expression ] }

   BEGIN("list of variable port identifiers");

   do {
      ident_t id, ext;
      p_external_identifier(&id, &ext);

      vlog_node_t v = vlog_new(V_PORT_DECL);
      vlog_set_subkind(v, kind);
      vlog_set_ident(v, id);
      vlog_set_ident2(v, ext);
      vlog_set_type(v, datatype);
      vlog_set_loc(v, &state.last_loc);

      if (optional(tEQ))
         vlog_set_value(v, p_constant_expression());

      vlog_add_decl(mod, v);
      vlog_symtab_put(symtab, v);

      declare_port(mod, v);
   } while (optional(tCOMMA));
}

static void p_inout_declaration(vlog_node_t mod)
{
   // inout net_port_type list_of_port_identifiers

   BEGIN("inout declaration");

   consume(tINOUT);

   vlog_node_t dt;
   vlog_net_kind_t kind;
   p_net_port_type(&kind, &dt);

   p_list_of_port_identifiers(mod, V_PORT_INOUT, dt);
}

static void p_input_declaration(vlog_node_t mod)
{
   // input net_port_type list_of_port_identifiers
   //   | input variable_port_type list_of_variable_identifiers

   BEGIN("input declaration");

   consume(tINPUT);

   vlog_node_t dt;
   vlog_net_kind_t kind;
   switch (peek()) {
   case tREG:
      dt = p_variable_port_type();
      p_list_of_variable_port_identifiers(mod, V_PORT_INPUT, dt);
      break;
   default:
      p_net_port_type(&kind, &dt);
      p_list_of_port_identifiers(mod, V_PORT_INPUT, dt);
      break;
   }
}

static void p_output_declaration(vlog_node_t mod)
{
   // output net_port_type list_of_port_identifiers
   //   | output variable_port_type list_of_variable_port_identifiers

   BEGIN("output declaration");

   consume(tOUTPUT);

   switch (peek()) {
   case tREG:
      {
         vlog_node_t dt = p_variable_port_type();
         p_list_of_variable_port_identifiers(mod, V_PORT_OUTPUT, dt);
      }
      break;
   default:
      {
         vlog_node_t dt;
         vlog_net_kind_t kind;
         p_net_port_type(&kind, &dt);
         p_list_of_port_identifiers(mod, V_PORT_OUTPUT, dt);
      }
      break;
   }
}

static void p_port_declaration(vlog_node_t mod)
{
   // { attribute_instance } inout_declaration
   //   | { attribute_instance } input_declaration
   //   | { attribute_instance } output_declaration
   //   | { attribute_instance } ref_declaration
   //   | { attribute_instance } interface_port_declaration

   BEGIN("port declaration");

   switch (peek()) {
   case tINOUT: p_inout_declaration(mod); break;
   case tINPUT: p_input_declaration(mod); break;
   case tOUTPUT: p_output_declaration(mod); break;
   default: should_not_reach_here();
   }
}

static void p_net_port_header(v_port_kind_t *dir, vlog_node_t *dt)
{
   // [ port_direction ] net_port_type

   BEGIN("net port header");

   if (optional(tINPUT))
      *dir = V_PORT_INPUT;
   else if (optional(tINOUT))
      *dir = V_PORT_INOUT;
   else if (optional(tOUTPUT))
      *dir = V_PORT_OUTPUT;
   else if (optional(tREF))
      *dir = V_PORT_REF;

   vlog_net_kind_t kind;
   p_net_port_type(&kind, dt);
}

static void p_part_select_range(vlog_node_t ps)
{
   // constant_expression : constant_expression
   //   | expression +: constant_expression
   //   | expression -: constant_expression

   BEGIN("part select range");

   vlog_range_kind_t kind = V_RANGE_CONST;
   switch (one_of(tCOLON, tINDEXPOS, tINDEXNEG)) {
   case tINDEXPOS: kind = V_RANGE_POS; break;
   case tINDEXNEG: kind = V_RANGE_NEG; break;
   }

   vlog_set_subkind(ps, kind);
   vlog_set_right(ps, p_constant_expression());
}

typedef struct {
   vlog_node_t iface;
   vlog_node_t prefix;
} modport_expr_t;

static bool modport_expr_pred(vlog_node_t v, void *ctx)
{
   return vlog_kind(v) == V_REF;
}

static vlog_node_t modport_expr_cb(vlog_node_t v, void *ctx)
{
   const modport_expr_t *me = ctx;

   if (vlog_kind(v) != V_REF || !vlog_has_ref(v))
      return v;

   vlog_node_t decl = vlog_ref(v);
   const int ndecls = vlog_decls(me->iface);
   for (int i = 0; i < ndecls; i++) {
      if (vlog_decl(me->iface, i) != decl)
         continue;

      vlog_node_t h = vlog_new(V_HIER_REF);
      vlog_set_value(h, me->prefix);
      vlog_set_ident(h, vlog_ident(v));
      vlog_set_loc(h, vlog_loc(v));
      vlog_set_global_flags(h, VLOG_GF_HIER_REF);
      return h;
   }

   return v;
}

static void bind_interface_member(vlog_node_t v, vlog_node_t iface)
{
   // The type of a member is known unless it depends on a parameter of
   // the interface

   const int ndecls = vlog_decls(iface);
   for (int i = 0; i < ndecls; i++) {
      switch (vlog_kind(vlog_decl(iface, i))) {
      case V_PARAM_DECL:
      case V_LOCALPARAM:
         return;
      default:
         break;
      }
   }

   for (int i = 0; i < ndecls; i++) {
      vlog_node_t d = vlog_decl(iface, i);
      if (vlog_kind(d) == V_NET_DECL && vlog_ident(d) == vlog_ident(v)) {
         vlog_set_ref(v, d);
         return;
      }
   }
}

static vlog_node_t check_modport_member(vlog_node_t v)
{
   // Only the items listed in the modport of an interface port are visible

   vlog_node_t mr = vlog_value(v);
   if (vlog_kind(mr) != V_MOD_REF || vlog_has_value(mr) || !vlog_has_ref(mr)
       || vlog_kind(vlog_ref(mr)) != V_IFACE_PORT)
      return v;

   vlog_node_t port = vlog_ref(mr);
   if (!vlog_has_ref(port) || vlog_ident(v) == ident_new("$self"))
      return v;

   vlog_node_t mp = vlog_ref(port);
   const int nmembers = vlog_decls(mp);
   for (int i = 0; i < nmembers; i++) {
      vlog_node_t m = vlog_decl(mp, i);
      if (vlog_ident(m) != vlog_ident(v))
         continue;
      vlog_node_t iface = lookup_interface(vlog_ident2(port));
      if (iface == NULL)
         return v;
      else if (!vlog_has_value(m)) {
         bind_interface_member(v, iface);
         return v;
      }

      modport_expr_t me = { .iface = iface, .prefix = mr };
      vlog_node_t expr = vlog_copy(vlog_value(m), modport_expr_pred, &me);
      return vlog_rewrite(expr, NULL, modport_expr_cb, &me);
   }

   vlog_node_t iface = lookup_interface(vlog_ident2(port));
   if (iface == NULL)
      return v;

   const int ndecls = vlog_decls(iface);
   for (int i = 0; i < ndecls; i++) {
      vlog_node_t d = vlog_decl(iface, i);
      if (vlog_has_ident(d) && vlog_ident(d) == vlog_ident(v)) {
         error_at(vlog_loc(v), "interface item %pQ is not in modport %pQ",
                  vlog_ident(v), vlog_ident(mp));
         break;
      }
   }

   return v;
}

static vlog_node_t p_instance_index(vlog_node_t mr)
{
   // [ constant_expression ]

   consume(tLSQUARE);
   vlog_add_param(mr, p_expression());
   consume(tRSQUARE);

   return mr;
}

static bool at_instance_index(bool dot)
{
   // A bit select of an instance array must be followed by a dot if it is
   // part of a hierarchical name

   if (peek() != tLSQUARE)
      return false;

   int n = 2;
   for (int depth = 1; depth > 0 && n < TOKENQ_SIZE - 2; n++) {
      const token_t tok = peek_nth(n);
      if (tok == tLSQUARE)
         depth++;
      else if (tok == tRSQUARE)
         depth--;
      else if (tok == tEOF || tok == tSEMI)
         return false;
   }

   return !dot || peek_nth(n) == tDOT;
}

static vlog_node_t interface_as_value(ident_t id, const loc_t *loc)
{
   vlog_node_t mr = vlog_new(V_MOD_REF);
   vlog_set_ident(mr, id);
   vlog_set_loc(mr, loc);
   vlog_symtab_lookup(symtab, mr);

   if (at_instance_index(false))
      p_instance_index(mr);

   vlog_node_t v = vlog_new(V_HIER_REF);
   vlog_set_value(v, mr);
   vlog_set_ident(v, ident_new("$self"));
   vlog_set_loc(v, loc);
   vlog_set_global_flags(v, VLOG_GF_HIER_REF);
   return v;
}

static bool vlog_always_true_cb(vlog_node_t v, void *ctx)
{
   // Copy an expression but not the declarations it refers to

   switch (vlog_kind(v)) {
   case V_REF:
   case V_EVENT:
   case V_EVENT_CONTROL:
   case V_DELAY_CONTROL:
   case V_NUMBER:
   case V_BINARY:
   case V_UNARY:
      return true;
   default:
      return false;
   }
}

static bool iface_declares(vlog_node_t node, ident_t id)
{
   // The interface or module declares an item with this name

   if (node != NULL && vlog_kind(node) == V_DATA_TYPE && vlog_has_ref(node))
      node = vlog_ref(node);

   if (node == NULL || vlog_kind(node) != V_MODULE)
      return false;

   const int ndecls = vlog_decls(node);
   for (int i = 0; i < ndecls; i++) {
      vlog_node_t d = vlog_decl(node, i);
      if (vlog_has_ident(d) && vlog_ident(d) == id)
         return true;
   }

   return false;
}

static bool is_clocking_name(ident_t id, vlog_node_t owner)
{
   if (owner != NULL)
      return iface_declares(owner, ident_sprintf("%s$ev", istr(id)));

   return clocking_names != NULL && hset_contains(clocking_names, id);
}

static vlog_node_t hier_owner(vlog_node_t mr)
{
   // The interface which an instance or port refers to if it is known

   if (vlog_kind(mr) != V_MOD_REF || !vlog_has_ref(mr))
      return NULL;

   vlog_node_t port = vlog_ref(mr);
   switch (vlog_kind(port)) {
   case V_IFACE_PORT:
      return lookup_interface(vlog_ident2(port));
   case V_MOD_INST:
      {
         ident_t name = inst_modules != NULL && vlog_ranges(port) == 0
            ? hash_get(inst_modules, port) : NULL;
         return name != NULL ? lookup_interface(name) : NULL;
      }
   default:
      return NULL;
   }
}

static ident_t clocking_member(ident_t cb)
{
   // A clockvar of a clocking block is a variable named after both, and the
   // clocking block on its own stands for its event

   if (peek() == tDOT && peek_nth(2) == tID) {
      consume(tDOT);
      ident_t sig = p_identifier();
      return ident_sprintf("%s$%s", istr(cb), istr(sig));
   }

   return ident_sprintf("%s$ev", istr(cb));
}

static vlog_node_t p_hierarchical_identifier(ident_t id)
{
   // [ $root . ] { identifier constant_bit_select . } identifier

   EXTEND("hierarchical identifier");

   if (id == NULL)
      id = p_identifier();

   vlog_node_t mr = vlog_new(V_MOD_REF);
   vlog_set_ident(mr, id);
   vlog_set_loc(mr, CURRENT_LOC);
   vlog_symtab_lookup(symtab, mr);

   if (at_instance_index(true))
      p_instance_index(mr);

   ident_t suffix;
   for (;;) {
      consume(tDOT);

      suffix = p_identifier();

      if (is_clocking_name(suffix, hier_owner(mr)))
         suffix = clocking_member(suffix);

      const bool indexed = at_instance_index(true);
      if (peek() != tDOT && !indexed)
         break;

      vlog_node_t next = vlog_new(V_MOD_REF);
      vlog_set_ident(next, suffix);
      vlog_set_loc(next, &state.last_loc);
      vlog_set_value(next, mr);

      if (indexed)
         p_instance_index(next);

      mr = next;
   };

   vlog_node_t v = vlog_new(V_HIER_REF);
   vlog_set_value(v, mr);
   vlog_set_ident(v, suffix);
   vlog_set_loc(v, CURRENT_LOC);

   if (current_package != NULL && current_module == NULL) {
      // Items in a package cannot refer to the design hierarchy (26.2)
      vlog_node_t root = mr;
      while (vlog_has_value(root) && vlog_kind(vlog_value(root)) == V_MOD_REF)
         root = vlog_value(root);

      diag_t *d = diag_new(DIAG_ERROR, vlog_loc(v));
      diag_printf(d, "hierarchical reference to %pQ in package %pQ",
                  vlog_ident(root), vlog_ident2(current_package));
      diag_hint(d, vlog_loc(v), "a package can only refer to names declared "
                "in it or imported from another package");
      diag_emit(d);
   }

   vlog_set_global_flags(v, VLOG_GF_HIER_REF);
   return check_modport_member(v);
}

static void p_inline_constraints(vlog_node_t call, vlog_node_t class)
{
   // with [ ( [ identifier_list ] ) ] constraint_block

   BEGIN("inline constraints");

   consume(tWITH);

   if (peek() == tLPAREN && class == NULL) {
      error_at(&(state.tokenq[state.tokenq_tail].loc), "sorry, an identifier "
               "list after with is not supported");
      drop_tokens_until(&state, tLBRACE);
   }
   else if (optional(tLPAREN)) {
      // Only the listed names are members of the object, all others are
      // looked up in the scope of the call
      ident_t *names LOCAL = NULL;
      int nnames = 0, max = 0;
      if (peek() != tRPAREN) {
         do {
            if (nnames == max)
               names = xrealloc_array(names, max = MAX(max * 2, 4),
                                      sizeof(ident_t));
            names[nnames++] = p_identifier();
         } while (optional(tCOMMA));
      }
      consume(tRPAREN);

      vlog_symtab_pop(symtab);
      vlog_symtab_push_members(symtab, class, names, nnames);

      consume(tLBRACE);
   }
   else
      consume(tLBRACE);

   while (not_at_token(tRBRACE)) {
      if (!optional(tSEMI))   // An empty item
         vlog_add_stmt(call, p_constraint_expression());
   }

   consume(tRBRACE);
}

static vlog_node_t p_method_call_rest(vlog_node_t prefix, ident_t id)
{
   // method_call_body with the prefix and identifier already parsed

   EXTEND("method call");

   vlog_node_t v = vlog_new(V_METHOD_CALL);
   vlog_set_value(v, prefix);
   vlog_set_ident(v, id);

   // Names in the arguments and constraints of randomize are members of
   // the class of the object before anything in the caller's scope
   vlog_node_t class = NULL, type = NULL;
   const bool is_randomize = id == ident_new("randomize");
   if (is_randomize && (type = vlog_get_type(prefix)) != NULL
       && vlog_kind(type) == V_CLASS_DECL)
      class = type;

   vlog_scope_t *saved_local = local_scope;
   if (class != NULL) {
      local_scope = vlog_symtab_current(symtab);
      vlog_symtab_push_members(symtab, class, NULL, 0);
   }

   vlog_node_t saved_iter = iter_decl, saved_index = iter_index;
   vlog_node_t arraytype = NULL, prefix_type = NULL;
   if (is_array_method_name(id) && (type = vlog_get_type(prefix)) != NULL
       && vlog_kind(type) != V_CLASS_DECL) {
      arraytype = prefix_type = type;
      if (vlog_kind(type) == V_DATA_TYPE
          && (vlog_subkind(type) == DT_DYNARRAY
              || vlog_subkind(type) == DT_QUEUE
              || vlog_subkind(type) == DT_ASSOC))
         arraytype = vlog_type(type);   // Element type
   }

   // An optional iterator name replaces the default "item"
   ident_t iter_name = ident_new("item");
   if (arraytype != NULL && peek() == tLPAREN && peek_nth(2) == tID
       && peek_nth(3) == tRPAREN) {
      consume(tLPAREN);
      iter_name = p_identifier();
      consume(tRPAREN);
   }
   else if (optional(tLPAREN)) {
      p_list_of_arguments(v);
      consume(tRPAREN);
   }

   // The queries of a covergroup that report the bins have the same names
   if (vlog_params(v) == 2 && id == ident_new("get_inst_coverage"))
      vlog_set_ident(v, ident_new("get_inst_coverage_n"));
   else if (vlog_params(v) == 2 && id == ident_new("get_coverage"))
      vlog_set_ident(v, ident_new("get_coverage_n"));

   if (is_randomize && peek() == tWITH)
      p_inline_constraints(v, class);
   else if (arraytype != NULL && peek() == tWITH) {
      consume(tWITH);

      vlog_node_t item = vlog_new(V_LOCAL_DECL);
      vlog_set_ident(item, iter_name);
      vlog_set_type(item, arraytype);
      vlog_set_loc(item, &state.last_loc);

      vlog_node_t index = vlog_new(V_LOCAL_DECL);
      vlog_set_ident(index, ident_new("index"));
      vlog_node_t inttype = make_integer_atom_type(DT_INT);
      vlog_set_flags(inttype, VLOG_F_SIGNED);
      vlog_node_t key = vlog_kind(prefix_type) == V_DATA_TYPE
         && vlog_subkind(prefix_type) == DT_ASSOC
         ? vlog_assoc_key(prefix_type) : NULL;
      vlog_set_type(index, key != NULL ? key : inttype);
      vlog_set_loc(index, &state.last_loc);

      vlog_symtab_push(symtab, v);
      vlog_symtab_put(symtab, item);

      iter_decl = item;
      iter_index = index;

      consume(tLPAREN);
      vlog_node_t expr = p_expression();
      consume(tRPAREN);

      iter_decl = saved_iter;
      iter_index = saved_index;

      vlog_symtab_pop(symtab);

      vlog_add_stmt(v, item);
      vlog_add_stmt(v, index);
      vlog_add_stmt(v, expr);
   }

   if (class != NULL) {
      vlog_symtab_pop(symtab);
      local_scope = saved_local;
   }

   vlog_set_loc(v, CURRENT_LOC);
   vlog_symtab_lookup(symtab, v);
   return v;
}

static vlog_node_t p_method_call_tail(vlog_node_t prefix)
{
   // method_call_body with the prefix already parsed

   EXTEND("method call");

   return p_method_call_rest(prefix, p_method_name());
}

static vlog_node_t p_implicit_this_call(void)
{
   // randomize ( ... ) in a class is the same as this.randomize ( ... )

   BEGIN("method call");

   ident_t id = p_identifier();

   vlog_node_t this = vlog_new(V_THIS);
   vlog_set_type(this, vlog_symtab_class(symtab));
   vlog_set_loc(this, &state.last_loc);

   return p_method_call_rest(this, id);
}

static void expand_randomize_args(vlog_node_t src, vlog_node_t v)
{
   // An array or an unpacked structure is randomized one element at a
   // time so replace the argument with the references to its elements

   const int nparams = vlog_params(src);
   vlog_node_t *args LOCAL = xmalloc_array(nparams, sizeof(vlog_node_t));
   for (int i = 0; i < nparams; i++)
      args[i] = vlog_param(src, i);

   vlog_node_t *flat = NULL;
   int nflat = 0, maxflat = 0;
   for (int i = 0; i < nparams; i++) {
      vlog_node_t p = args[i];

      vlog_node_t decl = NULL, sdecl = NULL;
      int64_t low = 0;
      int nelems = 1;
      bool expand = false, isarray = false;

      if (vlog_kind(p) == V_REF && vlog_has_ref(p)
          && vlog_kind(vlog_ref(p)) == V_VAR_DECL) {
         decl = vlog_ref(p);

         // The unpacked dimension is on the declaration or its type
         vlog_node_t type = vlog_type(decl), dims = decl;
         if (vlog_ranges(decl) == 0 && vlog_kind(type) == V_DATA_TYPE
             && vlog_ranges(type) == 1 && vlog_has_type(type)) {
            dims = type;
            type = vlog_type(type);
         }

         sdecl = vlog_get_type(type);

         const bool is_struct = sdecl != NULL
            && vlog_kind(sdecl) == V_STRUCT_DECL
            && !(vlog_flags(sdecl) & VLOG_F_PACKED);
         const bool is_array = vlog_ranges(dims) == 1
            && vlog_subkind(vlog_range(dims, 0)) == V_DIM_UNPACKED;

         expand = is_struct || is_array;
         if (is_array) {
            vlog_node_t r = vlog_range(dims, 0);
            int64_t left, right;
            if (!vlog_range_bound(vlog_left(r), &left)
                || !vlog_range_bound(vlog_right(r), &right))
               expand = false;
            else {
               low = MIN(left, right);
               nelems = (left > right ? left - right : right - left) + 1;
            }
         }
         else if (vlog_ranges(decl) > 0)
            expand = false;

         isarray = is_array;
         if (!is_struct)
            sdecl = NULL;
      }

      const int nfields = sdecl != NULL ? vlog_decls(sdecl) : 1;
      const int count = expand ? nelems * nfields : 1;

      if (nflat + count > maxflat) {
         maxflat = MAX(maxflat * 2, nflat + count + nparams);
         flat = xrealloc_array(flat, maxflat, sizeof(vlog_node_t));
      }

      if (!expand) {
         flat[nflat++] = p;
         continue;
      }

      for (int e = 0; e < nelems; e++) {
         vlog_node_t base = p;
         if (isarray) {
            base = vlog_new(V_BIT_SELECT);
            vlog_set_value(base, p);
            vlog_set_loc(base, vlog_loc(p));

            vlog_node_t index = vlog_new(V_NUMBER);
            vlog_set_number(index, number_from_int(low + e));
            vlog_set_loc(index, vlog_loc(p));
            vlog_add_param(base, index);
         }

         for (int f = 0; f < nfields; f++) {
            if (sdecl == NULL) {
               flat[nflat++] = base;
               continue;
            }

            vlog_node_t fdecl = vlog_decl(sdecl, f);
            vlog_node_t leaf = vlog_new(V_MEMBER_REF);
            vlog_set_value(leaf, base);
            vlog_set_ident(leaf, vlog_ident(fdecl));
            vlog_set_ref(leaf, fdecl);
            vlog_set_loc(leaf, vlog_loc(p));
            flat[nflat++] = leaf;
         }
      }
   }

   for (int i = 0; i < nflat; i++)
      vlog_add_param(v, flat[i]);

   free(flat);
}

static vlog_node_t p_std_randomize(void)
{
   // std :: randomize ( [ variable_identifier_list ] )
   //    [ with constraint_block ]

   BEGIN("std randomize call");

   if (is_scope_randomize())
      consume(tID);
   else {
      consume(tID);
      consume(tSCOPE);
      consume(tID);
   }

   vlog_node_t v = vlog_new(V_STD_RANDOMIZE);
   vlog_node_t args = vlog_new(V_STD_RANDOMIZE);

   consume(tLPAREN);
   p_list_of_arguments(args);
   consume(tRPAREN);

   expand_randomize_args(args, v);

   if (peek() == tWITH)
      p_inline_constraints(v, NULL);

   vlog_set_loc(v, CURRENT_LOC);
   return v;
}


typedef struct {
   vlog_node_t ref;
   vlog_node_t scope;
} pending_lookup_t;

static vlog_node_t *open_classes = NULL;
static int          nopen_classes = 0;
static pending_lookup_t *pending_lookups = NULL;
static int          npending_lookups = 0;

static bool defer_class_lookup(vlog_node_t ref, vlog_node_t scope)
{
   // A member of a class that is still being declared may come later
   // in its body, as when it is the actual of a type parameter of its
   // own base class

   if (vlog_kind(scope) != V_CLASS_DECL
       || vlog_scope_member(scope, vlog_ident(ref)) != NULL)
      return false;

   for (int i = 0; i < nopen_classes; i++) {
      if (open_classes[i] == scope) {
         pending_lookups = xrealloc_array(pending_lookups, npending_lookups + 1,
                                          sizeof(pending_lookup_t));
         pending_lookups[npending_lookups++] =
            (pending_lookup_t){ ref, scope };
         return true;
      }
   }

   return false;
}

static void resolve_pending_lookups(vlog_node_t scope)
{
   int wr = 0;
   const int n = npending_lookups;
   for (int i = 0; i < n; i++) {
      if (pending_lookups[i].scope == scope)
         vlog_symtab_lookup_in(symtab, pending_lookups[i].ref, scope);
      else
         pending_lookups[wr++] = pending_lookups[i];
   }
   npending_lookups = wr;
}
static bool foreach_at_last_index(void)
{
   // The next bracket is the last of the foreach array and so is followed
   // by the closing parenthesis

   int n = 2;
   for (int depth = 1; depth > 0 && n < TOKENQ_SIZE - 8; n++) {
      const token_t tok = peek_nth(n);
      if (tok == tLSQUARE)
         depth++;
      else if (tok == tRSQUARE)
         depth--;
      else if (tok == tEOF)
         break;
   }

   return peek_nth(n) == tRPAREN;
}

static vlog_node_t p_select(ident_t id)
{
   // [ { . member_identifier bit_select } . member_identifier ]
   //    { [ expression ] } [ [ part_select_range ] ]

   EXTEND("select");

   const bool stop_at_index = foreach_array;
   foreach_array = false;

   vlog_node_t qscope = qualify_scope;
   const bool qfailed = qualify_failed;
   qualify_scope = NULL;
   qualify_failed = false;

   vlog_scope_t *sscope = select_scope;
   select_scope = NULL;

   vlog_node_t prefix;
   if (id == NULL && peek() == tSUPER) {
      consume(tSUPER);
      prefix = vlog_new(V_THIS);
      vlog_set_flags(prefix, VLOG_F_STATIC);   // Non-virtual calls
      vlog_node_t class = vlog_symtab_class(symtab);
      if (class == NULL || !vlog_has_ref(class))
         parse_error(&state.last_loc, "super can only be used in a derived "
                     "class");
      else
         vlog_set_type(prefix, vlog_ref(class));
      vlog_set_loc(prefix, CURRENT_LOC);
   }
   else if (id == NULL) {
      consume(tTHIS);
      prefix = vlog_new(V_THIS);
      vlog_node_t class = vlog_symtab_class(symtab);
      if (class == NULL)
         parse_error(&state.last_loc, "this can only be used in a class");
      else
         vlog_set_type(prefix, class);
      vlog_set_loc(prefix, CURRENT_LOC);
   }
   else if (qscope != NULL) {
      prefix = vlog_new(V_REF);
      vlog_set_ident(prefix, id);
      vlog_set_loc(prefix, CURRENT_LOC);

      if (!defer_class_lookup(prefix, qscope))
         vlog_symtab_lookup_in(symtab, prefix, qscope);
   }
   else if (clocking_names != NULL && hset_contains(clocking_names, id)
            && vlog_symtab_query(symtab, id) == NULL) {
      prefix = vlog_new(V_REF);
      vlog_set_ident(prefix, clocking_member(id));
      vlog_set_loc(prefix, CURRENT_LOC);

      vlog_symtab_lookup(symtab, prefix);
   }
   else if (qfailed) {
      prefix = vlog_new(V_REF);
      vlog_set_ident(prefix, id);
      vlog_set_loc(prefix, CURRENT_LOC);
   }
   else {
      vlog_scope_t *inner = sscope != NULL
         ? vlog_symtab_enter(symtab, sscope) : NULL;

      vlog_node_t d = vlog_symtab_query(symtab, id);
      const bool is_inst = d != NULL && (vlog_kind(d) == V_MOD_INST
                                         || vlog_kind(d) == V_IFACE_PORT);
      if (is_inst && peek() != tDOT && !at_instance_index(true)) {
         if (inner != NULL)
            vlog_symtab_enter(symtab, inner);
         return interface_as_value(id, CURRENT_LOC);
      }
      else if (is_inst)
         prefix = p_hierarchical_identifier(id);
      else if (d == NULL && (peek() == tDOT || at_instance_index(true))) {
         // Assume this is a hierarchical reference to a later instance
         prefix = p_hierarchical_identifier(id);
      }
      else {
         prefix = vlog_new(V_REF);
         vlog_set_ident(prefix, id);
         vlog_set_loc(prefix, CURRENT_LOC);

         vlog_symtab_lookup(symtab, prefix);

         if (d != NULL && vlog_kind(d) == V_FUNC_DECL
             && (vlog_flags(d) & VLOG_F_LET)) {
            // A let declaration with no arguments
            vlog_node_t call = vlog_new(V_USER_FCALL);
            vlog_set_ident(call, id);
            vlog_set_loc(call, CURRENT_LOC);
            prefix = expand_let(d, call);
         }
      }

      if (inner != NULL)
         vlog_symtab_enter(symtab, inner);
   }

   for (;;) {
      if (vlog_kind(prefix) == V_HIER_REF && peek() == tLPAREN) {
         vlog_node_t v = vlog_new(V_METHOD_CALL);
         vlog_set_value(v, prefix);
         vlog_set_ident(v, vlog_ident(prefix));

         consume(tLPAREN);
         p_list_of_arguments(v);
         consume(tRPAREN);

         vlog_set_loc(v, CURRENT_LOC);
         prefix = v;
         vlog_set_global_flags(v, VLOG_GF_HIER_REF);
      }

      while (optional(tDOT)) {
         vlog_node_t ptype = vlog_get_type(prefix);
         if (iter_decl != NULL && vlog_kind(prefix) == V_REF
             && vlog_has_ref(prefix) && vlog_ref(prefix) == iter_decl
             && peek() == tID && peek_ident_nth(1) == ident_new("index")) {
            // The index of the current element of an array locator
            consume(tID);
            prefix = vlog_new(V_REF);
            vlog_set_ident(prefix, ident_new("index"));
            vlog_set_ref(prefix, iter_index);
            vlog_set_loc(prefix, CURRENT_LOC);
            continue;
         }

         if (peek_nth(2) == tLPAREN || is_array_method_call(prefix)
             || (ptype != NULL && vlog_kind(ptype) == V_ENUM_DECL)
             || (ptype != NULL && vlog_kind(ptype) == V_DATA_TYPE
                 && vlog_subkind(ptype) == DT_STRING)
             || (peek() == tID && peek_ident_nth(1) == ident_new("randomize")
                 && ptype != NULL && vlog_kind(ptype) == V_CLASS_DECL)
             || (ptype != NULL && vlog_kind(ptype) == V_DATA_TYPE
                 && (vlog_subkind(ptype) == DT_DYNARRAY
                     || vlog_subkind(ptype) == DT_QUEUE
                     || vlog_subkind(ptype) == DT_ASSOC
                     || vlog_subkind(ptype) == DT_PROCESS
                     || vlog_subkind(ptype) == DT_EVENT
                     || vlog_subkind(ptype) == DT_SEMAPHORE
                     || vlog_subkind(ptype) == DT_MAILBOX))) {
            // Parentheses are optional when there are no arguments
            prefix = p_method_call_tail(prefix);
            continue;
         }

         if (ptype != NULL && vlog_kind(ptype) == V_DATA_TYPE
             && vlog_subkind(ptype) == DT_VIRTUAL && vlog_has_ref(ptype)
             && peek() == tID && peek_nth(2) == tDOT) {
            // A modport only restricts the names of the interface
            vlog_node_t iface = vlog_ref(ptype);
            ident_t name = peek_ident_nth(1);
            const int ndecls = vlog_decls(iface);
            bool is_modport = false;
            for (int i = 0; i < ndecls; i++) {
               vlog_node_t d = vlog_decl(iface, i);
               is_modport |= vlog_kind(d) == V_MODPORT && vlog_ident(d) == name;
            }

            if (is_modport) {
               consume(tID);
               continue;
            }
         }

         if (vlog_kind(prefix) == V_REF && vlog_has_ref(prefix)
             && peek() == tID) {
            // A static variable of a function or task is visible by its name
            vlog_node_t sub = vlog_ref(prefix);
            if (vlog_kind(sub) == V_FUNC_DECL
                || vlog_kind(sub) == V_TASK_DECL) {
               ident_t id = peek_ident_nth(1);
               ident_t prefix_id = ident_sprintf("%s$static", istr(id));
               vlog_node_t found = NULL;
               const int ndecls = vlog_decls(sub);
               for (int i = 0; i < ndecls; i++) {
                  vlog_node_t d = vlog_decl(sub, i);
                  if (vlog_kind(d) == V_VAR_DECL && (vlog_flags(d) & VLOG_F_STATIC)
                      && ident_starts_with(vlog_ident(d), prefix_id))
                     found = d;
               }

               if (found != NULL) {
                  consume(tID);
                  vlog_node_t sref = vlog_new(V_REF);
                  vlog_set_ident(sref, id);
                  vlog_set_ref(sref, found);
                  vlog_set_loc(sref, CURRENT_LOC);
                  prefix = sref;
                  continue;
               }
            }
         }

         vlog_node_t ref = vlog_new(V_MEMBER_REF);
         ident_t member = p_identifier();
         if (is_clocking_name(member, ptype))
            member = clocking_member(member);
         else if (ptype != NULL && vlog_kind(ptype) == V_DATA_TYPE
                  && vlog_subkind(ptype) == DT_VIRTUAL
                  && !vlog_has_ref(ptype) && peek() == tDOT
                  && peek_nth(2) == tID && peek_nth(3) != tLPAREN) {
            // The interface is declared later so this may be a clockvar
            // or a member through a modport: vlog_virtual_decl decides
            consume(tDOT);
            member = ident_sprintf("%s$%s", istr(member),
                                   istr(p_identifier()));
         }
         vlog_set_ident(ref, member);
         vlog_set_value(ref, prefix);
         vlog_set_loc(ref, CURRENT_LOC);

         vlog_symtab_lookup(symtab, ref);

         prefix = ref;
      }

      if (peek() != tLSQUARE)
         return prefix;
      else if (stop_at_index) {
         // The last index gives the names of the loop variables
         int n = 2;
         for (int depth = 1; depth > 0 && n < TOKENQ_SIZE - 8; n++) {
            const token_t tok = peek_nth(n);
            if (tok == tLSQUARE)
               depth++;
            else if (tok == tRSQUARE)
               depth--;
            else if (tok == tEOF)
               break;
         }

         if (peek_nth(n) == tRPAREN)
            return prefix;
      }

      switch (peek_nth(2)) {
      case tTIMES:
      case tPLUS:
         return prefix;  // Repetition in property
      default:
         break;
      }

      consume(tLSQUARE);

      do {
         vlog_node_t expr = p_expression();
         if (scan(tCOLON, tINDEXPOS, tINDEXNEG)) {
            vlog_node_t ps = vlog_new(V_PART_SELECT);
            vlog_set_left(ps, expr);
            vlog_set_value(ps, prefix);

            p_part_select_range(ps);

            consume(tRSQUARE);

            vlog_set_loc(ps, CURRENT_LOC);
            return ps;
         }

         if (vlog_kind(prefix) == V_BIT_SELECT)
            vlog_add_param(prefix, expr);
         else {
            vlog_node_t bs = vlog_new(V_BIT_SELECT);
            vlog_set_loc(bs, CURRENT_LOC);
            vlog_set_value(bs, prefix);
            vlog_add_param(bs, expr);

            prefix = bs;
         }

         consume(tRSQUARE);

         if (stop_at_index && peek() == tLSQUARE && foreach_at_last_index())
            return prefix;   // The loop variables follow an element
      } while (optional(tLSQUARE));

      if (peek() != tDOT)
         return prefix;
   }
}

// The subroutine called by the user call whose arguments are being parsed
static vlog_node_t callee_decl = NULL;

static vlog_node_t arg_pattern_type(vlog_node_t call, int n)
{
   // The type an assignment pattern takes as the Nth argument of CALL

   vlog_node_t decl = NULL;
   switch (vlog_kind(call)) {
   case V_METHOD_CALL:
      {
         vlog_node_t type = vlog_get_type(vlog_value(call));
         if (type == NULL || !vlog_has_ident(call))
            return NULL;

         ident_t id = vlog_ident(call);
         if (vlog_kind(type) == V_CLASS_DECL)
            decl = vlog_class_member(type, id);
         else if (vlog_kind(type) == V_DATA_TYPE
                  && (vlog_subkind(type) == DT_QUEUE
                      || vlog_subkind(type) == DT_DYNARRAY)) {
            if ((id == ident_new("push_back") || id == ident_new("push_front"))
                && n == 0)
               return vlog_get_type(vlog_type(type));
            else if (id == ident_new("insert") && n == 1)
               return vlog_get_type(vlog_type(type));
            return NULL;
         }
      }
      break;
   case V_USER_FCALL:
   case V_USER_TCALL:
      decl = callee_decl;
      break;
   default:
      break;
   }

   if (decl == NULL)
      return NULL;

   switch (vlog_kind(decl)) {
   case V_FUNC_DECL:
   case V_TASK_DECL:
      if (n < vlog_ports(decl)) {
         vlog_node_t port = vlog_port(decl, n);
         if (vlog_kind(port) == V_TF_PORT_DECL && vlog_has_type(port))
            return vlog_get_type(vlog_type(port));
      }
      return NULL;
   default:
      return NULL;
   }
}

static bool is_assert_control(vlog_node_t call)
{
   // Tasks which enable or disable assertions of a scope or a label

   if (vlog_kind(call) != V_SYS_TCALL)
      return false;

   static const char *const names[] = {
      "$asserton", "$assertoff", "$assertkill", "$assertcontrol",
      "$assertpasson", "$assertpassoff", "$assertfailon", "$assertfailoff",
      "$assertnonvacuouson", "$assertvacuousoff",
   };

   for (int i = 0; i < ARRAY_LEN(names); i++) {
      if (vlog_ident(call) == ident_new(names[i]))
         return true;
   }

   return false;
}

static int assert_control_list(vlog_node_t call)
{
   // Position of the first scope or assertion of an assertion control task

   return vlog_ident(call) == ident_new("$assertcontrol") ? 4 : 1;
}

static bool at_scope_argument(vlog_node_t call)
{
   // The arguments of $dumpvars after the first name scopes, other than
   // the enclosing module which is resolved by the symbol table

   if (vlog_kind(call) != V_SYS_TCALL
       || vlog_ident(call) != ident_new("$dumpvars"))
      return false;

   ident_t id = peek_ident_nth(1);
   vlog_node_t unit = vlog_symtab_design_unit(symtab);
   if (unit != NULL && vlog_kind(unit) == V_MODULE && vlog_ident2(unit) == id)
      return false;

   return vlog_symtab_query(symtab, id) == NULL;
}

static vlog_node_t p_clocking_event(void);

static bool is_timescale_query(vlog_node_t call)
{
   switch (vlog_kind(call)) {
   case V_SYS_TCALL:
      return vlog_ident(call) == ident_new("$printtimescale");
   case V_SYS_FCALL:
      return vlog_ident(call) == ident_new("$timeunit")
         || vlog_ident(call) == ident_new("$timeprecision");
   default:
      return false;
   }
}

static vlog_node_t p_timescale_query_argument(void)
{
   // The argument of $printtimescale, $timeunit and $timeprecision is the
   // hierarchical name of a module instance (20.4.1, 20.4.2) which may be
   // declared later, so it is kept as a string and resolved at run time

   BEGIN("hierarchical identifier");

   LOCAL_TEXT_BUF tb = tb_new();

   if (peek() == tSYSTASK && peek_nth(2) == tDOT) {
      ident_t root = p_system_tf_identifier();
      if (root != ident_new("$root"))
         parse_error(&state.last_loc, "unexpected system name %pi before '.'",
                     root);

      consume(tDOT);
      tb_cat(tb, "$root.");
   }

   for (;;) {
      tb_istr(tb, p_identifier());
      while (optional(tLSQUARE)) {
         tb_printf(tb, "[%"PRIu64"]", p_unsigned_integer());
         consume(tRSQUARE);
      }

      if (!optional(tDOT))
         break;

      tb_append(tb, '.');
   }

   vlog_node_t v = vlog_new(V_STRING);
   vlog_set_number(v, number_from_string(tb_get(tb), tb_len(tb)));
   vlog_set_loc(v, CURRENT_LOC);
   return v;
}

static bool at_sampled_value_function(vlog_node_t call)
{
   static const char *const names[] = {
      "$past", "$rose", "$fell", "$stable", "$changed"
   };

   for (int i = 0; i < ARRAY_LEN(names); i++) {
      if (vlog_ident(call) == ident_new(names[i]))
         return true;
   }

   return false;
}

static void p_list_of_arguments(vlog_node_t call)
{
   // [ expression ] { , [ expression ] } { , . identifier ( [ expression ] ) }
   //    | . identifier ( [ expression ] ) { , . identifier ( [ expression ] ) }

   BEGIN("list of arguments");

   if (peek() == tRPAREN)
      return;

   vlog_node_t saved_arm = arm_type;
   arm_type = NULL;

   vlog_node_t pattern_type;
   do {
      if (peek() == tCOMMA || peek() == tRPAREN) {
         vlog_node_t v = vlog_new(V_EMPTY);
         vlog_set_loc(v, &state.last_loc);
         vlog_add_param(call, v);
      }
      else if (peek() == tDOT) {
         consume(tDOT);

         vlog_node_t v = vlog_new(V_NAMED_ARG);
         vlog_set_ident(v, p_identifier());

         consume(tLPAREN);
         if (peek() != tRPAREN)
            vlog_set_value(v, p_expression());
         consume(tRPAREN);

         vlog_set_loc(v, CURRENT_LOC);
         vlog_add_param(call, v);
      }
      else if (peek() == tAT && vlog_kind(call) == V_SYS_FCALL
               && at_sampled_value_function(call)) {
         // Clocking event of a sampled value function
         vlog_add_param(call, p_clocking_event());
      }
      else if (peek() == tTICK && peek_nth(2) == tLBRACE
               && (pattern_type = arg_pattern_type(call, vlog_params(call)))) {
         // The type of the pattern is that of the formal or element
         vlog_node_t v = p_assignment_pattern_expression(pattern_type);
         vlog_add_param(call, v);
      }
      else if (is_assert_control(call)
               && vlog_params(call) >= assert_control_list(call)
               && peek() != tSTRING) {   // A string is the path as it is
         // The assertion or scope to control is a hierarchical name which
         // is not evaluated: it is kept as a string, and a name which is
         // not a simple path (names with constant selects of generate
         // blocks are simple) as a string which matches nothing
         LOCAL_TEXT_BUF path = tb_new();
         bool simple = true, expect_id = true;
         int depth = 0;
         const loc_t loc = state.tokenq[state.tokenq_tail].loc;
         for (;;) {
            const token_t tok = peek();
            if (tok == tEOF || (depth == 0 && (tok == tCOMMA || tok == tRPAREN)))
               break;
            else if (tok == tID && expect_id) {
               tb_cat(path, istr(state.tokenq[state.tokenq_tail].lval.ident));
               expect_id = false;
            }
            else if (tok == tDOT && !expect_id) {
               tb_append(path, '.');
               expect_id = true;
            }
            else if (tok == tLSQUARE && !expect_id && depth == 0
                     && peek_nth(2) == tUNSNUM && peek_nth(3) == tRSQUARE) {
               // A selected generate block
               consume(tLSQUARE);
               tb_printf(path, "[%"PRIu64"]", p_unsigned_integer());
               consume(tRSQUARE);
               continue;
            }
            else
               simple = false;

            if (tok == tLPAREN || tok == tLSQUARE || tok == tLBRACE)
               depth++;
            else if (tok == tRPAREN || tok == tRSQUARE || tok == tRBRACE)
               depth--;

            drop_token(&state);
         }

         ident_t last = ident_new(tb_get(path));
         if (!simple || tb_len(path) == 0 || expect_id)
            last = ident_new(" ");

         vlog_node_t str = vlog_new(V_STRING);
         vlog_set_number(str, number_from_string(istr(last),
                                                 strlen(istr(last))));
         vlog_set_loc(str, &loc);
         vlog_add_param(call, str);
      }
      else if (vlog_params(call) == 0 && is_timescale_query(call))
         vlog_add_param(call, p_timescale_query_argument());
      else if (peek() == tID && (peek_nth(2) == tCOMMA || peek_nth(2) == tRPAREN)
               && vlog_params(call) > 0 && at_scope_argument(call)) {
         // The name of a module which may be declared later
         ident_t id = p_identifier();
         vlog_add_param(call, interface_as_value(id, &state.last_loc));
      }
      else
         vlog_add_param(call, p_expression());
   } while (optional(tCOMMA));

   arm_type = saved_arm;
}

static vlog_node_t foreach_number(int64_t value, const loc_t *loc);
static bool is_simple_type_token(token_t tok);

static bool type_has_const_size(vlog_node_t t)
{
   // False if a dimension depends on a parameter which is not yet folded

   switch (vlog_kind(t)) {
   case V_DATA_TYPE:
      {
         const int nranges = vlog_ranges(t);
         for (int i = 0; i < nranges; i++) {
            vlog_node_t r = vlog_range(t, i);
            int64_t left, right;
            if (vlog_subkind(r) == V_DIM_UNSIZED || !vlog_bounds(r, &left, &right))
               return false;
         }

         return !vlog_has_type(t) || type_has_const_size(vlog_type(t));
      }
   case V_TYPE_DECL:
   case V_ENUM_DECL:
      return !vlog_has_type(t) || type_has_const_size(vlog_type(t));
   case V_STRUCT_DECL:
   case V_UNION_DECL:
      {
         const int ndecls = vlog_decls(t);
         for (int i = 0; i < ndecls; i++) {
            vlog_node_t m = vlog_decl(t, i);
            const int nranges = vlog_ranges(m);
            for (int j = 0; j < nranges; j++) {
               int64_t left, right;
               if (!vlog_bounds(vlog_range(m, j), &left, &right))
                  return false;
            }

            if (vlog_has_type(m) && !type_has_const_size(vlog_type(m)))
               return false;
         }

         return true;
      }
   default:
      return true;
   }
}

static vlog_node_t p_system_tf_call(vlog_kind_t kind)
{
   // system_tf_identifier [ ( list_of_arguments ) ]

   BEGIN("system task or function call");

   ident_t id = p_system_tf_identifier();

   vlog_systf_t subk;
   switch (is_well_known(id)) {
   case W_DLR_SIGNED:   subk = V_SYSTF_SIGNED; break;
   case W_DLR_UNSIGNED: subk = V_SYSTF_UNSIGNED; break;
   case W_DLR_CLOG2:    subk = V_SYSTF_CLOG2; break;
   case W_DLR_SQRT:     subk = V_SYSTF_SQRT; break;
   case W_DLR_BITS:     subk = V_SYSTF_BITS; break;
   case W_DLR_RTOI:     subk = V_SYSTF_RTOI; break;
   case W_DLR_CEIL:     subk = V_SYSTF_CEIL; break;
   case W_DLR_FLOOR:    subk = V_SYSTF_FLOOR; break;
   default:
      {
         // The real valued mathematical functions can be constant
         static const char *const math[] = {
            "$ln", "$log10", "$exp", "$pow", "$sin", "$cos", "$tan", "$asin",
            "$acos", "$atan", "$atan2", "$hypot", "$sinh", "$cosh", "$tanh",
            "$asinh", "$acosh", "$atanh"
         };

         subk = V_SYSTF_NONE;
         for (int i = 0; i < ARRAY_LEN(math); i++) {
            if (id == ident_new(math[i]))
               subk = V_SYSTF_MATH;
         }
      }
      break;
   }

   vlog_node_t v = vlog_new(kind);
   vlog_set_ident(v, id);
   vlog_set_subkind(v, subk);
   vlog_set_ival(v, time_ival());
   if (in_subroutine)
      vlog_set_flags(v, VLOG_F_IN_SUBR);

   if (optional(tLPAREN)) {
      if (subk == V_SYSTF_BITS
          && ((scan(tBIT, tLOGIC, tREG, tBYTE, tSHORTINT, tSVINT, tLONGINT,
                    tINTEGER, tTIME) && peek_nth(2) != tTICK)
              || (peek() == tID && peek_nth(2) == tRPAREN
                  && vlog_symtab_query(symtab, peek_ident_nth(1)) != NULL
                  && vlog_kind(vlog_symtab_query(
                                  symtab, peek_ident_nth(1))) == V_TYPE_DECL
                  && !(vlog_flags(vlog_symtab_query(
                                     symtab, peek_ident_nth(1)))
                       & VLOG_F_TYPEPARAM)
                  && type_has_const_size(vlog_symtab_query(
                                            symtab, peek_ident_nth(1)))))) {
         // A built-in type
         vlog_node_t dt = p_data_type();
         vlog_add_param(v, foreach_number(vlog_size(dt), &state.last_loc));
         vlog_node_t n = vlog_param(v, 0);
         consume(tRPAREN);
         return n;
      }

      int hlen;
      if (subk == V_SYSTF_BITS && is_hier_type_name(&hlen)
          && peek_nth(hlen + 1) == tRPAREN
          && hier_type_decl(hlen) != NULL) {
         // A type declared in an interface
         vlog_node_t orig = hier_type_decl(hlen);
         vlog_node_t dt = p_hier_type();
         if (dt != orig && current_module != NULL)
            vlog_add_decl(current_module, dt);   // Bound with the instance

         vlog_node_t ref = vlog_new(V_REF);
         vlog_set_ident(ref, vlog_ident(dt));
         vlog_set_ref(ref, dt);
         vlog_set_loc(ref, &state.last_loc);
         vlog_add_param(v, ref);

         consume(tRPAREN);
         vlog_set_loc(v, CURRENT_LOC);
         return v;
      }

      if (id == ident_new("$typename")
          && ((is_simple_type_token(peek()) && peek_nth(2) != tTICK)
              || (peek() == tID && vlog_symtab_query(symtab, peek_ident_nth(1))
                  && vlog_kind(vlog_symtab_query(symtab, peek_ident_nth(1)))
                  == V_TYPE_DECL))) {
         // A data type is represented by a variable declared with it
         vlog_node_t decl = vlog_new(V_VAR_DECL);
         vlog_set_ident(decl, ident_uniq("typename"));
         vlog_set_type(decl, p_data_type());
         vlog_set_loc(decl, &state.last_loc);

         vlog_node_t ref = vlog_new(V_REF);
         vlog_set_ident(ref, vlog_ident(decl));
         vlog_set_ref(ref, decl);
         vlog_set_loc(ref, &state.last_loc);
         vlog_add_param(v, ref);

         consume(tRPAREN);
         vlog_set_loc(v, CURRENT_LOC);
         return v;
      }

      // As an extension, arguments of system tasks and functions may refer
      // to names declared later, with a warning
      const bool saved_forward = vlog_symtab_set_forward(symtab, true);
      systf_depth++;
      p_list_of_arguments(v);
      systf_depth--;
      vlog_symtab_set_forward(symtab, saved_forward);
      consume(tRPAREN);
   }

   vlog_set_loc(v, CURRENT_LOC);

   const char *swrite_radix = NULL;
   if (id == ident_new("$swriteb"))
      swrite_radix = "$__swriteb";
   else if (id == ident_new("$swriteo"))
      swrite_radix = "$__swriteo";
   else if (id == ident_new("$swriteh"))
      swrite_radix = "$__swriteh";

   if (kind == V_SYS_TCALL
       && (id == ident_new("$sformat") || id == ident_new("$swrite")
           || swrite_radix != NULL)
       && vlog_params(v) >= 2) {
      // Same as assigning the result of $sformatf
      vlog_node_t call = vlog_new(V_SYS_FCALL);
      vlog_set_ident(call, ident_new(swrite_radix ?: "$sformatf"));
      vlog_set_subkind(call, V_SYSTF_NONE);
      vlog_set_ival(call, vlog_ival(v));
      vlog_set_loc(call, CURRENT_LOC);

      const int nparams = vlog_params(v);
      for (int i = 1; i < nparams; i++)
         vlog_add_param(call, vlog_param(v, i));

      vlog_node_t assign = vlog_new(V_BASSIGN);
      vlog_set_target(assign, vlog_param(v, 0));
      vlog_set_value(assign, call);
      vlog_set_loc(assign, CURRENT_LOC);
      return assign;
   }

   if (kind == V_SYS_TCALL)
      return rewrite_monitor(v);

   return v;
}

static void check_monitor_arg_cb(vlog_node_t v, void *ctx)
{
   if (vlog_kind(v) == V_REF && vlog_has_ref(v)) {
      switch (vlog_kind(vlog_ref(v))) {
      case V_LOCAL_DECL:
      case V_TF_PORT_DECL:
         *(bool *)ctx = true;
         break;
      default:
         break;
      }
   }
}

static bool monitor_arg_sensitive(vlog_node_t v)
{
   switch (vlog_kind(v)) {
   case V_REF:
   case V_HIER_REF:
   case V_BIT_SELECT:
   case V_PART_SELECT:
   case V_BINARY:
   case V_UNARY:
   case V_COND_EXPR:
   case V_CONCAT:
      return true;
   default:
      return false;
   }
}

static vlog_node_t monitor_print_call(vlog_node_t call, const char *name,
                                      unsigned n, char radix)
{
   vlog_node_t print = vlog_new(V_SYS_TCALL);
   vlog_set_ident(print, ident_new(name));
   vlog_set_subkind(print, V_SYSTF_NONE);
   vlog_set_ival(print, vlog_ival(call));
   vlog_set_loc(print, vlog_loc(call));

   vlog_node_t num = vlog_new(V_NUMBER);
   vlog_set_number(num, number_from_int(n));
   vlog_set_loc(num, vlog_loc(call));
   vlog_add_param(print, num);

   vlog_node_t rad = vlog_new(V_NUMBER);
   vlog_set_number(rad, number_from_int(radix));
   vlog_set_loc(rad, vlog_loc(call));
   vlog_add_param(print, rad);

   const int nparams = vlog_params(call);
   for (int i = 0; i < nparams; i++)
      vlog_add_param(print, vlog_param(call, i));

   return print;
}

static vlog_node_t rewrite_monitor(vlog_node_t call)
{
   // $monitor and $strobe print at the end of the time step so they are
   // implemented by a postponed process which is sensitive to the
   // arguments and to a variable which the call itself changes

   ident_t id = vlog_ident(call);

   char radix = 'd';
   bool strobe = false, fmon = false, fstrobe = false;
   if (id == ident_new("$fmonitor"))
      fmon = true;
   else if (id == ident_new("$fstrobe"))
      strobe = fstrobe = true;
   else if (id == ident_new("$fstrobeb")) {
      strobe = fstrobe = true;
      radix = 'b';
   }
   else if (id == ident_new("$fstrobeo")) {
      strobe = fstrobe = true;
      radix = 'o';
   }
   else if (id == ident_new("$fstrobeh")) {
      strobe = fstrobe = true;
      radix = 'h';
   }
   else if (id == ident_new("$fmonitorb")) {
      fmon = true;
      radix = 'b';
   }
   else if (id == ident_new("$fmonitoro")) {
      fmon = true;
      radix = 'o';
   }
   else if (id == ident_new("$fmonitorh")) {
      fmon = true;
      radix = 'h';
   }
   else if (id == ident_new("$monitor") || id == ident_new("$strobe"))
      strobe = id == ident_new("$strobe");
   else if (id == ident_new("$monitorb") || id == ident_new("$strobeb")) {
      radix = 'b';
      strobe = id == ident_new("$strobeb");
   }
   else if (id == ident_new("$monitoro") || id == ident_new("$strobeo")) {
      radix = 'o';
      strobe = id == ident_new("$strobeo");
   }
   else if (id == ident_new("$monitorh") || id == ident_new("$strobeh")) {
      radix = 'h';
      strobe = id == ident_new("$strobeh");
   }
   else
      return call;

   if (current_module == NULL) {
      error_at(vlog_loc(call), "sorry, %pQ is only supported in a module",
               id);
      return call;
   }

   const unsigned n = ++monitor_count;

   // A variable of the subroutine does not exist in the postponed process
   // so its value is copied when the call is made
   vlog_node_t capture = NULL;
   const int nparams = vlog_params(call);
   for (int i = 0; i < nparams; i++) {
      vlog_node_t p = vlog_param(call, i);

      bool local = false;
      vlog_visit(p, check_monitor_arg_cb, &local);
      if (!local)
         continue;

      if (vlog_kind(p) != V_REF) {
         error_at(vlog_loc(call), "sorry, an expression of the variables "
                  "of a subroutine in the arguments of %pQ is not supported",
                  id);
         return call;
      }

      vlog_node_t decl = vlog_ref(p);

      vlog_node_t hidden = vlog_new(V_VAR_DECL);
      vlog_set_ident(hidden, ident_sprintf("$marg%u_%d", n, i));
      vlog_set_type(hidden, vlog_type(decl));
      vlog_set_loc(hidden, vlog_loc(p));
      vlog_add_decl(current_module, hidden);

      vlog_node_t target = vlog_new(V_REF);
      vlog_set_ident(target, vlog_ident(hidden));
      vlog_set_ref(target, hidden);
      vlog_set_loc(target, vlog_loc(p));

      vlog_node_t assign = vlog_new(V_BASSIGN);
      vlog_set_target(assign, target);
      vlog_set_value(assign, p);
      vlog_set_loc(assign, vlog_loc(p));

      if (capture == NULL) {
         capture = vlog_new(V_SEQ_BLOCK);
         vlog_set_loc(capture, vlog_loc(call));
      }

      vlog_add_stmt(capture, assign);

      vlog_node_t use = vlog_new(V_REF);
      vlog_set_ident(use, vlog_ident(hidden));
      vlog_set_ref(use, hidden);
      vlog_set_loc(use, vlog_loc(p));
      vlog_set_param(call, i, use);
   }

   vlog_node_t kick = vlog_new(V_VAR_DECL);
   vlog_set_ident(kick, ident_sprintf("$kick%u", n));
   vlog_set_type(kick, logic_type());
   vlog_set_loc(kick, vlog_loc(call));
   vlog_add_decl(current_module, kick);

   vlog_node_t ref = vlog_new(V_REF);
   vlog_set_ident(ref, vlog_ident(kick));
   vlog_set_ref(ref, kick);
   vlog_set_loc(ref, vlog_loc(call));

   // All lines of $monitor and $strobe are printed in the Postponed
   // region in the order in which their processes were woken
   vlog_node_t print = monitor_print_call(
      call, fstrobe ? "$__fstrobe" : strobe ? "$__strobe"
      : fmon ? "$__fmonitor_kick" : "$__monitor_kick", n, radix);

   vlog_node_t ctrl = vlog_new(V_EVENT_CONTROL);
   vlog_set_loc(ctrl, vlog_loc(call));

   vlog_node_t kev = vlog_new(V_EVENT);
   vlog_set_subkind(kev, V_EVENT_LEVEL);
   vlog_set_value(kev, ref);
   vlog_set_loc(kev, vlog_loc(call));
   vlog_add_param(ctrl, kev);

   vlog_node_t start = vlog_new(V_SYS_TCALL);
   vlog_set_ident(start, ident_new(strobe ? "$__strobe_start" : fmon
                                   ? "$__fmonitor_start" : "$__monitor_start"));
   vlog_set_subkind(start, V_SYSTF_NONE);
   vlog_set_ival(start, vlog_ival(call));
   vlog_set_loc(start, vlog_loc(call));

   vlog_node_t snum = vlog_new(V_NUMBER);
   vlog_set_number(snum, number_from_int(n));
   vlog_set_loc(snum, vlog_loc(call));
   vlog_add_param(start, snum);

   vlog_node_t sref = vlog_new(V_REF);
   vlog_set_ident(sref, vlog_ident(kick));
   vlog_set_ref(sref, kick);
   vlog_set_loc(sref, vlog_loc(call));
   vlog_add_param(start, sref);

   for (int i = 0; i < nparams; i++) {
      vlog_node_t p = vlog_param(call, i);

      if (!strobe && !(fmon && i == 0) && monitor_arg_sensitive(p)) {
         vlog_node_t ev = vlog_new(V_EVENT);
         vlog_set_subkind(ev, V_EVENT_LEVEL);
         vlog_set_value(ev, p);
         vlog_set_loc(ev, vlog_loc(p));

         vlog_node_t cctrl = vlog_new(V_EVENT_CONTROL);
         vlog_set_loc(cctrl, vlog_loc(call));
         vlog_add_param(cctrl, ev);

         vlog_node_t ctiming = vlog_new(V_TIMING);
         vlog_set_value(ctiming, cctrl);
         vlog_set_loc(ctiming, vlog_loc(call));
         vlog_add_stmt(ctiming, monitor_print_call(
                          call, fmon ? "$__fmonitor" : "$__monitor", n, radix));

         // A process for each argument runs in the order the arguments
         // changed
         vlog_node_t calways = vlog_new(V_ALWAYS);
         vlog_set_subkind(calways, V_ALWAYS_PLAIN);
         vlog_set_ident(calways, ident_sprintf("$postponed_s%u_%d", n, i));
         vlog_set_loc(calways, vlog_loc(call));
         vlog_add_stmt(calways, ctiming);
         vlog_add_stmt(current_module, calways);
      }
   }

   vlog_node_t timing = vlog_new(V_TIMING);
   vlog_set_value(timing, ctrl);
   vlog_set_loc(timing, vlog_loc(call));
   vlog_add_stmt(timing, print);

   vlog_node_t always = vlog_new(V_ALWAYS);
   vlog_set_subkind(always, V_ALWAYS_PLAIN);
   vlog_set_ident(always, ident_sprintf(strobe ? "$postponed_s%u"
                                        : "$postponed_s%uk", n));
   vlog_set_loc(always, vlog_loc(call));
   vlog_add_stmt(always, timing);
   vlog_add_stmt(current_module, always);

   if (capture != NULL) {
      vlog_add_stmt(capture, start);
      return capture;
   }

   return start;
}

static vlog_node_t deferred_inner = NULL;
static ident_t deferred_label = NULL;   // Label of the statement

typedef struct {
   unsigned         serial;
   vlog_node_t      scope;   // Where the copies are declared
   vlog_kind_t      kind;
   A(vlog_node_t) decls, copies;
} defer_capture_t;

static vlog_node_t defer_capture_expr(defer_capture_t *dc, vlog_node_t v)
{
   switch (vlog_kind(v)) {
   case V_BINARY:
      vlog_set_left(v, defer_capture_expr(dc, vlog_left(v)));
      vlog_set_right(v, defer_capture_expr(dc, vlog_right(v)));
      return v;
   case V_UNARY:
   case V_MEMBER_REF:
      vlog_set_value(v, defer_capture_expr(dc, vlog_value(v)));
      return v;
   case V_COND_EXPR:
      vlog_set_value(v, defer_capture_expr(dc, vlog_value(v)));
      vlog_set_left(v, defer_capture_expr(dc, vlog_left(v)));
      vlog_set_right(v, defer_capture_expr(dc, vlog_right(v)));
      return v;
   case V_BIT_SELECT:
      {
         vlog_set_value(v, defer_capture_expr(dc, vlog_value(v)));
         const int nparams = vlog_params(v);
         for (int i = 0; i < nparams; i++)
            vlog_set_param(v, i, defer_capture_expr(dc, vlog_param(v, i)));
      }
      return v;
   case V_CONCAT:
   case V_SYS_FCALL:
      {
         const int nparams = vlog_params(v);
         for (int i = 0; i < nparams; i++)
            vlog_set_param(v, i, defer_capture_expr(dc, vlog_param(v, i)));
      }
      return v;
   case V_REF:
      break;
   default:
      return v;
   }

   if (!vlog_has_ref(v))
      return v;

   vlog_node_t d = vlog_ref(v);
   switch (vlog_kind(d)) {
   case V_VAR_DECL:
   case V_NET_DECL:
   case V_PORT_DECL:
   case V_LOCAL_DECL:
   case V_TF_PORT_DECL:
      break;
   default:
      return v;
   }

   if (!vlog_has_type(d) || vlog_is_fixed_unpacked(d))
      return v;

   vlog_node_t hidden = NULL;
   for (unsigned i = 0; i < dc->decls.count; i++) {
      if (dc->decls.items[i] == d) {
         hidden = dc->copies.items[i];
         break;
      }
   }

   if (hidden == NULL) {
      hidden = vlog_new(dc->kind);
      vlog_set_ident(hidden, ident_sprintf("$darg%u_%u", dc->serial,
                                           dc->decls.count));
      vlog_set_type(hidden, vlog_type(d));
      vlog_set_loc(hidden, vlog_loc(v));
      vlog_add_decl(dc->scope, hidden);

      APUSH(dc->decls, d);
      APUSH(dc->copies, hidden);
   }

   vlog_node_t use = vlog_new(V_REF);
   vlog_set_ident(use, vlog_ident(hidden));
   vlog_set_ref(use, hidden);
   vlog_set_loc(use, vlog_loc(v));
   return use;
}

static void defer_capture_call(defer_capture_t *dc, vlog_node_t call)
{
   // Only arguments passed by value to an input are copied

   if (!vlog_has_ref(call))
      return;

   vlog_node_t decl = vlog_ref(call);
   if (vlog_kind(decl) != V_TASK_DECL && vlog_kind(decl) != V_FUNC_DECL)
      return;

   const int nports = vlog_ports(decl);
   const int nparams = vlog_params(call);
   for (int i = 0; i < nparams; i++) {
      vlog_node_t p = vlog_param(call, i), port = NULL;
      if (vlog_kind(p) == V_NAMED_ARG) {
         for (int j = 0; j < nports; j++) {
            if (vlog_ident(vlog_port(decl, j)) == vlog_ident(p)) {
               port = vlog_port(decl, j);
               break;
            }
         }

         if (port != NULL && vlog_subkind(port) == V_PORT_INPUT
             && vlog_has_value(p))
            vlog_set_value(p, defer_capture_expr(dc, vlog_value(p)));
      }
      else if (i < nports) {
         port = vlog_port(decl, i);
         if (vlog_subkind(port) == V_PORT_INPUT)
            vlog_set_param(call, i, defer_capture_expr(dc, p));
      }
   }
}

static void defer_capture_stmt(defer_capture_t *dc, vlog_node_t s)
{
   switch (vlog_kind(s)) {
   case V_SYS_TCALL:
      {
         const int nparams = vlog_params(s);
         for (int i = 0; i < nparams; i++)
            vlog_set_param(s, i, defer_capture_expr(dc, vlog_param(s, i)));
      }
      break;
   case V_USER_TCALL:
      defer_capture_call(dc, s);
      break;
   case V_VOID_CALL:
      if (vlog_kind(vlog_value(s)) == V_USER_FCALL)
         defer_capture_call(dc, vlog_value(s));
      break;
   case V_SEQ_BLOCK:
   case V_COND:
      {
         const int nstmts = vlog_stmts(s);
         for (int i = 0; i < nstmts; i++)
            defer_capture_stmt(dc, vlog_stmt(s, i));
      }
      break;
   case V_IF:
      {
         const int nconds = vlog_conds(s);
         for (int i = 0; i < nconds; i++)
            defer_capture_stmt(dc, vlog_cond(s, i));
      }
      break;
   default:
      break;
   }
}

static vlog_node_t defer_ref(vlog_node_t decl, const loc_t *loc)
{
   vlog_node_t ref = vlog_new(V_REF);
   vlog_set_ident(ref, vlog_ident(decl));
   vlog_set_ref(ref, decl);
   vlog_set_loc(ref, loc);
   return ref;
}

static vlog_node_t defer_queue_call(vlog_node_t queue, const char *name,
                                    vlog_method_t method, const loc_t *loc)
{
   vlog_node_t call = vlog_new(V_METHOD_CALL);
   vlog_set_value(call, defer_ref(queue, loc));
   vlog_set_ident(call, ident_new(name));
   vlog_set_subkind(call, method);
   vlog_set_loc(call, loc);
   return call;
}

static vlog_node_t defer_queue_decl(vlog_node_t hidden, const loc_t *loc)
{
   // Each evaluation of a deferred assertion queues its own report with
   // the values it captured (16.4.2)

   vlog_node_t type = vlog_new(V_DATA_TYPE);
   vlog_set_subkind(type, DT_QUEUE);
   vlog_set_type(type, vlog_type(hidden));
   vlog_set_loc(type, loc);

   vlog_node_t queue = vlog_new(V_VAR_DECL);
   vlog_set_ident(queue, ident_sprintf("%s$q", istr(vlog_ident(hidden))));
   vlog_set_type(queue, type);
   vlog_set_loc(queue, loc);
   vlog_add_decl(current_module, queue);
   return queue;
}

static vlog_node_t rewrite_deferred_assertion(vlog_node_t v)
{
   // The expression of a deferred assertion is evaluated where the
   // assertion is reached but the action block runs after the last
   // evaluation before the Observed region, in the Reactive region, or for
   // a final deferred assertion in the Postponed region.  In a module this
   // is a process which is kicked at each evaluation.  Elsewhere, or if the
   // action block uses a local variable, a process is forked which waits
   // for that region.

   const bool final = vlog_subkind(v) == V_ASSERT_FINAL;
   const int nconds = vlog_conds(v);
   const loc_t *loc = vlog_loc(v);

   static unsigned count = 0;
   const unsigned n = count++;

   vlog_node_t block = vlog_new(V_SEQ_BLOCK);
   vlog_set_loc(block, loc);

   // The arguments of a system task call in the action block are the
   // values when the assertion was evaluated
   bool local = false;
   for (int i = 0; i < nconds; i++)
      vlog_visit(vlog_cond(v, i), check_monitor_arg_cb, &local);

   const bool process = current_module != NULL && class_depth == 0 && !local;

   vlog_node_t tmp = vlog_new(V_SEQ_BLOCK);
   defer_capture_t dc = {
      .serial = n,
      .scope  = tmp,
      .kind   = process ? V_VAR_DECL : V_LOCAL_DECL,
   };
   for (int i = 0; i < nconds; i++)
      defer_capture_stmt(&dc, vlog_cond(v, i));

   vlog_node_t value = vlog_new(dc.kind);
   vlog_set_ident(value, ident_sprintf("$dexpr%u", n));
   vlog_set_type(value, logic_type());
   vlog_set_loc(value, loc);
   vlog_add_decl(process ? current_module : block, value);

   const int ncopies = vlog_decls(tmp);
   for (int i = 0; i < ncopies; i++)
      vlog_add_decl(process ? current_module : block, vlog_decl(tmp, i));

   vlog_node_t not1 = vlog_new(V_UNARY);
   vlog_set_subkind(not1, V_UNARY_NOT);
   vlog_set_value(not1, vlog_value(v));
   vlog_set_loc(not1, loc);

   vlog_node_t not2 = vlog_new(V_UNARY);
   vlog_set_subkind(not2, V_UNARY_NOT);
   vlog_set_value(not2, not1);
   vlog_set_loc(not2, loc);

   vlog_node_t assign = vlog_new(V_BASSIGN);
   vlog_set_target(assign, defer_ref(value, loc));
   vlog_set_value(assign, not2);
   vlog_set_loc(assign, loc);
   vlog_add_stmt(block, assign);

   for (unsigned i = 0; i < dc.decls.count; i++) {
      vlog_node_t copy = vlog_new(V_BASSIGN);
      vlog_set_target(copy, defer_ref(dc.copies.items[i], loc));
      vlog_set_value(copy, defer_ref(dc.decls.items[i], loc));
      vlog_set_loc(copy, loc);
      vlog_add_stmt(block, copy);
   }

   ACLEAR(dc.decls);
   ACLEAR(dc.copies);

   vlog_node_t inner = vlog_new(vlog_kind(v));
   vlog_set_subkind(inner, V_ASSERT_SIMPLE);
   vlog_set_value(inner, defer_ref(value, loc));
   vlog_set_loc(inner, loc);
   for (int i = 0; i < nconds; i++)
      vlog_add_cond(inner, vlog_cond(v, i));

   deferred_inner = inner;

   if (!process) {
      // An observed deferred assertion runs its action block in the
      // Reactive region and a final one in the Postponed region
      vlog_node_t wait = vlog_new(V_SYS_TCALL);
      vlog_set_ident(wait, ident_new(final ? "$__postponed" : "$__reactive"));
      vlog_set_subkind(wait, V_SYSTF_NONE);
      vlog_set_ival(wait, 0);
      vlog_set_loc(wait, loc);

      if (final) {
         vlog_node_t late = vlog_new(V_NUMBER);
         vlog_set_number(late, number_from_int(1));
         vlog_set_loc(late, loc);
         vlog_add_param(wait, late);
      }

      vlog_node_t body = vlog_new(V_SEQ_BLOCK);
      vlog_set_loc(body, loc);
      vlog_add_stmt(body, wait);
      vlog_add_stmt(body, inner);

      vlog_node_t fork = vlog_new(V_FORK);
      vlog_set_subkind(fork, V_FORK_JOIN_NONE);
      vlog_set_loc(fork, loc);
      vlog_add_stmt(fork, body);
      vlog_add_stmt(block, fork);
      return block;
   }

   // The value and the captured arguments of each evaluation wait in a
   // queue until the report matures
   vlog_node_t queues[ncopies + 1];
   queues[0] = defer_queue_decl(value, loc);
   for (int i = 0; i < ncopies; i++)
      queues[i + 1] = defer_queue_decl(vlog_decl(tmp, i), loc);

   for (int i = 0; i <= ncopies; i++) {
      vlog_node_t hidden = i == 0 ? value : vlog_decl(tmp, i - 1);
      vlog_node_t push = defer_queue_call(queues[i], "push_back",
                                          V_METHOD_PUSH_BACK, loc);
      vlog_add_param(push, defer_ref(hidden, loc));
      vlog_add_stmt(block, push);
   }

   vlog_node_t live = vlog_new(V_VAR_DECL);
   vlog_set_ident(live, ident_sprintf("$dlive%u", n));
   vlog_set_type(live, logic_type());
   vlog_set_loc(live, loc);
   vlog_add_decl(current_module, live);

   vlog_node_t report = vlog_new(V_WHILE);
   vlog_set_loc(report, loc);
   vlog_set_value(report, defer_queue_call(queues[0], "size",
                                           V_METHOD_SIZE, loc));

   vlog_node_t each = vlog_new(V_SEQ_BLOCK);
   vlog_set_loc(each, loc);
   vlog_add_stmt(report, each);

   for (int i = 0; i <= ncopies; i++) {
      vlog_node_t hidden = i == 0 ? value : vlog_decl(tmp, i - 1);
      vlog_node_t pop = vlog_new(V_BASSIGN);
      vlog_set_target(pop, defer_ref(hidden, loc));
      vlog_set_value(pop, defer_queue_call(queues[i], "pop_front",
                                           V_METHOD_POP_FRONT, loc));
      vlog_set_loc(pop, loc);
      vlog_add_stmt(each, pop);
   }

   // A report is dropped if its process reached a flush point since the
   // evaluation (16.4.4)
   vlog_node_t check = vlog_new(V_SYS_TCALL);
   vlog_set_ident(check, ident_new("$__defer"));
   vlog_set_subkind(check, V_SYSTF_NONE);
   vlog_set_ival(check, 0);
   vlog_set_loc(check, loc);

   vlog_node_t num3 = vlog_new(V_NUMBER);
   vlog_set_number(num3, number_from_int(n));
   vlog_set_loc(num3, loc);
   vlog_add_param(check, num3);
   vlog_add_param(check, defer_ref(live, loc));

   vlog_node_t zero = vlog_new(V_NUMBER);
   vlog_set_number(zero, number_from_int(0));
   vlog_set_loc(zero, loc);
   vlog_add_param(check, zero);
   vlog_add_stmt(each, check);

   vlog_node_t cond = vlog_new(V_IF);
   vlog_set_loc(cond, loc);
   vlog_node_t c0 = vlog_new(V_COND);
   vlog_set_value(c0, defer_ref(live, loc));
   vlog_set_loc(c0, loc);
   vlog_add_stmt(c0, inner);
   vlog_add_cond(cond, c0);
   vlog_add_stmt(each, cond);

   vlog_node_t kick = vlog_new(V_VAR_DECL);
   vlog_set_ident(kick, ident_sprintf("$dkick%u", n));
   vlog_set_type(kick, logic_type());
   vlog_set_loc(kick, loc);
   vlog_add_decl(current_module, kick);

   // The blocks around the assertion and its label name what a disable
   // statement cancels (16.4.4)
   vlog_node_t scopes = vlog_new(V_SYS_TCALL);
   vlog_set_ident(scopes, ident_new("$__defer_scope"));
   vlog_set_subkind(scopes, V_SYSTF_NONE);
   vlog_set_ival(scopes, 0);
   vlog_set_loc(scopes, loc);

   vlog_node_t snum = vlog_new(V_NUMBER);
   vlog_set_number(snum, number_from_int(n));
   vlog_set_loc(snum, loc);
   vlog_add_param(scopes, snum);

   hier_owner_t owner;
   vlog_node_t pkg;
   ident_t suffix = vlog_symtab_hier_suffix(symtab, &owner, &pkg);
   if (suffix != NULL) {
      const char *p = istr(suffix);
      for (;;) {
         const char *dot = strchr(p, '.');
         ident_t part = dot != NULL ? ident_new_n(p, dot - p) : ident_new(p);
         vlog_node_t h = vlog_new(V_NUMBER);
         vlog_set_number(h, number_from_int(vlog_label_hash(part) & 0x7fffffff));
         vlog_set_loc(h, loc);
         vlog_add_param(scopes, h);
         if (dot == NULL)
            break;
         p = dot + 1;
      }
   }

   if (deferred_label != NULL) {
      vlog_node_t h = vlog_new(V_NUMBER);
      vlog_set_number(h, number_from_int(
                         vlog_label_hash(deferred_label) & 0x7fffffff));
      vlog_set_loc(h, loc);
      vlog_add_param(scopes, h);
   }

   vlog_add_stmt(block, scopes);

   vlog_node_t defer = vlog_new(V_SYS_TCALL);
   vlog_set_ident(defer, ident_new("$__defer"));
   vlog_set_subkind(defer, V_SYSTF_NONE);
   vlog_set_ival(defer, 0);
   vlog_set_loc(defer, loc);

   vlog_node_t num = vlog_new(V_NUMBER);
   vlog_set_number(num, number_from_int(n));
   vlog_set_loc(num, loc);
   vlog_add_param(defer, num);
   vlog_add_param(defer, defer_ref(kick, loc));
   vlog_add_stmt(block, defer);

   vlog_node_t ev = vlog_new(V_EVENT);
   vlog_set_subkind(ev, V_EVENT_LEVEL);
   vlog_set_value(ev, defer_ref(kick, loc));
   vlog_set_loc(ev, loc);

   vlog_node_t ctrl = vlog_new(V_EVENT_CONTROL);
   vlog_set_loc(ctrl, loc);
   vlog_add_param(ctrl, ev);

   vlog_node_t timing = vlog_new(V_TIMING);
   vlog_set_value(timing, ctrl);
   vlog_set_loc(timing, loc);

   if (final)
      vlog_add_stmt(timing, report);
   else {
      // The report of an observed deferred assertion matures in the
      // Observed region and its action block runs in the Reactive region,
      // where it may assign variables which make the assertion run again
      // in the same time step (16.4.1)
      vlog_node_t body = vlog_new(V_SEQ_BLOCK);
      vlog_set_loc(body, loc);

      vlog_node_t reactive = vlog_new(V_SYS_TCALL);
      vlog_set_ident(reactive, ident_new("$__reactive"));
      vlog_set_subkind(reactive, V_SYSTF_NONE);
      vlog_set_ival(reactive, 0);
      vlog_set_loc(reactive, loc);
      vlog_add_stmt(body, reactive);

      vlog_node_t done = vlog_new(V_SYS_TCALL);
      vlog_set_ident(done, ident_new("$__defer"));
      vlog_set_subkind(done, V_SYSTF_NONE);
      vlog_set_ival(done, 0);
      vlog_set_loc(done, loc);

      vlog_node_t num2 = vlog_new(V_NUMBER);
      vlog_set_number(num2, number_from_int(n));
      vlog_set_loc(num2, loc);
      vlog_add_param(done, num2);
      vlog_add_stmt(body, done);

      vlog_add_stmt(body, report);
      vlog_add_stmt(timing, body);
   }

   vlog_node_t always = vlog_new(V_ALWAYS);
   vlog_set_subkind(always, V_ALWAYS_PLAIN);
   vlog_set_ident(always, ident_sprintf(final ? "$postponed_s%ud"
                                        : "$deferred_d%u", n));
   vlog_set_loc(always, loc);
   vlog_add_stmt(always, timing);
   vlog_add_stmt(current_module, always);

   return block;
}

typedef struct {
   vlog_node_t  decl;
   vlog_node_t *actuals;
} let_expand_t;

static bool let_formal_pred(vlog_node_t v, void *ctx)
{
   let_expand_t *e = ctx;

   if (vlog_kind(v) != V_REF || !vlog_has_ref(v))
      return false;

   vlog_node_t d = vlog_ref(v);
   const int nports = vlog_ports(e->decl);
   for (int i = 0; i < nports; i++) {
      if (vlog_port(e->decl, i) == d)
         return true;
   }

   return false;
}

static vlog_node_t let_formal_rewrite(vlog_node_t v, void *ctx)
{
   let_expand_t *e = ctx;

   if (!let_formal_pred(v, ctx))
      return v;

   const int nports = vlog_ports(e->decl);
   for (int i = 0; i < nports; i++) {
      if (vlog_port(e->decl, i) == vlog_ref(v))
         return e->actuals[i];
   }

   return v;
}

static vlog_node_t expand_let(vlog_node_t decl, vlog_node_t call)
{
   // The body is expanded with the actual arguments in place of the formals

   if (vlog_stmts(decl) == 0) {
      // Called from its own body
      error_at(vlog_loc(call), "recursive use of let %pQ is not allowed",
               vlog_ident(decl));
      return call;
   }

   const int nports = vlog_ports(decl);
   const int nparams = vlog_params(call);

   vlog_node_t *actuals LOCAL = xcalloc_array(MAX(nports, 1),
                                              sizeof(vlog_node_t));

   for (int i = 0; i < nports; i++) {
      vlog_node_t port = vlog_port(decl, i);
      for (int j = 0; j < nparams; j++) {
         vlog_node_t p = vlog_param(call, j);
         if (vlog_kind(p) != V_NAMED_ARG) {
            if (j == i)
               actuals[i] = p;
         }
         else if (vlog_ident(p) == vlog_ident(port)) {
            if (vlog_has_value(p))
               actuals[i] = vlog_value(p);
            break;
         }
      }

      if (actuals[i] != NULL && vlog_kind(actuals[i]) == V_EMPTY)
         actuals[i] = NULL;

      if (actuals[i] == NULL) {
         if (!vlog_has_value(port)) {
            error_at(vlog_loc(call), "missing value for argument %pQ",
                     vlog_ident(port));
            return call;
         }

         actuals[i] = vlog_value(port);
      }
   }

   let_expand_t e = { .decl = decl, .actuals = actuals };

   vlog_node_t copy = vlog_copy(vlog_stmt(decl, 0), let_formal_pred, &e);
   return vlog_rewrite(copy, NULL, let_formal_rewrite, &e);
}

static bool is_let_declaration(void)
{
   return peek() == tID && peek_ident_nth(1) == ident_new("let")
      && peek_nth(2) == tID && (peek_nth(3) == tLPAREN || peek_nth(3) == tEQ);
}

static void p_let_declaration(void)
{
   // let let_identifier [ ( [ let_port_list ] ) ] = expression ;

   BEGIN("let declaration");

   consume(tID);

   vlog_node_t v = vlog_new(V_FUNC_DECL);
   vlog_set_ident(v, p_identifier());
   vlog_set_ident2(v, vlog_symtab_qualified(symtab));
   vlog_set_flags(v, VLOG_F_LET);
   vlog_set_loc(v, &state.last_loc);

   vlog_symtab_put(symtab, v);
   vlog_symtab_push(symtab, v);

   if (optional(tLPAREN)) {
      if (peek() != tRPAREN)
         p_tf_port_list(v);
      consume(tRPAREN);
   }

   consume(tEQ);
   vlog_add_stmt(v, p_expression());
   consume(tSEMI);

   vlog_symtab_pop(symtab);
}

static vlog_node_t p_tf_call(vlog_kind_t kind)
{
   // ps_or_hierarchical_tf_identifier { attribute_instance }
   //    [ ( list_of_arguments ) ]

   BEGIN("task or function call");

   vlog_node_t scope = qualify_scope;
   const bool failed = qualify_failed;
   qualify_scope = NULL;
   qualify_failed = false;

   vlog_node_t v = vlog_new(kind);
   vlog_set_ident(v, p_identifier());

   optional_attributes();

   if (optional(tLPAREN)) {
      vlog_node_t saved_callee = callee_decl;
      if (scope != NULL)
         callee_decl = failed ? NULL : vlog_scope_member(scope, vlog_ident(v));
      else
         callee_decl = !failed ? vlog_symtab_query(symtab, vlog_ident(v)) : NULL;
      p_list_of_arguments(v);
      callee_decl = saved_callee;
      consume(tRPAREN);
   }

   if (scope != NULL && vlog_params(v) == 2
       && vlog_ident(v) == ident_new("get_coverage"))
      vlog_set_ident(v, ident_new("get_coverage_n"));

   vlog_set_loc(v, CURRENT_LOC);

   if (scope != NULL)
      vlog_symtab_lookup_in(symtab, v, scope);
   else if (!failed)
      vlog_symtab_lookup(symtab, v);   // Otherwise reported already

   if (kind == V_USER_FCALL && vlog_has_ref(v)
       && vlog_kind(vlog_ref(v)) == V_FUNC_DECL
       && (vlog_flags(vlog_ref(v)) & VLOG_F_LET))
      return expand_let(vlog_ref(v), v);

   return v;
}

static vlog_node_t p_method_call_root(void)
{
   // primary | implicit_class_handle

   BEGIN("method call root");

   vlog_node_t v = vlog_new(V_REF);
   vlog_set_ident(v, p_identifier());
   vlog_set_loc(v, CURRENT_LOC);

   vlog_symtab_lookup(symtab, v);
   return v;
}

static void p_method_call_body(vlog_node_t v)
{
   // method_identifier { attribute_instance } [ ( list_of_arguments ) ]
   //   | built_in_method_call

   BEGIN("method call body");

   vlog_set_ident(v, p_identifier());

   if (optional(tLPAREN)) {
      p_list_of_arguments(v);
      consume(tRPAREN);
   }

   if (vlog_params(v) == 2 && vlog_ident(v) == ident_new("get_inst_coverage"))
      vlog_set_ident(v, ident_new("get_inst_coverage_n"));
   else if (vlog_params(v) == 2 && vlog_ident(v) == ident_new("get_coverage"))
      vlog_set_ident(v, ident_new("get_coverage_n"));
}

static vlog_node_t p_method_call(void)
{
   // method_call_root . method_call_body

   vlog_node_t v = vlog_new(V_METHOD_CALL);
   vlog_set_value(v, p_method_call_root());

   consume(tDOT);

   p_method_call_body(v);

   vlog_set_loc(v, CURRENT_LOC);
   vlog_symtab_lookup(symtab, v);
   return v;
}

static vlog_node_t p_subroutine_call(vlog_kind_t kind)
{
   // tf_call | system_tf_call | method_call | [ std:: ] randomize_call

   BEGIN("subroutine call");

   if (peek() == tSYSTASK)
      return p_system_tf_call(kind);
   else if (is_std_randomize())
      return p_std_randomize();
   else if (qualify_scope != NULL || qualify_failed)
      return p_tf_call(kind);
   else if (is_implicit_this_call())
      return p_implicit_this_call();
   else if (peek_nth(2) == tDOT)
      return p_method_call();
   else
      return p_tf_call(kind);
}

static vlog_node_t p_mintypmax_expression(void)
{
   // expression | expression : expression : expression

   BEGIN("mintypmax expression");

   vlog_node_t e = p_expression();

   if (optional(tCOLON)) {
      vlog_node_t mtm = vlog_new(V_MIN_TYP_MAX);
      vlog_set_left(mtm, e);
      vlog_set_value(mtm, p_expression());

      consume(tCOLON);
      vlog_set_right(mtm, p_expression());

      return mtm;
   }

   return e;
}

static vlog_node_t p_concatenation(vlog_node_t head)
{
   // { expression { , expression } }

   BEGIN_WITH_HEAD("concatenation", head);

   if (head == NULL) {
      consume(tLBRACE);
      head = p_expression();
   }

   vlog_node_t v = vlog_new(V_CONCAT);
   vlog_add_param(v, head);

   while (optional(tCOMMA))
      vlog_add_param(v, p_expression());

   consume(tRBRACE);

   vlog_set_loc(v, CURRENT_LOC);
   return v;
}

static vlog_node_t p_multiple_concatenation(vlog_node_t head)
{
   // { expression concatenation }

   BEGIN_WITH_HEAD("multiple concatenation", head);

   vlog_node_t v = p_concatenation(NULL);
   vlog_set_value(v, head);

   consume(tRBRACE);

   vlog_set_loc(v, CURRENT_LOC);
   return v;
}

static hash_t *vhdl_types = NULL;
static hash_t *vhdl_packages = NULL;

static ident_t vhdl_lower_name(ident_t id)
{
   LOCAL_TEXT_BUF tb = tb_new();
   tb_istr(tb, id);
   tb_downcase(tb);
   return ident_new(tb_get(tb));
}

static vlog_node_t import_vhdl_type(type_t type)
{
   // VHDL records and arrays are visible in System Verilog as packed
   // types with the same element order: the first element is leftmost
   // and the port shim connects them element by element

   if (vhdl_types == NULL)
      vhdl_types = hash_new(64);

   vlog_node_t cached = hash_get(vhdl_types, type);
   if (cached != NULL)
      return cached;

   vlog_node_t result = NULL;
   const loc_t *loc = &state.last_loc;

   if (type_is_record(type)) {
      type_t base = type_base_recur(type);

      result = vlog_new(V_STRUCT_DECL);
      vlog_set_flags(result, VLOG_F_PACKED);
      vlog_set_loc(result, loc);

      const int nfields = type_fields(base);
      for (int i = 0; i < nfields; i++) {
         tree_t f = type_field(base, i);
         vlog_node_t ft = import_vhdl_type(tree_type(f));
         if (ft == NULL)
            return NULL;

         vlog_node_t d = vlog_new(V_VAR_DECL);
         vlog_set_ident(d, vhdl_lower_name(tree_ident(f)));
         vlog_set_type(d, ft);
         vlog_set_loc(d, loc);
         vlog_add_decl(result, d);
      }
   }
   else if (type_is_array(type)) {
      if (type_is_unconstrained(type))
         return NULL;

      type_t elem = type_elem(type);
      vlog_node_t et = import_vhdl_type(elem);
      if (et == NULL)
         return NULL;

      result = vlog_new(V_DATA_TYPE);
      vlog_set_subkind(result, DT_LOGIC);
      vlog_set_loc(result, loc);

      if (type_is_record(elem))
         vlog_set_type(result, et);
      else if (type_is_array(elem) && vlog_has_type(et))
         vlog_set_type(result, vlog_type(et));   // Inner dimensions added below

      const int ndims = dimension_of(type);
      for (int i = 0; i < ndims; i++) {
         tree_t r = range_of(type, i);
         int64_t left, right;
         if (r == NULL || !folded_int(tree_left(r), &left)
             || !folded_int(tree_right(r), &right))
            return NULL;

         vlog_node_t ln = vlog_new(V_NUMBER);
         vlog_set_number(ln, number_from_int(left));
         vlog_set_loc(ln, loc);

         vlog_node_t rn = vlog_new(V_NUMBER);
         vlog_set_number(rn, number_from_int(right));
         vlog_set_loc(rn, loc);

         vlog_node_t dim = vlog_new(V_DIMENSION);
         vlog_set_subkind(dim, V_DIM_PACKED);
         vlog_set_left(dim, ln);
         vlog_set_right(dim, rn);
         vlog_set_loc(dim, loc);

         vlog_add_range(result, dim);
      }

      // An array of arrays has the same layout as more dimensions
      if (type_is_array(elem)) {
         const int nranges = vlog_ranges(et);
         for (int i = 0; i < nranges; i++)
            vlog_add_range(result, vlog_range(et, i));
      }
   }
   else if (type_eq(type_base_recur(type), ieee_type(IEEE_STD_ULOGIC))) {
      result = vlog_new(V_DATA_TYPE);
      vlog_set_subkind(result, DT_LOGIC);
      vlog_set_loc(result, loc);
   }
   else
      return NULL;   // Not visible from System Verilog

   hash_put(vhdl_types, type, result);
   return result;
}

static vlog_node_t import_vhdl_package(tree_t pack, ident_t id)
{
   if (vhdl_packages != NULL) {
      vlog_node_t v = hash_get(vhdl_packages, pack);
      if (v != NULL)
         return v;
   }
   else
      vhdl_packages = hash_new(16);

   vlog_node_t v = vlog_new(V_PACKAGE);
   vlog_set_ident(v, tree_ident(pack));
   vlog_set_ident2(v, id);
   vlog_set_loc(v, &state.last_loc);

   const int ndecls = tree_decls(pack);
   for (int i = 0; i < ndecls; i++) {
      tree_t d = tree_decl(pack, i);
      switch (tree_kind(d)) {
      case T_TYPE_DECL:
      case T_SUBTYPE_DECL:
         {
            vlog_node_t type = import_vhdl_type(tree_type(d));
            if (type == NULL)
               break;

            vlog_node_t td = vlog_new(V_TYPE_DECL);
            vlog_set_ident(td, vhdl_lower_name(tree_ident(d)));
            vlog_set_type(td, type);
            vlog_set_loc(td, &state.last_loc);
            vlog_add_decl(v, td);
         }
         break;
      default:
         break;
      }
   }

   hash_put(vhdl_packages, pack, v);
   return v;
}

static vlog_node_t package_by_name(ident_t id)
{
   lib_t work = lib_work();
   ident_t qual = ident_prefix(lib_name(work), id, '.');

   // The package being parsed is not yet in the library
   vlog_node_t du = vlog_symtab_design_unit(symtab);
   if (du != NULL && vlog_kind(du) == V_PACKAGE
       && (vlog_ident(du) == qual || vlog_ident2(du) == id))
      return du;

   scan_note_name(id);

   object_t *obj = lib_get_generic(lib_work(), qual, NULL);

   // A VHDL package uses the upper case name
   LOCAL_TEXT_BUF tb = tb_new();
   tb_istr(tb, id);
   tb_upcase(tb);
   ident_t upper = ident_new(tb_get(tb));

   if (obj == NULL || tree_from_object(obj) != NULL) {
      ident_t vqual = ident_prefix(lib_name(work), upper, '.');
      object_t *vobj = lib_get_generic(work, vqual, NULL);
      tree_t pack = tree_from_object(vobj);
      if (pack != NULL && tree_kind(pack) == T_PACKAGE)
         return import_vhdl_package(pack, id);
   }

   if (obj == NULL) {
      // Look in the other libraries on the search path
      if ((obj = search_other_libs(id, accept_package)) == NULL) {
         object_t *vobj = search_other_libs(upper, accept_vhdl_package);
         if (vobj != NULL)
            return import_vhdl_package(tree_from_object(vobj), id);
      }
   }

   vlog_node_t v = vlog_from_object(obj);
   if (v == NULL || vlog_kind(v) != V_PACKAGE) {
      diag_t *d = diag_new(DIAG_ERROR, &state.last_loc);
      diag_printf(d, "no package named %pQ in library %pi or any other "
                  "library on the search path", id, lib_name(work));
      lib_search_paths_to_diag(d);
      diag_emit(d);
      return NULL;
   }

   return v;
}

static vlog_node_t p_package_identifier(void)
{
   // identifier

   BEGIN("package identifier");

   return package_by_name(p_identifier());
}

static vlog_node_t p_scope_prefix(void)
{
   // { package_identifier :: | class_identifier :: }
   //
   // Returns the package or class where the name which follows is declared

   BEGIN("scope prefix");

   qualify_failed = false;

   vlog_node_t scope = NULL;
   do {
      if (scope == NULL && peek_nth(2) == tHASH && lib_scan_active()
          && vlog_symtab_query(symtab, peek_ident_nth(1)) == NULL) {
         // Specialisation of a class which is not available
         consume(tID);
         scan_skip_parameters();
         consume(tSCOPE);
         while (peek() == tID && peek_nth(2) == tSCOPE) {
            consume(tID);
            consume(tSCOPE);
         }
         qualify_failed = true;
         return NULL;
      }

      if (scope == NULL && peek_nth(2) == tHASH) {
         // Specialisation of a parameterised class
         vlog_node_t c = vlog_get_type(p_data_type());
         consume(tSCOPE);

         if (c != NULL && vlog_kind(c) == V_CLASS_DECL) {
            scope = c;
            continue;
         }

         qualify_failed = true;
         return NULL;
      }

      ident_t id = p_identifier();
      const loc_t loc = state.last_loc;
      consume(tSCOPE);

      if (scope == NULL) {
         vlog_node_t declared = vlog_symtab_query(symtab, id), d = declared;
         if (d != NULL && vlog_kind(d) == V_TYPE_DECL)
            d = vlog_get_type(d);

         if (d != NULL && vlog_kind(d) == V_CLASS_DECL)
            scope = d;
         else if (lib_scan_active() && is_scope_declaration(declared))
            scope = NULL;   // Declared here: not a package to be found
         else
            scope = package_by_name(id);
      }
      else {
         vlog_node_t d = vlog_scope_member(scope, id);
         if (d != NULL && vlog_kind(d) == V_TYPE_DECL)
            d = vlog_get_type(d);

         if (d == NULL || vlog_kind(d) != V_CLASS_DECL) {
            error_at(&loc, "%pQ is not a class in %s %pQ", id,
                     vlog_kind(scope) == V_PACKAGE ? "package" : "class",
                     vlog_ident(scope));
            scope = NULL;
         }
         else
            scope = d;
      }

      if (scope == NULL) {
         // Skip the rest of the qualified name
         while (peek() == tID && (peek_nth(2) == tSCOPE
                                  || (lib_scan_active()
                                      && peek_nth(2) == tHASH))) {
            const bool special = peek_nth(2) == tHASH;
            consume(tID);
            if (special)
               scan_skip_parameters();
            if (peek() == tSCOPE)
               consume(tSCOPE);
         }
         qualify_failed = true;
         return NULL;
      }
   } while (peek() == tID && peek_nth(2) == tSCOPE);

   return scope;
}

static vlog_node_t p_pattern_item_expr(vlog_node_t dt, vlog_node_t member)
{
   // A nested pattern takes the type of the enclosing array pattern or of
   // the member of the structure it initialises

   if (peek() == tTICK && peek_nth(2) == tLBRACE && dt != NULL) {
      if (vlog_kind(dt) == V_DATA_TYPE && vlog_has_type(dt)
          && vlog_ranges(dt) > 0) {
         // A packed array of structures: the elements are structures
         vlog_node_t et = vlog_get_type(vlog_type(dt));
         if (et != NULL && (vlog_kind(et) == V_STRUCT_DECL
                            || vlog_kind(et) == V_UNION_DECL))
            return p_assignment_pattern_expression(et);
      }

      if (vlog_kind(dt) == V_DATA_TYPE
          && (vlog_subkind(dt) == DT_DYNARRAY || vlog_subkind(dt) == DT_QUEUE)) {
         vlog_node_t et = vlog_get_type(vlog_type(dt));
         if (et != NULL)
            return p_assignment_pattern_expression(et);
      }
      else if (vlog_kind(dt) != V_STRUCT_DECL && vlog_kind(dt) != V_UNION_DECL)
         return p_assignment_pattern_expression(dt);
      else if (member != NULL) {
         vlog_node_t mt = vlog_get_type(vlog_type(member));
         if (mt != NULL)
            return p_assignment_pattern_expression(mt);
      }
   }

   if (peek() == tLBRACE && member != NULL && dt != NULL
       && (vlog_kind(dt) == V_STRUCT_DECL || vlog_kind(dt) == V_UNION_DECL)
       && vlog_is_fixed_unpacked(member)
       && vlog_get_type(vlog_type(member)) != NULL) {
      // An unpacked array concatenation has the same elements as a
      // positional assignment pattern
      vlog_node_t mt = vlog_get_type(vlog_type(member));

      vlog_node_t v = vlog_new(V_PATTERN_EXPR);
      vlog_set_type(v, mt);

      consume(tLBRACE);

      do {
         vlog_node_t item = vlog_new(V_PATTERN_ITEM);
         vlog_set_right(item, p_pattern_item_expr(mt, NULL));
         vlog_add_param(v, item);
      } while (optional(tCOMMA));

      consume(tRBRACE);

      vlog_set_loc(v, CURRENT_LOC);
      return v;
   }

   return p_expression();
}

static bool is_simple_type_token(token_t tok)
{
   switch (tok) {
   case tBYTE: case tSHORTINT: case tSVINT: case tLONGINT: case tINTEGER:
   case tTIME: case tBIT: case tLOGIC: case tREG: case tSVREAL: case tSHORTREAL:
   case tREALTIME: case tSTRINGK:
      return true;
   default:
      return false;
   }
}

static bool is_type_key(vlog_node_t dt)
{
   // simple_type : expression

   if (peek_nth(2) != tCOLON)
      return false;
   else if (is_simple_type_token(peek()))
      return true;
   else if (peek() != tID)
      return false;

   ident_t id = peek_ident_nth(1);

   vlog_node_t bt = dt != NULL ? vlog_get_type(dt) : NULL;
   if (bt != NULL && (vlog_kind(bt) == V_STRUCT_DECL
                      || vlog_kind(bt) == V_UNION_DECL)) {
      const int ndecls = vlog_decls(bt);
      for (int i = 0; i < ndecls; i++) {
         if (vlog_ident(vlog_decl(bt, i)) == id)
            return false;
      }
   }

   vlog_node_t d = vlog_symtab_query(symtab, id);
   return d != NULL && vlog_kind(d) == V_TYPE_DECL;
}

static bool type_key_matches(vlog_node_t key, vlog_node_t type)
{
   vlog_node_t a = vlog_get_type(key), b = vlog_get_type(type);
   if (a == NULL || b == NULL)
      return false;
   else if (a == b)
      return true;
   else if (vlog_kind(a) != V_DATA_TYPE || vlog_kind(b) != V_DATA_TYPE
            || vlog_subkind(a) != vlog_subkind(b)
            || vlog_is_signed(a) != vlog_is_signed(b)
            || vlog_ranges(a) != vlog_ranges(b))
      return false;

   switch (vlog_subkind(a)) {
   case DT_DYNARRAY:
   case DT_QUEUE:
   case DT_ASSOC:
      return false;
   default:
      break;
   }

   for (int i = 0; i < vlog_ranges(a); i++) {
      const unsigned sa = vlog_size(vlog_range(a, i));
      if (sa == 0 || sa != vlog_size(vlog_range(b, i)))
         return false;
   }

   return true;
}

static bool bit_key_matches(vlog_node_t key, vlog_node_t type)
{
   // The elements of a packed vector are single bits of the same type

   vlog_node_t a = vlog_get_type(key), b = vlog_get_type(type);
   return a != NULL && b != NULL && vlog_kind(a) == V_DATA_TYPE
      && vlog_kind(b) == V_DATA_TYPE && vlog_ranges(b) == 1
      && vlog_subkind(a) == vlog_subkind(b) && vlog_ranges(a) == 0
      && (vlog_subkind(a) == DT_LOGIC || vlog_subkind(a) == DT_BIT);
}

static void resolve_type_keys(vlog_node_t *items, int count, vlog_node_t dt,
                              vlog_node_t expr)
{
   // A type key gives a value to each member of a structure with that type
   // which has no key of its own, and to each element of an array which has
   // no index key. The last of the keys that match is used.

   vlog_node_t bt = dt != NULL ? vlog_get_type(dt) : NULL;

   if (bt != NULL && (vlog_kind(bt) == V_STRUCT_DECL
                      || vlog_kind(bt) == V_UNION_DECL)) {
      for (int i = 0; i < count; i++) {
         if (vlog_subkind(items[i]) != 3)
            vlog_add_param(expr, items[i]);
      }

      const int ndecls = vlog_decls(bt);
      for (int i = 0; i < ndecls; i++) {
         vlog_node_t m = vlog_decl(bt, i);

         bool named = false;
         for (int j = 0; j < count && !named; j++) {
            vlog_node_t it = items[j];
            named = vlog_subkind(it) == 0 && vlog_has_left(it)
               && vlog_kind(vlog_left(it)) == V_REF
               && vlog_has_ref(vlog_left(it)) && vlog_ref(vlog_left(it)) == m;
         }

         if (named || vlog_ranges(m) > 0)
            continue;

         for (int j = count - 1; j >= 0; j--) {
            vlog_node_t it = items[j];
            if (vlog_subkind(it) != 3
                || !type_key_matches(vlog_left(it), vlog_type(m)))
               continue;

            vlog_node_t ref = vlog_new(V_REF);
            vlog_set_ident(ref, vlog_ident(m));
            vlog_set_ref(ref, m);
            vlog_set_loc(ref, vlog_loc(it));

            vlog_node_t item = vlog_new(V_PATTERN_ITEM);
            vlog_set_left(item, ref);
            vlog_set_right(item, vlog_right(it));
            vlog_set_loc(item, vlog_loc(it));
            vlog_add_param(expr, item);
            break;
         }
      }
   }
   else {
      vlog_node_t etype = dt;
      if (bt != NULL && vlog_kind(bt) == V_DATA_TYPE
          && (vlog_subkind(bt) == DT_DYNARRAY || vlog_subkind(bt) == DT_QUEUE))
         etype = vlog_type(bt);

      vlog_node_t def = NULL;
      for (int i = 0; i < count; i++) {
         vlog_node_t it = items[i];
         if (vlog_subkind(it) == 3) {
            if (etype != NULL && (type_key_matches(vlog_left(it), etype)
                                  || bit_key_matches(vlog_left(it), etype)))
               def = it;
         }
         else {
            vlog_add_param(expr, it);
            if (vlog_subkind(it) == 1 && def == NULL)
               def = it;
         }
      }

      if (def != NULL && vlog_subkind(def) == 3) {
         for (int i = 0; i < count; i++) {
            if (vlog_subkind(items[i]) == 1)
               vlog_set_right(items[i], vlog_right(def));
         }

         bool have_default = false;
         for (int i = 0; i < count; i++)
            have_default |= vlog_subkind(items[i]) == 1;

         if (!have_default) {
            vlog_node_t item = vlog_new(V_PATTERN_ITEM);
            vlog_set_subkind(item, 1);
            vlog_set_right(item, vlog_right(def));
            vlog_set_loc(item, vlog_loc(def));
            vlog_add_param(expr, item);
         }
      }
   }
}

static void p_assignment_pattern(vlog_node_t expr)
{
   // '{ expression { , expression } }
   //    | '{ structure_pattern_key : expression
   //          { , structure_pattern_key : expression } }
   //    | '{ array_pattern_key : expression
   //          { , array_pattern_key : expression } }
   //    | '{ constant_expression { expression { , expression } } }

   BEGIN("assignment pattern");

   optional(tTICK);   // An unpacked array concatenation has the same form
   consume(tLBRACE);

   const bool of_array = pattern_array_elems;
   pattern_array_elems = false;

   if (optional(tRBRACE))
      return;   // Empty queue or dynamic array

   vlog_node_t dt = NULL;
   if (vlog_has_type(expr))
      dt = vlog_type(expr);
   else
      parse_error(CURRENT_LOC, "cannot determine type of assignment pattern");

   // The keys of an associative array are expressions
   vlog_node_t bt = dt != NULL ? vlog_get_type(dt) : NULL;
   const bool assoc = bt != NULL && vlog_kind(bt) == V_DATA_TYPE
      && vlog_subkind(bt) == DT_ASSOC;

   SCOPED_A(vlog_node_t) items = AINIT;
   bool have_error = false, have_type_key = false;
   do {
      vlog_node_t v = vlog_new(V_PATTERN_ITEM);

      if (optional(tDEFAULT)) {
         vlog_set_loc(v, &state.last_loc);
         vlog_set_subkind(v, 1);
         consume(tCOLON);
         vlog_set_right(v, p_pattern_item_expr(dt, NULL));
      }
      else if (!assoc && is_type_key(dt)) {
         vlog_node_t key;
         if (is_simple_type_token(peek()))
            key = p_data_type();
         else {
            key = vlog_symtab_query(symtab, p_identifier());
            if (vlog_kind(key) != V_TYPE_DECL)
               key = vlog_type(key);
         }

         vlog_set_loc(v, &state.last_loc);
         vlog_set_subkind(v, 3);
         vlog_set_left(v, key);

         consume(tCOLON);

         if (peek() == tTICK && peek_nth(2) == tLBRACE)
            vlog_set_right(v, p_assignment_pattern_expression(
                           vlog_get_type(key)));
         else
            vlog_set_right(v, p_expression());

         have_type_key = true;
      }
      else if (!assoc && peek() == tID && peek_nth(2) == tCOLON) {
         ident_t id = p_identifier();

         vlog_node_t ref = vlog_new(V_REF);
         vlog_set_ident(ref, id);
         vlog_set_loc(ref, &state.last_loc);

         if (dt != NULL) {
            switch (vlog_kind(dt)) {
            case V_UNION_DECL:
            case V_STRUCT_DECL:
               {
                  bool found = false;
                  const int ndecls = vlog_decls(dt);
                  for (int i = 0; i < ndecls; i++) {
                     vlog_node_t d = vlog_decl(dt, i);
                     if (vlog_ident(d) == id) {
                        vlog_set_ref(ref, d);
                        found = true;
                        break;
                     }
                  }

                  if (!found && !have_error) {
                     diag_t *d = diag_new(DIAG_ERROR, &state.last_loc);
                     diag_printf(d, "struct or union has no field %pQ", id);
                     diag_hint(d, vlog_loc(dt), "data type declared here");
                     diag_emit(d);
                     have_error = true;
                  }
               }
               break;
            default:
               if (!have_error) {
                  parse_error(&state.last_loc, "cannot use structure pattern "
                              "key with this data type");
                  have_error = true;
               }
               break;
            }
         }

         vlog_set_left(v, ref);
         vlog_set_loc(v, &state.last_loc);

         consume(tCOLON);

         vlog_set_right(v, p_pattern_item_expr(
                           dt, vlog_has_ref(ref) ? vlog_ref(ref) : NULL));
      }
      else {
         // The item of a structure pattern without a key is a member in order
         vlog_node_t member = NULL;
         if (bt != NULL && (vlog_kind(bt) == V_STRUCT_DECL
                            || vlog_kind(bt) == V_UNION_DECL)
             && !of_array && items.count < vlog_decls(bt))
            member = vlog_decl(bt, items.count);

         vlog_node_t head;
         if (of_array && bt != NULL && peek() == tTICK
             && peek_nth(2) == tLBRACE
             && (vlog_kind(bt) == V_STRUCT_DECL
                 || vlog_kind(bt) == V_UNION_DECL))
            head = p_assignment_pattern_expression(dt);   // An element
         else
            head = p_pattern_item_expr(dt, member);
         vlog_set_loc(v, vlog_loc(head));

         if (optional(tCOLON)) {
            vlog_set_left(v, head);
            vlog_set_right(v, p_pattern_item_expr(dt, NULL));
         }
         else if (optional(tLBRACE)) {
            // Replication of a list of items
            vlog_set_left(v, head);

            vlog_node_t list = vlog_new(V_PATTERN_EXPR);
            do {
               vlog_node_t item = vlog_new(V_PATTERN_ITEM);
               vlog_set_right(item, p_pattern_item_expr(dt, NULL));
               vlog_add_param(list, item);
            } while (optional(tCOMMA));

            vlog_set_subkind(v, 2);
            vlog_set_right(v, list);
            consume(tRBRACE);
         }
         else
            vlog_set_right(v, head);
      }

      APUSH(items, v);
   } while (optional(tCOMMA));

   consume(tRBRACE);

   if (have_type_key)
      resolve_type_keys(items.items, items.count, dt, expr);
   else {
      for (int i = 0; i < items.count; i++)
         vlog_add_param(expr, items.items[i]);
   }
}

static vlog_node_t hier_member_decl(vlog_node_t v)
{
   // The declaration of a member of an interface or module instance which
   // is known while parsing, or NULL if the name is only bound later

   if (vlog_kind(v) == V_MEMBER_REF) {
      if (vlog_has_ref(v))
         return vlog_ref(v);
      else if (vlog_virtual_pending(v))
         return vlog_virtual_decl_or_null(v);
      return NULL;
   }
   else if (vlog_kind(v) != V_HIER_REF)
      return NULL;
   else if (vlog_has_ref(v))
      return vlog_ref(v);

   ident_t names[8];
   int nnames = 0;
   vlog_node_t first = NULL;
   for (vlog_node_t mr = vlog_value(v); ; mr = vlog_value(mr)) {
      if (vlog_kind(mr) != V_MOD_REF || vlog_params(mr) > 0 || nnames == 8)
         return NULL;

      names[nnames++] = vlog_ident(mr);
      first = mr;
      if (!vlog_has_value(mr))
         break;
   }

   for (int i = 0; i < nnames / 2; i++) {
      ident_t tmp = names[i];
      names[i] = names[nnames - 1 - i];
      names[nnames - 1 - i] = tmp;
   }

   vlog_node_t container = NULL;
   int next = 1;
   if (vlog_has_ref(first) && vlog_kind(vlog_ref(first)) == V_MOD_INST) {
      ident_t name = inst_modules != NULL
         ? hash_get(inst_modules, vlog_ref(first)) : NULL;
      if (name == NULL)
         return NULL;
      container = lookup_interface(name);
   }
   else if (current_module != NULL && vlog_kind(current_module) == V_MODULE
            && ident_casecmp(names[0], vlog_ident2(current_module)))
      container = current_module;
   else
      return NULL;

   for (; container != NULL; next++) {
      if (next > nnames)
         break;

      const ident_t id = next == nnames ? vlog_ident(v) : names[next];

      if (next == nnames) {
         const int ndecls = vlog_decls(container);
         for (int i = 0; i < ndecls; i++) {
            vlog_node_t d = vlog_decl(container, i);
            if (vlog_has_ident(d) && vlog_ident(d) == id)
               return d;
         }

         return NULL;
      }

      // Only an instance of a module declared earlier can be followed
      ident_t name = NULL;
      const int nstmts = vlog_stmts(container);
      for (int i = 0; i < nstmts && name == NULL; i++) {
         vlog_node_t list = vlog_stmt(container, i);
         if (vlog_kind(list) != V_INST_LIST || vlog_params(list) > 0)
            continue;

         const int ninst = vlog_stmts(list);
         for (int j = 0; j < ninst; j++) {
            vlog_node_t inst = vlog_stmt(list, j);
            if (vlog_kind(inst) == V_MOD_INST && vlog_ident(inst) == id
                && vlog_ranges(inst) == 0)
               name = vlog_ident(list);
         }
      }

      container = name != NULL ? lookup_interface(name) : NULL;
   }

   return NULL;
}

static vlog_node_t pattern_type_of(vlog_node_t lhs)
{
   // A slice of an array takes elements of the type of the array

   if (vlog_kind(lhs) == V_PART_SELECT) {
      vlog_node_t type = vlog_get_type(vlog_value(lhs));
      if (type != NULL && vlog_kind(type) == V_DATA_TYPE
          && vlog_subkind(type) == DT_DYNARRAY && vlog_ranges(type) == 1) {
         // A slice of an array declared with a fixed size has fewer elements
         vlog_node_t wrap = vlog_new(V_DATA_TYPE);
         vlog_set_subkind(wrap, DT_DYNARRAY);
         vlog_set_type(wrap, vlog_type(type));
         vlog_set_loc(wrap, vlog_loc(lhs));
         return wrap;
      }

      return type;
   }

   vlog_node_t type = vlog_get_type(lhs);
   if (type == NULL) {
      vlog_node_t decl = hier_member_decl(lhs);
      if (decl != NULL && vlog_has_type(decl))
         type = vlog_get_type(vlog_type(decl));
   }

   return type;
}

static vlog_node_t p_assignment_pattern_expression(vlog_node_t dt)
{
   // [ assignment_pattern_expression_type ] assignment_pattern

   BEGIN("assignment pattern expression");

   vlog_node_t v = vlog_new(V_PATTERN_EXPR);
   vlog_set_type(v, dt);

   p_assignment_pattern(v);

   vlog_set_loc(v, CURRENT_LOC);
   return v;
}

static vlog_node_t p_tagged_union_expression(vlog_node_t dt)
{
   // tagged member_identifier [ expression ]

   BEGIN("tagged union expression");

   consume(tTAGGED);

   ident_t id = p_identifier();
   const loc_t id_loc = state.last_loc;

   vlog_node_t union_type = dt != NULL ? vlog_get_type(dt) : NULL;
   if (union_type == NULL || vlog_kind(union_type) != V_UNION_DECL
       || !(vlog_flags(union_type) & VLOG_F_TAGGED)) {
      parse_error(&id_loc, "cannot determine the tagged union type of this "
                  "expression");
      union_type = NULL;
   }

   vlog_node_t member = NULL;
   if (union_type != NULL) {
      const int ndecls = vlog_decls(union_type);
      for (int i = 0; i < ndecls && member == NULL; i++) {
         if (vlog_ident(vlog_decl(union_type, i)) == id)
            member = vlog_decl(union_type, i);
      }

      if (member == NULL) {
         diag_t *d = diag_new(DIAG_ERROR, &id_loc);
         diag_printf(d, "tagged union has no member %pQ", id);
         diag_hint(d, vlog_loc(union_type), "tagged union declared here");
         diag_emit(d);
      }
   }

   vlog_node_t value = NULL;
   switch (peek()) {
   case tLPAREN:
      consume(tLPAREN);
      value = p_pattern_item_expr(
         member != NULL ? vlog_get_type(vlog_type(member)) : NULL, NULL);
      consume(tRPAREN);
      break;
   case tTICK:
      value = p_assignment_pattern_expression(
         member != NULL ? vlog_get_type(vlog_type(member)) : NULL);
      break;
   case tNUMBER:
   case tUNSNUM:
   case tREAL:
   case tSTRING:
   case tID:
      value = p_primary();
      break;
   default:
      break;
   }

   if (value == NULL) {
      value = vlog_new(V_NUMBER);
      vlog_set_number(value, number_from_int(0));
      vlog_set_loc(value, &id_loc);
   }

   vlog_node_t ref = vlog_new(V_REF);
   vlog_set_ident(ref, id);
   vlog_set_loc(ref, &id_loc);
   if (member != NULL)
      vlog_set_ref(ref, member);

   vlog_node_t item = vlog_new(V_PATTERN_ITEM);
   vlog_set_left(item, ref);
   vlog_set_right(item, value);
   vlog_set_loc(item, CURRENT_LOC);

   vlog_node_t v = vlog_new(V_PATTERN_EXPR);
   vlog_set_type(v, union_type != NULL ? dt : make_integer_atom_type(DT_INT));
   vlog_add_param(v, item);
   vlog_set_loc(v, CURRENT_LOC);
   return v;
}

static void match_unwind(int depth)
{
   // Leave the scopes of the variables bound by patterns

   while (match_depth > depth) {
      vlog_symtab_pop(symtab);
      match_depth--;
   }
}

static vlog_node_t p_pattern_constant(void)
{
   // A constant expression has the precedence of a conditional expression

   return p_binary_expression(p_nonbinary_expression(), 2);
}

static vlog_node_t pattern_shape;   // Member whose array dimensions are bound

static vlog_node_t p_pattern(vlog_node_t dt, vlog_node_t matches)
{
   // . variable_identifier | .* | constant_expression
   //   | tagged member_identifier [ pattern ]
   //   | '{ pattern { , pattern } }
   //   | '{ member_identifier : pattern { , member_identifier : pattern } }

   BEGIN("pattern");

   vlog_node_t v = vlog_new(V_MATCH_PAT);

   vlog_node_t shape = pattern_shape;
   pattern_shape = NULL;

   switch (peek()) {
   case tDOTSTAR:
      consume(tDOTSTAR);
      vlog_set_subkind(v, V_PAT_WILD);
      break;
   case tDOT:
      {
         consume(tDOT);

         vlog_node_t d = vlog_new(V_LOCAL_DECL);
         vlog_set_ident(d, p_identifier());
         vlog_set_loc(d, &state.last_loc);

         if (dt == NULL)
            parse_error(&state.last_loc, "cannot determine the type of "
                        "pattern variable %pQ", vlog_ident(d));
         else
            vlog_set_type(d, dt);

         // A variable bound to an array member is an array
         const int nranges = shape != NULL ? vlog_ranges(shape) : 0;
         for (int i = 0; i < nranges; i++)
            vlog_add_range(d, vlog_range(shape, i));

         vlog_add_decl(matches, d);
         vlog_symtab_put(symtab, d);

         vlog_set_subkind(v, V_PAT_BIND);
         vlog_set_ref(v, d);
      }
      break;
   case tTAGGED:
      {
         consume(tTAGGED);

         ident_t id = p_identifier();
         const loc_t id_loc = state.last_loc;

         vlog_set_subkind(v, V_PAT_TAGGED);

         vlog_node_t member = NULL;
         if (dt == NULL || vlog_kind(dt) != V_UNION_DECL
             || !(vlog_flags(dt) & VLOG_F_TAGGED))
            parse_error(&id_loc, "the value matched by a tagged pattern "
                        "must have a tagged union type");
         else {
            vlog_set_type(v, dt);

            const int ndecls = vlog_decls(dt);
            for (int i = 0; i < ndecls && member == NULL; i++) {
               if (vlog_ident(vlog_decl(dt, i)) == id)
                  member = vlog_decl(dt, i);
            }

            if (member == NULL) {
               diag_t *d = diag_new(DIAG_ERROR, &id_loc);
               diag_printf(d, "tagged union has no member %pQ", id);
               diag_hint(d, vlog_loc(dt), "tagged union declared here");
               diag_emit(d);
            }
            else
               vlog_set_ref(v, member);
         }

         switch (peek()) {
         case tRPAREN:
         case tRBRACE:
         case tCOMMA:
         case tCOLON:
         case tTRPLAMP:
         case tQUESTION:
         case tSEMI:
            break;
         default:
            pattern_shape = member;
            vlog_add_param(v, p_pattern(
                              member != NULL
                              ? vlog_get_type(vlog_type(member)) : NULL,
                              matches));
            break;
         }
      }
      break;
   case tTICK:
      {
         consume(tTICK);
         consume(tLBRACE);

         vlog_set_subkind(v, V_PAT_STRUCT);

         if (dt == NULL || vlog_kind(dt) != V_STRUCT_DECL) {
            parse_error(&state.last_loc, "an assignment pattern in a pattern "
                        "must match a structure");
            drop_tokens_until(&state, tRBRACE);
            consume(tRBRACE);
            break;
         }

         vlog_set_type(v, dt);

         const int ndecls = vlog_decls(dt);
         vlog_node_t *fields LOCAL = xcalloc_array(ndecls, sizeof(vlog_node_t));

         int next = 0;
         do {
            int field = next++;
            if (peek() == tID && peek_nth(2) == tCOLON) {
               ident_t id = p_identifier();
               consume(tCOLON);

               for (field = 0; field < ndecls; field++) {
                  if (vlog_ident(vlog_decl(dt, field)) == id)
                     break;
               }

               if (field == ndecls) {
                  parse_error(&state.last_loc, "structure has no member %pQ",
                              id);
                  field = -1;
               }
            }
            else if (field >= ndecls) {
               parse_error(&state.last_loc, "too many elements in structure "
                           "pattern");
               field = -1;
            }

            pattern_shape = field >= 0 ? vlog_decl(dt, field) : NULL;
            vlog_node_t sub = p_pattern(
               field >= 0 ? vlog_get_type(vlog_type(vlog_decl(dt, field)))
               : NULL, matches);
            if (field >= 0)
               fields[field] = sub;
         } while (optional(tCOMMA));

         consume(tRBRACE);

         for (int i = 0; i < ndecls; i++) {
            if (fields[i] == NULL) {
               fields[i] = vlog_new(V_MATCH_PAT);
               vlog_set_subkind(fields[i], V_PAT_WILD);
               vlog_set_loc(fields[i], &state.last_loc);
            }
            vlog_add_param(v, fields[i]);
         }
      }
      break;
   default:
      vlog_set_subkind(v, V_PAT_CONST);
      vlog_set_value(v, p_pattern_constant());
      break;
   }

   vlog_set_loc(v, CURRENT_LOC);
   return v;
}

static vlog_node_t p_matches(vlog_node_t head)
{
   // expression matches pattern

   BEGIN_WITH_HEAD("pattern match", head);

   consume(tMATCHES);

   vlog_node_t v = vlog_new(V_MATCHES);
   vlog_set_value(v, head);
   vlog_set_loc(v, &state.last_loc);

   // The variables bound by the pattern are visible in the guard and in
   // the statement or expression that follows
   vlog_symtab_push(symtab, v);
   match_depth++;

   vlog_set_left(v, p_pattern(vlog_get_type(head), v));

   vlog_set_loc(v, CURRENT_LOC);
   return v;
}

static vlog_node_t p_expression_of_type(vlog_node_t dt)
{
   if (peek() == tTICK)
      return p_assignment_pattern_expression(dt);
   else if (peek() == tTAGGED)
      return p_tagged_union_expression(dt);

   vlog_node_t saved = arm_type;
   arm_type = dt;
   vlog_node_t v = p_expression();
   arm_type = saved;
   return v;
}

static void p_casting_type(vlog_node_t cast, vlog_node_t head)
{
   // simple_type | constant_primary | signing | string | const

   BEGIN("casting type");

   assert(head != NULL);

   if (vlog_kind(head) == V_REF && vlog_has_ref(head)
       && is_data_type(vlog_ref(head))) {
      vlog_set_subkind(cast, V_CAST_TYPE);
      vlog_set_type(cast, vlog_ref(head));
      return;
   }

   if (vlog_kind(head) == V_CAST && vlog_subkind(head) == V_CAST_TYPEREF) {
      // The type of an expression: type(expr)'(value)
      vlog_set_subkind(cast, V_CAST_TYPE);
      vlog_set_type(cast, vlog_type(head));
      return;
   }

   vlog_set_subkind(cast, V_CAST_WIDTH);
   vlog_set_left(cast, head);
}


static vlog_node_t p_cast(vlog_node_t head)
{
   // casting_type ' ( expression )

   BEGIN_WITH_HEAD("cast", head);

   vlog_node_t v = vlog_new(V_CAST);

   p_casting_type(v, head);

   consume(tTICK);
   consume(tLPAREN);

   vlog_set_value(v, p_expression());

   consume(tRPAREN);

   vlog_set_loc(v, CURRENT_LOC);
   return v;
}

static vlog_node_t p_local_scope_select(void)
{
   // local :: hierarchical_identifier select

   BEGIN("local scope");

   consume(tVLOCAL);
   consume(tSCOPE);

   ident_t id = p_identifier();

   if (local_scope == NULL) {
      error_at(&state.last_loc, "local:: can only be used in the constraint "
               "block of a randomize call");
      return p_select(id);
   }

   // Skip the members of the object being randomized, but not the
   // variables of the constraint which the indexes may refer to
   select_scope = local_scope;
   return p_select(id);
}

static vlog_node_t p_streaming_concatenation(void)
{
   // { stream_operator [ slice_size ] stream_concatenation }

   BEGIN("streaming concatenation");

   consume(tLBRACE);

   vlog_node_t v = vlog_new(V_STREAM);

   switch (one_of(tSHIFTLL, tSHIFTRL)) {
   case tSHIFTLL: vlog_set_subkind(v, V_STREAM_LEFT); break;
   default:       vlog_set_subkind(v, V_STREAM_RIGHT); break;
   }

   int width = 0;
   bool type_name = false;
   if (peek() == tID && peek_nth(2) == tLBRACE) {
      // The size of a slice may be the width of a type (11.4.14.1)
      vlog_node_t d = vlog_symtab_query(symtab, peek_ident_nth(1));
      if (d != NULL && vlog_kind(d) == V_TYPE_DECL && vlog_has_type(d)) {
         vlog_node_t t = vlog_get_type(d);
         if (t != NULL && !vlog_is_dynamic_type(t)) {
            width = vlog_size(t);
            type_name = width > 0;
         }
      }
   }

   switch (type_name ? tLBRACE : peek()) {
   case tBIT:
   case tLOGIC:
   case tREG:      width = 1; break;
   case tBYTE:     width = 8; break;
   case tSHORTINT: width = 16; break;
   case tSVINT:
   case tINTEGER:  width = 32; break;
   case tLONGINT:
   case tTIME:     width = 64; break;
   case tLBRACE:
      break;
   default:
      vlog_set_value(v, p_expression());
      break;
   }

   if (width != 0) {
      drop_token(&state);   // The type or its name

      vlog_node_t size = vlog_new(V_NUMBER);
      vlog_set_number(size, number_from_int(width));
      vlog_set_value(v, size);
   }

   consume(tLBRACE);

   do {
      vlog_node_t operand = p_expression();
      if (optional(tWITH)) {
         // The slice of an array which is streamed
         consume(tLSQUARE);

         vlog_node_t ps = vlog_new(V_PART_SELECT);
         vlog_set_value(ps, operand);
         vlog_set_left(ps, p_expression());

         vlog_range_kind_t kind = V_RANGE_CONST;
         switch (one_of(tCOLON, tINDEXPOS, tINDEXNEG)) {
         case tINDEXPOS: kind = V_RANGE_POS; break;
         case tINDEXNEG: kind = V_RANGE_NEG; break;
         }

         vlog_set_subkind(ps, kind);
         vlog_set_right(ps, p_expression());

         consume(tRSQUARE);

         vlog_set_loc(ps, CURRENT_LOC);
         operand = ps;
      }

      vlog_add_param(v, operand);
   } while (optional(tCOMMA));

   consume(tRBRACE);
   consume(tRBRACE);

   vlog_set_loc(v, CURRENT_LOC);
   return v;
}

static vlog_node_t p_process_scope(void)
{
   // process :: self ( ) | process :: state_name

   BEGIN("process scope");

   consume(tID);
   consume(tSCOPE);

   ident_t id = p_identifier();
   const loc_t loc = state.last_loc;

   if (id == ident_new("self")) {
      vlog_node_t v = vlog_new(V_SYS_FCALL);
      vlog_set_ident(v, ident_new("$process_self"));
      vlog_set_subkind(v, V_SYSTF_NONE);
      vlog_set_ival(v, time_ival());

      consume(tLPAREN);
      consume(tRPAREN);

      vlog_set_loc(v, CURRENT_LOC);

      if (peek() != tDOT)
         return v;

      // Method of the handle: bound here as the result has no type
      consume(tDOT);

      static const struct {
         const char    *name;
         vlog_method_t  method;
      } methods[] = {
         { "status",  V_METHOD_PROC_STATUS },
         { "kill",    V_METHOD_PROC_KILL },
         { "await",   V_METHOD_PROC_AWAIT },
         { "suspend", V_METHOD_PROC_SUSPEND },
         { "resume",  V_METHOD_PROC_RESUME },
         { "srandom", V_METHOD_SRANDOM },
         { "set_randstate", V_METHOD_RANDSTATE_SET },
      };

      ident_t mid = p_identifier();
      const loc_t mloc = state.last_loc;

      vlog_node_t call = vlog_new(V_METHOD_CALL);
      vlog_set_value(call, v);
      vlog_set_ident(call, mid);

      bool found = false;
      for (int i = 0; i < ARRAY_LEN(methods); i++) {
         if (mid == ident_new(methods[i].name)) {
            vlog_set_subkind(call, methods[i].method);
            found = true;
         }
      }

      if (!found) {
         error_at(&mloc, "process has no method named %pQ", mid);
         vlog_set_subkind(call, V_METHOD_PROC_STATUS);
      }

      if (optional(tLPAREN)) {
         p_list_of_arguments(call);
         consume(tRPAREN);
      }

      vlog_set_loc(call, CURRENT_LOC);
      return call;
   }

   static const struct {
      const char *name;
      int         value;
   } states[] = {
      { "FINISHED", 0 }, { "RUNNING", 1 }, { "WAITING", 2 },
      { "SUSPENDED", 3 }, { "KILLED", 4 },
   };

   for (int i = 0; i < ARRAY_LEN(states); i++) {
      if (id == ident_new(states[i].name)) {
         vlog_node_t v = vlog_new(V_NUMBER);
         vlog_set_number(v, number_from_int(states[i].value));
         vlog_set_loc(v, CURRENT_LOC);
         return v;
      }
   }

   error_at(&loc, "process has no member named %pQ", id);

   vlog_node_t v = vlog_new(V_NUMBER);
   vlog_set_number(v, number_from_int(0));
   vlog_set_loc(v, &loc);
   return v;
}

static bool constraint_item = false;

static vlog_node_t p_logical_implication(vlog_node_t lhs)
{
   // expression -> expression | expression <-> expression

   const token_t tok = peek();
   if (tok != tIFIMPL && tok != tIFFIMPL)
      return lhs;

   consume(tok);

   vlog_node_t rhs = p_logical_implication(p_expression());

   vlog_node_t not1 = vlog_new(V_UNARY);
   vlog_set_subkind(not1, V_UNARY_NOT);
   vlog_set_value(not1, lhs);
   vlog_set_loc(not1, CURRENT_LOC);

   vlog_node_t v = vlog_new(V_BINARY);
   vlog_set_left(v, not1);
   if (tok == tIFIMPL) {
      vlog_set_subkind(v, V_BINARY_LOG_OR);
      vlog_set_right(v, rhs);
   }
   else {
      vlog_node_t not2 = vlog_new(V_UNARY);
      vlog_set_subkind(not2, V_UNARY_NOT);
      vlog_set_value(not2, rhs);
      vlog_set_loc(not2, CURRENT_LOC);

      vlog_set_subkind(v, V_BINARY_LOG_EQ);
      vlog_set_right(v, not2);
   }

   vlog_set_loc(v, CURRENT_LOC);
   return v;
}

static bool at_argless_call(vlog_node_t scope)
{
   // A function with no arguments can be called without parentheses

   assert(peek() == tID);

   ident_t id = peek_ident_nth(1);
   vlog_node_t d = scope != NULL ? vlog_scope_member(scope, id)
      : vlog_symtab_query(symtab, id);

   if (d == NULL || vlog_kind(d) != V_FUNC_DECL || d == current_func
       || (vlog_flags(d) & (VLOG_F_LET)) != 0
       || !vlog_has_type(d))
      return false;

   // Every argument must have a default value (13.5.3)
   const int nports = vlog_ports(d);
   for (int i = 0; i < nports; i++) {
      vlog_node_t p = vlog_port(d, i);
      if (!vlog_has_value(p))
         return false;
   }

   return peek_nth(2) != tDOT && peek_nth(2) != tLSQUARE;
}

static vlog_node_t global_clock_call(vlog_node_t call)
{
   // $global_clock and the sampled value functions of the global clock
   // are the corresponding functions with the global clocking event (16.9.4)

   static const struct {
      const char *name, *base;
   } funcs[] = {
      { "$rose_gclk", "$rose" }, { "$fell_gclk", "$fell" },
      { "$stable_gclk", "$stable" }, { "$changed_gclk", "$changed" },
      { "$past_gclk", "$past" },
   };

   if (vlog_kind(call) != V_SYS_FCALL)
      return call;

   ident_t id = vlog_ident(call);

   if (id == ident_new("$global_clock") && vlog_params(call) == 0)
      return global_clock_ref(vlog_loc(call));

   for (int i = 0; i < ARRAY_LEN(funcs); i++) {
      if (id != ident_new(funcs[i].name))
         continue;

      vlog_set_ident(call, ident_new(funcs[i].base));
      if (i == 4) {
         while (vlog_params(call) < 3) {
            vlog_node_t e = vlog_new(V_EMPTY);
            vlog_set_loc(e, vlog_loc(call));
            vlog_add_param(call, e);
         }
      }

      vlog_add_param(call, global_clock_control(vlog_loc(call)));
      break;
   }

   return call;
}

typedef struct {
   int     depth;
   hset_t *done;
} future_ctx_t;

static bool is_future_gclk(vlog_node_t v, const char **base)
{
   static const struct {
      const char *name, *base;
   } funcs[] = {
      { "$future_gclk", NULL }, { "$rising_gclk", "$rose" },
      { "$falling_gclk", "$fell" }, { "$steady_gclk", "$stable" },
      { "$changing_gclk", "$changed" },
   };

   if (vlog_kind(v) != V_SYS_FCALL)
      return false;

   for (int i = 0; i < ARRAY_LEN(funcs); i++) {
      if (vlog_ident(v) == ident_new(funcs[i].name)) {
         if (base != NULL)
            *base = funcs[i].base;
         return true;
      }
   }

   return false;
}

static void future_pre_cb(vlog_node_t v, void *ctx)
{
   future_ctx_t *fc = ctx;
   if (is_future_gclk(v, NULL))
      fc->depth++;
}

static vlog_node_t future_post_cb(vlog_node_t v, void *ctx)
{
   future_ctx_t *fc = ctx;

   const char *base;
   if (is_future_gclk(v, &base)) {
      fc->depth--;
      if (vlog_params(v) != 1) {
         error_at(vlog_loc(v), "%s takes one argument", istr(vlog_ident(v)));
         return v;
      }
      else if (base == NULL)
         return vlog_param(v, 0);

      vlog_set_ident(v, ident_new(base));
      vlog_add_param(v, global_clock_control(vlog_loc(v)));
      return v;
   }
   else if (vlog_kind(v) == V_REF && fc->depth == 0 && vlog_has_ref(v)
            && !hset_contains(fc->done, v)) {
      switch (vlog_kind(vlog_ref(v))) {
      case V_VAR_DECL:
      case V_NET_DECL:
      case V_PORT_DECL:
         break;
      default:
         return v;
      }

      // The value at the previous tick of the global clock
      hset_insert(fc->done, v);
      vlog_node_t call = vlog_new(V_SYS_FCALL);
      vlog_set_ident(call, ident_new("$past"));
      vlog_set_subkind(call, V_SYSTF_NONE);
      vlog_set_ival(call, 0);
      vlog_set_loc(call, vlog_loc(v));
      vlog_add_param(call, v);

      vlog_node_t one = vlog_new(V_NUMBER);
      vlog_set_number(one, number_from_int(1));
      vlog_set_loc(one, vlog_loc(v));
      vlog_add_param(call, one);

      vlog_node_t gate = vlog_new(V_EMPTY);
      vlog_set_loc(gate, vlog_loc(v));
      vlog_add_param(call, gate);

      vlog_add_param(call, global_clock_control(vlog_loc(v)));
      return call;
   }

   return v;
}

static void find_future_gclk_cb(vlog_node_t v, void *ctx)
{
   if (is_future_gclk(v, NULL))
      *(bool *)ctx = true;
}

static vlog_node_t sva_future_gclk(vlog_node_t expr)
{
   // A boolean expression with future global clocking functions is
   // evaluated at the next tick of the global clock, so the other values
   // in it are those of the previous tick (16.9.4)

   bool found = false;
   vlog_visit_only(expr, find_future_gclk_cb, &found, V_SYS_FCALL);
   if (!found)
      return expr;

   future_ctx_t fc = { .depth = 0, .done = hset_new(16) };
   vlog_node_t result = vlog_rewrite(expr, future_pre_cb, future_post_cb, &fc);
   hset_free(fc.done);
   return result;
}

static vlog_node_t p_primary(void)
{
   // primary_literal | empty_queue
   // | [ class_qualifier | package_scope ] hierarchical_identifier select
   // | concatenation [ [ range_expression ] ]
   // | multiple_concatenation [ [ range_expression ] ]
   // | function_subroutine_call
   // | let_expression | ( mintypmax_expression ) | cast
   // | assignment_pattern_expression | streaming_concatenation
   // | sequence_method_call | this | $ | null

   BEGIN("primary");

   // The top level modules are found by name so $root only has to be skipped
   if (peek() == tSYSTASK && peek_nth(2) == tDOT) {
      ident_t root = p_system_tf_identifier();
      if (root != ident_new("$root"))
         parse_error(&state.last_loc, "unexpected system name %pi before '.'",
                     root);

      consume(tDOT);
   }

   vlog_node_t head;
   switch (peek()) {
   case tTYPE:
      {
         // The type reference operand of an equality or case comparison

         vlog_node_t dt = p_data_type();

         vlog_node_t zero = vlog_new(V_NUMBER);
         vlog_set_number(zero, number_from_int(0));
         vlog_set_loc(zero, &state.last_loc);

         head = vlog_new(V_CAST);
         vlog_set_subkind(head, V_CAST_TYPEREF);
         vlog_set_type(head, dt);
         vlog_set_value(head, zero);
         vlog_set_loc(head, CURRENT_LOC);
      }
      break;
   case tID:
      if (is_std_randomize()) {
         head = p_std_randomize();
         break;
      }

      switch (peek_nth(2)) {
      case tLPAREN:
      case tATTRBEGIN:
         head = p_subroutine_call(V_USER_FCALL);
         break;
      case tDOT:
         {
            vlog_node_t d = peek_reference();
            if (sva_is_decl(d) && vlog_subkind(d) == V_SVA_SEQUENCE
                && peek_nth(3) == tID
                && (peek_ident_nth(3) == ident_new("triggered")
                    || peek_ident_nth(3) == ident_new("matched"))) {
               consume(tID);
               head = p_sva_trigger(d);
               consume(tDOT);
               consume(tID);
            }
            else
               head = p_select(p_identifier());
         }
         break;
      case tSCOPE:
         if (is_process_name()) {
            head = p_process_scope();
            break;
         }

         qualify_scope = p_scope_prefix();
         if (peek() == tNEW) {
            head = p_class_new(qualify_scope);
            qualify_scope = NULL;
         }
         else if (peek() == tID && (peek_nth(2) == tLPAREN
                                    || peek_nth(2) == tATTRBEGIN
                                    || at_argless_call(qualify_scope)))
            head = p_subroutine_call(V_USER_FCALL);
         else
            head = p_select(p_identifier());
         break;
      case tHASH:
         if (is_specialised_scope()) {
            qualify_scope = p_scope_prefix();
            if (peek() == tNEW) {
               head = p_class_new(qualify_scope);
               qualify_scope = NULL;
            }
            else if (peek() == tID && (peek_nth(2) == tLPAREN
                                       || peek_nth(2) == tATTRBEGIN))
               head = p_subroutine_call(V_USER_FCALL);
            else
               head = p_select(p_identifier());
            break;
         }
         // Fall-through
      default:
         if (at_argless_call(NULL))
            head = p_subroutine_call(V_USER_FCALL);
         else
            head = p_select(p_identifier());
         break;
      }
      break;
   case tSTRING:
   case tNUMBER:
   case tUNSNUM:
   case tREAL:
      head = p_primary_literal();
      break;
   case tSIGNED:
   case tUNSIGNED:
      {
         // A signing cast has the effect of $signed or $unsigned
         const bool is_signed = peek() == tSIGNED;
         consume(is_signed ? tSIGNED : tUNSIGNED);
         consume(tTICK);
         consume(tLPAREN);

         head = vlog_new(V_SYS_FCALL);
         vlog_set_ident(head, ident_new(is_signed ? "$signed" : "$unsigned"));
         vlog_set_subkind(head, is_signed ? V_SYSTF_SIGNED : V_SYSTF_UNSIGNED);
         vlog_set_ival(head, time_ival());
         vlog_add_param(head, p_expression());

         consume(tRPAREN);
         vlog_set_loc(head, CURRENT_LOC);
      }
      break;
   case tSYSTASK:
      head = global_clock_call(p_subroutine_call(V_SYS_FCALL));
      break;
   case tLPAREN:
      consume(tLPAREN);
      head = p_logical_implication(p_mintypmax_expression());
      switch (peek()) {
      case tEQ:
      case tPLUSEQ:
      case tMINUSEQ:
      case tTIMESEQ:
      case tDIVEQ:
      case tPERCENTEQ:
      case tAMPEQ:
      case tBAREQ:
      case tCARETEQ:
      case tLSLEQ:
      case tLSREQ:
      case tASLEQ:
      case tASREQ:
         // Assignment as an expression
         head = p_operator_assignment(head);
         break;
      default:
         break;
      }
      consume(tRPAREN);
      break;
   case tTIMELIT:
      {
         // In an expression this is a value in units of the time scale
         uint64_t value, scale;
         unsigned frac;
         p_time_literal(&value, &scale, &frac);

         if (time_unscaled) {
            // The time unit is the precision of the design which is not
            // known until elaboration
            head = vlog_new(V_NUMBER);
            vlog_set_number(head, number_from_int(
                               time_literal_to_fs(value, scale, frac,
                                                  &state.last_loc)));
            vlog_set_flags(head, VLOG_F_TIMELIT | VLOG_F_TIMEEXPR);
            vlog_set_loc(head, CURRENT_LOC);
            break;
         }

         const unsigned __int128 num = (unsigned __int128)value * scale;
         const unsigned __int128 den =
            (unsigned __int128)time_pow10(frac) * time_scale;

         if (num % den == 0 && num / den <= INT64_MAX) {
            head = vlog_new(V_NUMBER);
            vlog_set_number(head, number_from_int((uint64_t)(num / den)));
         }
         else {
            head = vlog_new(V_REAL);
            vlog_set_dval(head, (double)((long double)num / (long double)den));
         }

         vlog_set_loc(head, CURRENT_LOC);
      }
      break;
   case tDOLLAR:
      consume(tDOLLAR);
      head = vlog_new(V_DOLLAR);
      vlog_set_loc(head, CURRENT_LOC);
      break;
   case tTHIS:
   case tSUPER:
      head = p_select(NULL);
      break;
   case tNEW:
      head = p_class_new(NULL);
      parse_error(vlog_loc(head), "new can only be used as the value of an "
                  "assignment or declaration initialiser");
      break;
   case tSTRINGK: case tCHANDLE:
   case tSVREAL:
   case tSVINT:
   case tLONGINT:
   case tSHORTINT:
   case tBYTE:
   case tVOID:
   case tINTEGER:
   case tBIT:
   case tLOGIC:
   case tREG:
   case tTIME:
   case tSHORTREAL:
   case tREALTIME:
      {
         // Simple type cast
         vlog_node_t type = p_data_type();
         vlog_node_t v = vlog_new(V_CAST);
         vlog_set_subkind(v, V_CAST_TYPE);
         vlog_set_type(v, type);
         consume(tTICK);
         consume(tLPAREN);
         vlog_set_value(v, p_expression());
         consume(tRPAREN);
         vlog_set_loc(v, CURRENT_LOC);
         return v;
      }
   case tLBRACE:
      {
         if (peek_nth(2) == tSHIFTLL || peek_nth(2) == tSHIFTRL) {
            head = p_streaming_concatenation();
            break;
         }

         consume(tLBRACE);

         if (optional(tRBRACE)) {
            head = vlog_new(V_CONCAT);   // Empty queue
            vlog_set_loc(head, CURRENT_LOC);
            break;
         }

         vlog_node_t head2 = p_expression();
         if (peek() == tLBRACE)
            head = p_multiple_concatenation(head2);
         else
            head = p_concatenation(head2);
      }
      break;
   case tNULL:
      consume(tNULL);
      head =  vlog_new(V_NULL);
      vlog_set_loc(head, CURRENT_LOC);
      break;
   case tTICK:
      head = p_assignment_pattern_expression(NULL);
      break;
   case tVLOCAL:
      head = p_local_scope_select();
      break;
   default:
      one_of(tID, tSTRING, tNUMBER, tUNSNUM, tREAL, tSYSTASK, tLPAREN,
             tLBRACE, tNULL, tTICK);
      head = p_select(error_marker());
      break;
   }

   head = p_call_result_chain(head);

   if (is_real_view(head))
      head = real_view_call("$bitstoreal", head);

   if (vlog_kind(head) == V_CONCAT && vlog_params(head) > 0
       && peek() == tLSQUARE) {
      // Select from a concatenation
      consume(tLSQUARE);

      vlog_node_t expr = p_expression();
      if (scan(tCOLON, tINDEXPOS, tINDEXNEG)) {
         vlog_node_t ps = vlog_new(V_PART_SELECT);
         vlog_set_left(ps, expr);
         vlog_set_value(ps, head);

         p_part_select_range(ps);

         head = ps;
      }
      else {
         vlog_node_t bs = vlog_new(V_BIT_SELECT);
         vlog_set_value(bs, head);
         vlog_add_param(bs, expr);

         head = bs;
      }

      consume(tRSQUARE);

      vlog_set_loc(head, CURRENT_LOC);
   }

   if (peek() == tTICK && peek_nth(2) == tLPAREN)
      return p_cast(head);
   else if (peek() == tTICK && peek_nth(2) == tLBRACE
            && vlog_kind(head) == V_REF && vlog_has_ref(head)
            && is_data_type(vlog_ref(head))
            && vlog_get_type(vlog_ref(head)) != NULL)
      return p_assignment_pattern_expression(vlog_get_type(vlog_ref(head)));
   else
      return head;
}

static vlog_binary_t p_binary_operator(void)
{
   // + | - | * | / | % | == | != | === | !== | ==? | !=? | && | ||
   //  | ** | < | <= | > | >= | & | | | ^ | ^~ | ~^ | >> | <<
   //  | >>> | <<< | -> | <->

   BEGIN("binary operator");

   switch (one_of(tBAR, tPLUS, tAMP, tCASEEQ, tCASENEQ, tWILDEQ, tWILDNEQ, tLOGOR,
                  tLOGEQ, tLOGNEQ, tDBLAMP, tSHIFTLL, tSHIFTRL,
                  tSHIFTLA, tSHIFTRA, tLT, tGT, tLE, tGE, tMINUS,
                  tTIMES, tOVER, tPERCENT, tPOWER, tCARET, tTILDECARET,
                  tTILDEAMP, tTILDEBAR)) {
   case tBAR:        return V_BINARY_OR;
   case tAMP:        return V_BINARY_AND;
   case tCASEEQ:     return V_BINARY_CASE_EQ;
   case tCASENEQ:    return V_BINARY_CASE_NEQ;
   case tWILDEQ:     return V_BINARY_WILD_EQ;
   case tWILDNEQ:    return V_BINARY_WILD_NEQ;
   case tLOGEQ:      return V_BINARY_LOG_EQ;
   case tLOGNEQ:     return V_BINARY_LOG_NEQ;
   case tLOGOR:      return V_BINARY_LOG_OR;
   case tDBLAMP:     return V_BINARY_LOG_AND;
   case tSHIFTLL:    return V_BINARY_SHIFT_LL;
   case tSHIFTRL:    return V_BINARY_SHIFT_RL;
   case tSHIFTLA:    return V_BINARY_SHIFT_LA;
   case tSHIFTRA:    return V_BINARY_SHIFT_RA;
   case tLT:         return V_BINARY_LT;
   case tGT:         return V_BINARY_GT;
   case tLE:         return V_BINARY_LEQ;
   case tGE:         return V_BINARY_GEQ;
   case tMINUS:      return V_BINARY_MINUS;
   case tTIMES:      return V_BINARY_TIMES;
   case tOVER:       return V_BINARY_DIVIDE;
   case tPERCENT:    return V_BINARY_MOD;
   case tPOWER:      return V_BINARY_EXP;
   case tCARET:      return V_BINARY_XOR;
   case tTILDECARET: return V_BINARY_XNOR;
   case tTILDEAMP:   return V_BINARY_NAND;
   case tTILDEBAR:   return V_BINARY_NOR;
   case tPLUS:
   default:          return V_BINARY_PLUS;
   }
}

static vlog_unary_t p_unary_operator(void)
{
   // + | - | ! | ~ | & | ~& | | | ~| | ^ | ~^ | ^~

   BEGIN("unary operator");

   switch (one_of(tMINUS, tPLUS, tTILDE, tBANG, tAMP, tBAR, tCARET,
                  tTILDEAMP, tTILDEBAR, tTILDECARET)) {
   case tMINUS:      return V_UNARY_NEG;
   case tTILDE:      return V_UNARY_BITNEG;
   case tBANG:       return V_UNARY_NOT;
   case tAMP:        return V_UNARY_AND;
   case tBAR:        return V_UNARY_OR;
   case tCARET:      return V_UNARY_XOR;
   case tTILDEAMP:   return V_UNARY_NAND;
   case tTILDEBAR:   return V_UNARY_NOR;
   case tTILDECARET: return V_UNARY_XNOR;
   case tPLUS:
   default:          return V_UNARY_IDENTITY;
   }
}

static vlog_incdec_t p_inc_or_dec_operator(void)
{
   // ++ | --

   BEGIN("inc or dec operator");

   switch (one_of(tPLUSPLUS, tMINUSMINUS)) {
   case tMINUSMINUS: return V_INCDEC_MINUS;
   case tPLUSPLUS:
   default: return V_INCDEC_PLUS;
   }
}

static vlog_node_t p_inc_or_dec_expression(vlog_node_t head)
{
   // inc_or_dec_operator { attribute_instance } variable_lvalue
   //   | variable_lvalue { attribute_instance } inc_or_dec_operator

   BEGIN_WITH_HEAD("inc or dec expression", head);

   vlog_node_t v = vlog_new(head ? V_POSTFIX : V_PREFIX);
   vlog_set_subkind(v, p_inc_or_dec_operator());
   vlog_set_target(v, head ?: p_variable_lvalue());

   vlog_set_loc(v, CURRENT_LOC);

   if (is_real_view(vlog_target(v))) {
      // The real value held in the bits of a union is updated as a whole
      vlog_node_t one = vlog_new(V_REAL);
      vlog_set_dval(one, 1.0);
      vlog_set_loc(one, vlog_loc(v));

      vlog_node_t bin = vlog_new(V_BINARY);
      vlog_set_subkind(bin, vlog_subkind(v) == V_INCDEC_MINUS
                       ? V_BINARY_MINUS : V_BINARY_PLUS);
      vlog_set_left(bin, real_view_call("$bitstoreal", vlog_target(v)));
      vlog_set_right(bin, one);
      vlog_set_loc(bin, vlog_loc(v));

      vlog_node_t assign = vlog_new(V_BASSIGN);
      vlog_set_target(assign, vlog_target(v));
      vlog_set_value(assign, real_view_call("$realtobits", bin));
      vlog_set_loc(assign, vlog_loc(v));
      return assign;
   }

   return v;
}

static vlog_node_t p_nonbinary_expression(void)
{
   // primary | unary_operator { attribute_instance } primary
   //   | inc_or_dec_expression | ( operator_assignment )
   //   | conditional_expression | inside_expression | tagged_union_expression

   switch (peek()) {
   case tTIMELIT:
   case tDOLLAR:
   case tTHIS:
   case tSUPER:
   case tNEW:
   case tTYPE:
   case tSTRINGK: case tCHANDLE:
   case tSVREAL:
   case tSVINT:
   case tLONGINT:
   case tSHORTINT:
   case tBYTE:
   case tINTEGER:
   case tBIT:
   case tLOGIC:
   case tREG:
   case tTIME:
   case tSHORTREAL:
   case tREALTIME:
   case tID:
   case tSTRING:
   case tNUMBER:
   case tUNSNUM:
   case tREAL:
   case tSYSTASK:
   case tLPAREN:
   case tLBRACE:
   case tNULL:
   case tTICK:
   case tVLOCAL:
   case tSIGNED:
   case tUNSIGNED:
      {
         vlog_node_t v = p_primary();
         if (scan(tPLUSPLUS, tMINUSMINUS))
            return p_inc_or_dec_expression(v);
         return v;
      }
   case tMINUS:
   case tPLUS:
   case tTILDE:
   case tBANG:
   case tAMP:
   case tBAR:
   case tCARET:
   case tTILDEAMP:
   case tTILDEBAR:
   case tTILDECARET:
      {
         vlog_node_t v = vlog_new(V_UNARY);
         vlog_set_subkind(v, p_unary_operator());

         optional_attributes();

         vlog_set_value(v, p_primary());
         vlog_set_loc(v, CURRENT_LOC);
         return v;
      }
   case tPLUSPLUS:
   case tMINUSMINUS:
      return p_inc_or_dec_expression(NULL);
   default:
      one_of(tID, tSTRING, tNUMBER, tUNSNUM, tREAL, tSYSTASK, tLPAREN,
             tLBRACE, tNULL, tMINUS, tTILDE, tBANG, tAMP, tBAR, tCARET,
             tTILDEAMP, tTILDEBAR, tPLUSPLUS, tTILDECARET, tMINUSMINUS);
      return p_select(error_marker());
   }
}

static vlog_node_t p_conditional_expression(vlog_node_t head)
{
   // cond_predicate ? { attribute_instance } expression : expression

   BEGIN_WITH_HEAD("conditional expression", head);

   vlog_node_t v = vlog_new(V_COND_EXPR);
   vlog_set_value(v, head);

   consume(tQUESTION);

   optional_attributes();

   const int depth = match_depth;
   vlog_set_left(v, arm_type ? p_expression_of_type(arm_type)
                 : p_expression());
   match_unwind(depth - (vlog_kind(head) == V_MATCHES));

   consume(tCOLON);

   vlog_set_right(v, arm_type ? p_expression_of_type(arm_type)
                  : p_expression());

   vlog_set_loc(v, CURRENT_LOC);
   return v;
}

static bool peek_binary_operator(int *prec)
{
   // See LRM 1800-2017 section 11.3.2 for operator precedence table

   switch (peek()) {
   case tPOWER:      *prec = 12; return true;
   case tTIMES:
   case tOVER:
   case tPERCENT:    *prec = 11; return true;
   case tPLUS:
   case tMINUS:      *prec = 10; return true;
   case tSHIFTLL:
   case tSHIFTRL:
   case tSHIFTLA:
   case tSHIFTRA:    *prec = 9;  return true;
   case tLT:
   case tGT:
   case tLE:
   case tGE:
   case tINSIDE:     *prec = 8;  return true;
   case tCASEEQ:
   case tCASENEQ:
   case tWILDEQ:
   case tWILDNEQ:
   case tLOGEQ:
   case tLOGNEQ:     *prec = 7;  return true;
   case tTILDEAMP:
   case tAMP:        *prec = 6;  return true;
   case tTILDECARET:
   case tCARET:      *prec = 5;  return true;
   case tTILDEBAR:
   case tBAR:        *prec = 4;  return true;
   case tDBLAMP:     *prec = 3;  return true;
   case tLOGOR:      *prec = 2;  return true;
   case tQUESTION:
   case tMATCHES:    *prec = 1;  return true;
   default:
      return false;
   }
}

static vlog_node_t p_value_range(void)
{
   // [ expression : expression ]

   BEGIN("value range");

   consume(tLSQUARE);

   vlog_node_t v = vlog_new(V_INSIDE_RANGE);
   vlog_set_left(v, p_expression());
   consume(tCOLON);
   vlog_set_right(v, p_expression());
   consume(tRSQUARE);

   vlog_set_loc(v, CURRENT_LOC);
   return v;
}

static vlog_node_t p_inside_expression(vlog_node_t head)
{
   // expression inside { open_range_list }

   BEGIN_WITH_HEAD("inside expression", head);

   consume(tINSIDE);

   vlog_node_t v = vlog_new(V_INSIDE);
   vlog_set_value(v, head);

   consume(tLBRACE);

   do {
      if (peek() == tLSQUARE)
         vlog_add_param(v, p_value_range());
      else
         vlog_add_param(v, p_expression());
   } while (optional(tCOMMA));

   consume(tRBRACE);

   vlog_set_loc(v, CURRENT_LOC);
   return v;
}

static vlog_node_t p_binary_expression(vlog_node_t lhs, int min_prec)
{
   // Precedence climbing method, see
   //    https://en.wikipedia.org/wiki/Operator-precedence_parser

   int prec1;
   while (peek_binary_operator(&prec1) && prec1 >= min_prec) {
      if (peek() == tQUESTION)
         lhs = p_conditional_expression(lhs);
      else if (peek() == tMATCHES)
         lhs = p_matches(lhs);
      else if (peek() == tINSIDE)
         lhs = p_inside_expression(lhs);
      else {
         vlog_node_t v = vlog_new(V_BINARY);
         vlog_set_subkind(v, p_binary_operator());
         vlog_set_left(v, lhs);

         optional_attributes();

         vlog_node_t rhs = p_nonbinary_expression();

         int prec2;
         while (peek_binary_operator(&prec2) && prec2 > prec1)
            rhs = p_binary_expression(rhs, prec1 + (prec2 > prec1));

         vlog_set_right(v, rhs);
         vlog_set_loc(v, CURRENT_LOC);
         lhs = v;
      }
   }

   return lhs;
}

static vlog_node_t p_expression(void)
{
   // primary | unary_operator { attribute_instance } primary
   //   | inc_or_dec_expression | ( operator_assignment )
   //   | expression binary_operator { attribute_instance } expression
   //   | conditional_expression | inside_expression | tagged_union_expression

   BEGIN("expression");

   vlog_node_t head = p_binary_expression(p_nonbinary_expression(), 0);
   if (constraint_item)
      return head;

   return p_logical_implication(head);
}

static void hoist_edge_expression(vlog_node_t event)
{
   // An edge of an expression which is not a signal is an edge of the least
   // significant bit of its value (1800-2017 9.4.2.1): make that the value
   // of a net so the edge can be detected

   vlog_node_t expr = vlog_value(event);
   switch (vlog_kind(expr)) {
   case V_REF:
   case V_HIER_REF:
   case V_BIT_SELECT:
   case V_PART_SELECT:
   case V_MEMBER_REF:
      return;
   default:
      break;
   }

   vlog_node_t d = vlog_new(V_NET_DECL);
   vlog_set_subkind(d, V_NET_WIRE);
   vlog_set_type(d, implicit_type());
   vlog_set_ident(d, ident_uniq("edge"));
   vlog_set_value(d, expr);
   vlog_set_loc(d, vlog_loc(expr));
   APUSH(pending_statics, d);   // Added at the end of the unit

   vlog_node_t ref = vlog_new(V_REF);
   vlog_set_ident(ref, vlog_ident(d));
   vlog_set_ref(ref, d);
   vlog_set_loc(ref, vlog_loc(expr));
   vlog_set_value(event, ref);
}

static void p_event_expression(vlog_node_t ctrl)
{
   // [ edge_identifier ] expression [ iff expression ]
   //   | sequence_instance [ iff expression ]
   //   | event_expression or event_expression
   //   | event_expression , event_expression
   //   | ( event_expression )

   BEGIN("event expression");

   do {
      if (optional(tLPAREN)) {
         p_event_expression(ctrl);
         consume(tRPAREN);
      }
      else {
         vlog_node_t v = vlog_new(V_EVENT);

         if (peek() == tID && sva_is_decl(peek_reference())
             && vlog_subkind(peek_reference()) == V_SVA_SEQUENCE
             && peek_nth(2) != tDOT) {
            vlog_set_subkind(v, V_EVENT_POSEDGE);
            vlog_node_t d = peek_reference();
            consume(tID);
            vlog_set_value(v, p_sva_trigger(d));
            vlog_set_loc(v, CURRENT_LOC);
            vlog_add_param(ctrl, v);
            continue;
         }

         if (optional(tPOSEDGE))
            vlog_set_subkind(v, V_EVENT_POSEDGE);
         else if (optional(tNEGEDGE))
            vlog_set_subkind(v, V_EVENT_NEGEDGE);
         else if (optional(tEDGE))
            vlog_set_subkind(v, V_EVENT_EDGE);
         else
            vlog_set_subkind(v, V_EVENT_LEVEL);

         const bool saved_forward = vlog_symtab_set_forward(symtab, true);
         vlog_set_value(v, p_expression());
         vlog_symtab_set_forward(symtab, saved_forward);

         if (vlog_subkind(v) != V_EVENT_LEVEL && !in_subroutine)
            hoist_edge_expression(v);

         if (peek() == tIFFKW) {
            consume(tIFFKW);
            vlog_set_left(v, p_expression());
         }

         vlog_set_loc(v, CURRENT_LOC);

         vlog_add_param(ctrl, v);
      }
   } while (optional(tOR) || optional(tCOMMA));
}

static vlog_node_t p_dist_item(void)
{
   // value_range [ := expression | :/ expression ]

   BEGIN("dist item");

   vlog_node_t v = vlog_new(V_DIST_ITEM);

   if (optional(tLSQUARE)) {
      vlog_set_left(v, p_expression());
      consume(tCOLON);
      vlog_set_right(v, p_expression());
      consume(tRSQUARE);
   }
   else
      vlog_set_left(v, p_expression());

   vlog_set_subkind(v, V_DIST_PER_VALUE);

   if (optional(tCOLON)) {
      if (one_of(tEQ, tOVER) == tOVER)
         vlog_set_subkind(v, V_DIST_PER_RANGE);

      vlog_set_delay(v, p_expression());
   }
   else {
      vlog_node_t one = vlog_new(V_NUMBER);
      vlog_set_number(one, number_from_int(1));
      vlog_set_loc(one, &state.last_loc);
      vlog_set_delay(v, one);
   }

   vlog_set_loc(v, CURRENT_LOC);
   return v;
}

static vlog_node_t p_expression_or_dist(void)
{
   // expression [ dist { dist_list } ]

   BEGIN("expression or dist");

   vlog_node_t head = p_expression();
   if (peek() != tDIST)
      return head;

   consume(tDIST);

   vlog_node_t v = vlog_new(V_DIST);
   vlog_set_value(v, head);

   consume(tLBRACE);

   do {
      vlog_add_param(v, p_dist_item());
   } while (optional(tCOMMA));

   consume(tRBRACE);

   vlog_set_loc(v, CURRENT_LOC);
   return v;
}

static void p_constraint_set(vlog_node_t parent)
{
   // constraint_expression | { { constraint_expression } }

   BEGIN("constraint set");

   if (optional(tLBRACE)) {
      while (not_at_token(tRBRACE)) {
         if (!optional(tSEMI))   // An empty item
            vlog_add_stmt(parent, p_constraint_expression());
      }

      consume(tRBRACE);
   }
   else
      vlog_add_stmt(parent, p_constraint_expression());
}

static vlog_node_t p_constraint_block(void)
{
   // constraint_set used as a branch of a conditional constraint

   BEGIN("constraint set");

   vlog_node_t v = vlog_new(V_CONS_BLOCK);
   p_constraint_set(v);

   vlog_set_loc(v, CURRENT_LOC);
   return v;
}

static vlog_node_t p_constraint_foreach(void)
{
   // foreach ( ps_or_hierarchical_array_identifier [ loop_variables ] )
   //    constraint_set

   BEGIN("foreach constraint");

   consume(tFOREACH);
   consume(tLPAREN);

   vlog_node_t v = vlog_new(V_CONS_FOREACH);

   // The index variables are visible in the body only
   vlog_symtab_push(symtab, v);

   foreach_array = true;
   vlog_set_value(v, p_select(p_identifier()));
   foreach_array = false;

   consume(tLSQUARE);

   // The index of an associative array has the type of its key
   vlog_node_t key = NULL;
   if ((vlog_kind(vlog_value(v)) == V_REF
        || vlog_kind(vlog_value(v)) == V_MEMBER_REF)
       && vlog_has_ref(vlog_value(v))) {
      vlog_node_t decl = vlog_ref(vlog_value(v));
      switch (vlog_kind(decl)) {
      case V_VAR_DECL:
      case V_LOCAL_DECL:
      case V_TF_PORT_DECL:
         if (vlog_has_type(decl) && vlog_ranges(decl) == 0) {
            vlog_node_t type = vlog_get_type(vlog_type(decl));
            if (type != NULL && vlog_kind(type) == V_DATA_TYPE
                && vlog_subkind(type) == DT_ASSOC)
               key = vlog_assoc_key(type);
         }
         break;
      default:
         break;
      }
   }

   do {
      if (peek() != tID) {
         error_at(&(state.tokenq[state.tokenq_tail].loc), "sorry, skipping "
                  "a loop variable in foreach is not supported");
         continue;
      }

      vlog_node_t d = vlog_new(V_LOCAL_DECL);
      vlog_set_ident(d, p_identifier());
      vlog_set_loc(d, &state.last_loc);

      if (key != NULL && vlog_decls(v) == 0)
         vlog_set_type(d, key);
      else {
         vlog_node_t dt = make_integer_atom_type(DT_INT);
         vlog_set_flags(dt, VLOG_F_SIGNED);
         vlog_set_type(d, dt);
      }

      vlog_add_decl(v, d);
      vlog_symtab_put(symtab, d);
   } while (optional(tCOMMA));

   consume(tRSQUARE);

   consume(tRPAREN);

   p_constraint_set(v);

   vlog_symtab_pop(symtab);

   vlog_set_loc(v, CURRENT_LOC);
   return v;
}

static vlog_node_t p_constraint_expression(void)
{
   // expression_or_dist ; | soft expression_or_dist ;
   //   | uniqueness_constraint ; | expression -> constraint_set
   //   | if ( expression ) constraint_set [ else constraint_set ]
   //   | foreach ( ps_or_hierarchical_array_identifier [ loop_variables ] )
   //       constraint_set
   //   | disable soft constraint_primary ;
   //   | solve solve_before_list before solve_before_list ;

   BEGIN("constraint expression");

   switch (peek()) {
   case tSOFT:
      {
         consume(tSOFT);

         vlog_node_t v = vlog_new(V_CONS_EXPR);
         vlog_set_flags(v, VLOG_F_SOFT);
         vlog_set_value(v, p_expression_or_dist());

         consume(tSEMI);

         vlog_set_loc(v, CURRENT_LOC);
         return v;
      }

   case tUNIQUE:
      {
         consume(tUNIQUE);

         vlog_node_t v = vlog_new(V_CONS_UNIQUE);

         consume(tLBRACE);

         do {
            vlog_add_param(v, p_expression());
         } while (optional(tCOMMA));

         consume(tRBRACE);
         consume(tSEMI);

         vlog_set_loc(v, CURRENT_LOC);
         return v;
      }

   case tSOLVE:
      {
         consume(tSOLVE);

         vlog_node_t v = vlog_new(V_CONS_SOLVE);

         vlog_node_t left = vlog_new(V_CONCAT);
         do {
            vlog_add_param(left, p_expression());
         } while (optional(tCOMMA));

         vlog_set_loc(left, CURRENT_LOC);
         vlog_set_left(v, left);

         consume(tVBEFORE);

         vlog_node_t right = vlog_new(V_CONCAT);
         do {
            vlog_add_param(right, p_expression());
         } while (optional(tCOMMA));

         vlog_set_right(v, right);

         consume(tSEMI);

         vlog_set_loc(right, CURRENT_LOC);
         vlog_set_loc(v, CURRENT_LOC);
         return v;
      }

   case tIF:
      {
         consume(tIF);

         vlog_node_t v = vlog_new(V_CONS_IF);

         consume(tLPAREN);
         vlog_set_value(v, p_expression());
         consume(tRPAREN);

         vlog_set_left(v, p_constraint_block());

         if (optional(tELSE))
            vlog_set_right(v, p_constraint_block());

         vlog_set_loc(v, CURRENT_LOC);
         return v;
      }

   case tFOREACH:
      return p_constraint_foreach();

   case tDISABLE:
      {
         consume(tDISABLE);
         consume(tSOFT);

         vlog_node_t v = vlog_new(V_CONS_EXPR);
         vlog_set_flags(v, VLOG_F_SOFT | VLOG_F_DISABLE);
         vlog_set_value(v, p_expression());

         consume(tSEMI);

         vlog_set_loc(v, CURRENT_LOC);
         return v;
      }

   default:
      {
         // The arrow of an implication constraint is not an operator here
         const bool saved_item = constraint_item;
         constraint_item = true;
         vlog_node_t value = p_expression_or_dist();
         constraint_item = saved_item;

         if (optional(tIFIMPL)) {
            vlog_node_t v = vlog_new(V_CONS_IMPLY);
            vlog_set_value(v, value);
            p_constraint_set(v);

            vlog_set_loc(v, CURRENT_LOC);
            return v;
         }

         vlog_node_t v = vlog_new(V_CONS_EXPR);
         vlog_set_value(v, value);

         if (peek() == tRBRACE)
            warn_at(&state.last_loc, "missing semicolon at the end of a "
                    "constraint expression");
         else if (peek() == tCOMMA) {
            // Not allowed by the grammar but accepted as a separator
            consume(tCOMMA);
            warn_at(&state.last_loc, "comma used instead of a semicolon "
                    "after a constraint expression");
         }
         else
            consume(tSEMI);

         vlog_set_loc(v, CURRENT_LOC);
         return v;
      }
   }
}

static vlog_node_t p_sva_expr(int min_prec);

static vlog_node_t sva_number(int n, const loc_t *loc)
{
   vlog_node_t v = vlog_new(V_NUMBER);
   vlog_set_number(v, number_from_int(n));
   vlog_set_loc(v, loc);
   return v;
}

static vlog_node_t sva_dollar(void)
{
   vlog_node_t v = vlog_new(V_DOLLAR);
   vlog_set_loc(v, &state.last_loc);
   return v;
}

static vlog_node_t sva_new(vlog_sva_op_t op)
{
   vlog_node_t v = vlog_new(V_SVA_OP);
   vlog_set_subkind(v, op);
   return v;
}

static vlog_node_t sva_unary(vlog_sva_op_t op, vlog_node_t arg,
                             const loc_t *loc)
{
   vlog_node_t v = sva_new(op);
   vlog_add_param(v, arg);
   vlog_set_loc(v, loc);
   return v;
}

static void p_cycle_delay_const_range_expression(vlog_node_t v)
{
   // constant_expression : constant_expression | constant_expression : $

   BEGIN("cycle delay constant range expression");

   vlog_set_left(v, p_constant_expression());

   consume(tCOLON);

   if (optional(tDOLLAR))
      vlog_set_right(v, sva_dollar());
   else
      vlog_set_right(v, p_constant_expression());
}

static void p_cycle_delay_range(vlog_node_t v)
{
   // ## constant_primary
   //   | ## [ cycle_delay_const_range_expression ]
   //   | ##[*]
   //   | ##[+]

   BEGIN("cycle delay range");

   consume(tCYCLEDLY);

   if (optional(tTIMESRPT)) {
      vlog_set_left(v, sva_number(0, &state.last_loc));
      vlog_set_right(v, sva_dollar());
   }
   else if (optional(tPLUSRPT)) {
      vlog_set_left(v, sva_number(1, &state.last_loc));
      vlog_set_right(v, sva_dollar());
   }
   else if (optional(tLSQUARE)) {
      switch (peek()) {
      case tTIMESRPT:
      case tTIMES:
         consume(peek());
         vlog_set_left(v, sva_number(0, &state.last_loc));
         vlog_set_right(v, sva_dollar());
         break;
      case tPLUS:
         consume(tPLUS);
         vlog_set_left(v, sva_number(1, &state.last_loc));
         vlog_set_right(v, sva_dollar());
         break;
      default:
         p_cycle_delay_const_range_expression(v);
         break;
      }
      consume(tRSQUARE);
   }
   else {
      vlog_node_t n = p_primary();
      vlog_set_left(v, n);
      vlog_set_right(v, n);
   }
}

static void p_sva_repetition_range(vlog_node_t v, bool allow_dollar)
{
   // const_or_range_expression

   vlog_node_t lo = p_constant_expression();
   vlog_set_left(v, lo);

   if (optional(tCOLON)) {
      if (allow_dollar && optional(tDOLLAR))
         vlog_set_right(v, sva_dollar());
      else
         vlog_set_right(v, p_constant_expression());
   }
   else
      vlog_set_right(v, lo);
}

static vlog_node_t p_boolean_abbrev(vlog_node_t head)
{
   // consecutive_repetition | non_consecutive_repetition | goto_repetition

   BEGIN("boolean abbreviation");

   for (;;) {
      vlog_node_t v;
      switch (peek()) {
      case tTIMESRPT:
         consume(tTIMESRPT);
         v = sva_unary(V_SVA_REPEAT, head, &state.last_loc);
         if (peek() == tRSQUARE) {
            vlog_set_left(v, sva_number(0, &state.last_loc));
            vlog_set_right(v, sva_dollar());
         }
         else
            p_sva_repetition_range(v, true);
         consume(tRSQUARE);
         break;
      case tPLUSRPT:
         consume(tPLUSRPT);
         v = sva_unary(V_SVA_REPEAT, head, &state.last_loc);
         vlog_set_left(v, sva_number(1, &state.last_loc));
         vlog_set_right(v, sva_dollar());
         break;
      case tLSQUARE:
         // Lexed as separate tokens when followed by a bracket or space
         consume(tLSQUARE);
         switch (one_of(tTIMES, tPLUS)) {
         case tTIMES:
            v = sva_unary(V_SVA_REPEAT, head, &state.last_loc);
            if (peek() == tRSQUARE) {
               vlog_set_left(v, sva_number(0, &state.last_loc));
               vlog_set_right(v, sva_dollar());
            }
            else
               p_sva_repetition_range(v, true);
            break;
         default:
            v = sva_unary(V_SVA_REPEAT, head, &state.last_loc);
            vlog_set_left(v, sva_number(1, &state.last_loc));
            vlog_set_right(v, sva_dollar());
            break;
         }
         consume(tRSQUARE);
         break;
      case tGOTORPT:
         consume(tGOTORPT);
         v = sva_unary(V_SVA_GOTO, head, &state.last_loc);
         p_sva_repetition_range(v, false);
         consume(tRSQUARE);
         break;
      case tEQRPT:
         consume(tEQRPT);
         v = sva_unary(V_SVA_NONCONS, head, &state.last_loc);
         p_sva_repetition_range(v, false);
         consume(tRSQUARE);
         break;
      default:
         return head;
      }

      vlog_set_loc(v, CURRENT_LOC);
      head = v;
   }
}

static vlog_node_t p_clocking_event(void)
{
   // @ identifier | @ ( event_expression )

   BEGIN("clocking event");

   consume(tAT);

   vlog_node_t v = vlog_new(V_EVENT_CONTROL);

   switch (peek()) {
   case tID:
      {
         vlog_node_t event = vlog_new(V_EVENT);
         vlog_set_subkind(event, V_EVENT_LEVEL);
         vlog_set_value(event, p_select(p_identifier()));
         vlog_set_loc(event, CURRENT_LOC);
         vlog_add_param(v, event);
      }
      break;
   case tLPAREN:
      consume(tLPAREN);
      p_event_expression(v);
      consume(tRPAREN);
      break;
   default:
      one_of(tID, tLPAREN);
   }

   vlog_set_loc(v, CURRENT_LOC);
   return v;
}

static bool sva_binop(token_t tok, int *prec, bool *right, vlog_sva_op_t *op)
{
   switch (tok) {
   case tSUFFIXOVR:  *prec = 1; *right = true;  *op = V_SVA_IMPL_OVER;   return true;
   case tSUFFIXNON:  *prec = 1; *right = true;  *op = V_SVA_IMPL_NON;    return true;
   case tFOLLOWBY1:  *prec = 1; *right = true;  *op = V_SVA_FOLLOW_OVER; return true;
   case tFOLLOWBY2:  *prec = 1; *right = true;  *op = V_SVA_FOLLOW_NON;  return true;
   case tUNTIL:      *prec = 2; *right = true;  *op = V_SVA_UNTIL;       return true;
   case tSUNTIL:     *prec = 2; *right = true;  *op = V_SVA_S_UNTIL;     return true;
   case tUNTILWITH:  *prec = 2; *right = true;  *op = V_SVA_UNTIL_WITH;  return true;
   case tSUNTILWITH: *prec = 2; *right = true;  *op = V_SVA_S_UNTIL_WITH; return true;
   case tIMPLIES:    *prec = 2; *right = true;  *op = V_SVA_IMPLIES;     return true;
   case tIFFKW:      *prec = 3; *right = true;  *op = V_SVA_IFF;         return true;
   case tOR:         *prec = 4; *right = false; *op = V_SVA_OR;          return true;
   case tAND:        *prec = 5; *right = false; *op = V_SVA_AND;         return true;
   case tINTERSECT:  *prec = 6; *right = false; *op = V_SVA_INTERSECT;   return true;
   case tWITHIN:     *prec = 7; *right = false; *op = V_SVA_WITHIN;      return true;
   case tTHROUGHOUT: *prec = 8; *right = true;  *op = V_SVA_THROUGHOUT;  return true;
   case tCYCLEDLY:   *prec = 9; *right = false; *op = V_SVA_DELAY;       return true;
   default:
      return false;
   }
}

static bool sva_marker(token_t tok)
{
   switch (tok) {
   case tCYCLEDLY: case tSUFFIXOVR: case tSUFFIXNON: case tFOLLOWBY1:
   case tFOLLOWBY2: case tTIMESRPT: case tPLUSRPT: case tGOTORPT: case tEQRPT:
   case tAND: case tOR: case tNOT: case tIF: case tINTERSECT: case tWITHIN:
   case tTHROUGHOUT: case tFIRSTMATCH: case tIFFKW: case tUNTIL: case tSUNTIL:
   case tUNTILWITH: case tSUNTILWITH: case tIMPLIES: case tNEXTTIME:
   case tSNEXTTIME: case tALWAYS: case tSALWAYS: case tEVENTUALLY:
   case tSEVENTUALLY: case tACCEPTON: case tREJECTON: case tSYNCACCEPTON:
   case tSYNCREJECTON: case tAT: case tSTRONG: case tWEAK: case tCASE:
      return true;
   default:
      return false;
   }
}

static bool sva_paren_p(void)
{
   // Look ahead from an opening parenthesis for an operator which
   // cannot occur in a plain expression

   int depth = 1, braces = 0;
   for (int n = 2; n < TOKENQ_SIZE - 2; n++) {
      const token_t tok = peek_nth(n);
      switch (tok) {
      case tEOF:
         return false;
      case tLPAREN:
         depth++;
         break;
      case tRPAREN:
         if (--depth == 0)
            return false;
         break;
      case tLBRACE:
         braces++;
         break;
      case tRBRACE:
         braces--;
         break;
      case tCOMMA:
         if (depth == 1 && braces == 0)
            return true;
         break;
      case tID:
         if (sva_is_decl(vlog_symtab_query(symtab, peek_ident_nth(n))))
            return true;
         break;
      default:
         if (sva_marker(tok))
            return true;
         break;
      }
   }

   return false;
}

typedef struct {
   vlog_node_t  decl;
   vlog_node_t *actuals;
   A(vlog_node_t) orig;
   A(vlog_node_t) clone;
} sva_expand_t;

static bool sva_is_local_decl(vlog_node_t d)
{
   return vlog_kind(d) == V_VAR_DECL && (vlog_flags(d) & VLOG_F_SVA_LOCAL);
}

static bool sva_formal_pred(vlog_node_t v, void *ctx)
{
   sva_expand_t *e = ctx;

   if (vlog_kind(v) != V_REF || !vlog_has_ref(v))
      return false;

   vlog_node_t d = vlog_ref(v);
   const int nports = vlog_ports(e->decl);
   for (int i = 0; i < nports; i++) {
      if (vlog_port(e->decl, i) == d)
         return true;
   }

   return sva_is_local_decl(d);
}

static vlog_node_t sva_local_clone(sva_expand_t *e, vlog_node_t d)
{
   for (unsigned i = 0; i < e->orig.count; i++) {
      if (e->orig.items[i] == d)
         return e->clone.items[i];
   }

   vlog_node_t c = vlog_new(V_VAR_DECL);
   vlog_set_ident(c, vlog_ident(d));
   vlog_set_type(c, vlog_type(d));
   vlog_set_flags(c, vlog_flags(d) | VLOG_F_SVA_LOCAL);
   vlog_set_loc(c, vlog_loc(d));

   APUSH(e->orig, d);
   APUSH(e->clone, c);
   return c;
}

static vlog_node_t sva_formal_rewrite(vlog_node_t v, void *ctx)
{
   sva_expand_t *e = ctx;

   if (!sva_formal_pred(v, ctx))
      return v;

   for (unsigned i = 0; i < e->clone.count; i++) {
      if (e->clone.items[i] == vlog_ref(v))
         return v;
   }

   const int nports = vlog_ports(e->decl);
   for (int i = 0; i < nports; i++) {
      vlog_node_t port = vlog_port(e->decl, i);
      if (port != vlog_ref(v))
         continue;
      else if (!(vlog_flags(port) & VLOG_F_SVA_LOCAL))
         return e->actuals[i];

      vlog_node_t c = sva_local_clone(e, port);
      vlog_node_t ref = vlog_new(V_REF);
      vlog_set_ident(ref, vlog_ident(c));
      vlog_set_ref(ref, c);
      vlog_set_loc(ref, vlog_loc(v));
      return ref;
   }

   vlog_set_ref(v, sva_local_clone(e, vlog_ref(v)));
   return v;
}

static vlog_node_t sva_wrap_init(vlog_node_t decl, vlog_node_t init,
                                 vlog_node_t body, const loc_t *loc)
{
   vlog_node_t v;
   if (vlog_subkind(decl) == V_SVA_PROPERTY) {
      v = sva_new(V_SVA_IMPL_OVER);
      vlog_add_param(v, init);
      vlog_add_param(v, body);
   }
   else {
      v = sva_new(V_SVA_DELAY);
      vlog_set_left(v, sva_number(0, loc));
      vlog_set_right(v, sva_number(0, loc));
      vlog_add_param(v, init);
      vlog_add_param(v, body);
   }

   vlog_set_loc(v, loc);
   return v;
}

static vlog_node_t sva_local_init(vlog_node_t decl, vlog_node_t body,
                                  sva_expand_t *e, const loc_t *loc)
{
   // A local variable with an initial value and a local input formal
   // are assigned at the start of each instance of the sequence

   // The initial values are assigned in the order of declaration so that
   // one may refer to an earlier local variable
   A(vlog_node_t) assigns = AINIT;
   A(vlog_node_t) origs = AINIT;
   for (unsigned i = 0; i < e->orig.count; i++) {
      vlog_node_t d = e->orig.items[i], value = NULL;

      const int nports = vlog_ports(decl);
      for (int j = 0; j < nports; j++) {
         if (vlog_port(decl, j) == d)
            value = e->actuals[j];
      }

      if (value == NULL && vlog_has_value(d)) {
         vlog_node_t copy = vlog_copy(vlog_value(d), sva_formal_pred, e);
         value = vlog_rewrite(copy, NULL, sva_formal_rewrite, e);
      }

      if (value == NULL)
         continue;

      vlog_node_t target = vlog_new(V_REF);
      vlog_set_ident(target, vlog_ident(e->clone.items[i]));
      vlog_set_ref(target, e->clone.items[i]);
      vlog_set_loc(target, loc);

      vlog_node_t assign = vlog_new(V_BASSIGN);
      vlog_set_target(assign, target);
      vlog_set_value(assign, value);
      vlog_set_loc(assign, loc);

      // Insertion sort by position of the declaration
      const loc_t *dl = vlog_loc(d);
      unsigned pos = assigns.count;
      while (pos > 0) {
         const loc_t *pl = vlog_loc(origs.items[pos - 1]);
         if (pl->first_line < dl->first_line
             || (pl->first_line == dl->first_line
                 && pl->first_column <= dl->first_column))
            break;
         pos--;
      }

      APUSH(assigns, assign);
      APUSH(origs, d);
      for (unsigned k = assigns.count - 1; k > pos; k--) {
         assigns.items[k] = assigns.items[k - 1];
         origs.items[k] = origs.items[k - 1];
      }
      assigns.items[pos] = assign;
      origs.items[pos] = d;
   }

   vlog_node_t init = NULL;
   if (assigns.count > 0) {
      init = sva_new(V_SVA_MATCH);
      vlog_add_param(init, sva_number(1, loc));
      vlog_set_loc(init, loc);
      for (unsigned i = 0; i < assigns.count; i++)
         vlog_add_param(init, assigns.items[i]);
   }

   ACLEAR(assigns);
   ACLEAR(origs);

   if (init == NULL)
      return body;

   if (vlog_kind(body) == V_SVA_OP && (vlog_subkind(body) == V_SVA_CLOCKED
                                       || vlog_subkind(body) == V_SVA_DISABLE)) {
      vlog_node_t inner = vlog_param(body, 0);
      vlog_set_param(body, 0, sva_wrap_init(decl, init, inner, loc));
      return body;
   }

   return sva_wrap_init(decl, init, body, loc);
}

static vlog_node_t p_sva_instance(vlog_node_t decl)
{
   // ps_or_hierarchical_sequence_identifier [ ( [ sequence_list_of_arguments ] ) ]
   //   | ps_or_hierarchical_property_identifier [ ( [ property_list_of_arguments ] ) ]

   BEGIN("sequence or property instance");

   const loc_t loc = *CURRENT_LOC;
   consume(tID);

   const int nports = vlog_ports(decl);
   vlog_node_t *actuals LOCAL = xcalloc_array(MAX(nports, 1),
                                              sizeof(vlog_node_t));

   if (optional(tLPAREN)) {
      int pos = 0;
      if (peek() != tRPAREN) {
         do {
            if (optional(tDOT)) {
               ident_t name = p_identifier();
               consume(tLPAREN);
               vlog_node_t actual = NULL;
               if (peek() != tRPAREN)
                  actual = p_sva_expr(1);
               consume(tRPAREN);

               bool found = false;
               for (int i = 0; i < nports; i++) {
                  if (vlog_ident(vlog_port(decl, i)) == name) {
                     actuals[i] = actual;
                     found = true;
                  }
               }

               if (!found)
                  error_at(&state.last_loc, "%pQ is not a formal argument of "
                           "%pQ", name, vlog_ident(decl));
            }
            else {
               vlog_node_t actual = NULL;
               if (peek() != tCOMMA && peek() != tRPAREN)
                  actual = p_sva_expr(1);

               if (pos < nports)
                  actuals[pos] = actual;
               else
                  error_at(&state.last_loc, "too many actual arguments for "
                           "%pQ", vlog_ident(decl));
               pos++;
            }
         } while (optional(tCOMMA));
      }
      consume(tRPAREN);
   }

   for (int i = 0; i < nports; i++) {
      vlog_node_t port = vlog_port(decl, i);
      if (actuals[i] == NULL) {
         if (!vlog_has_value(port)) {
            error_at(&loc, "missing value for argument %pQ", vlog_ident(port));
            return sva_number(0, &loc);
         }

         actuals[i] = vlog_value(port);
      }
   }

   if (!vlog_has_value(decl))
      return sva_number(0, &loc);

   sva_expand_t e = { .decl = decl, .actuals = actuals };

   vlog_node_t copy = vlog_copy(vlog_value(decl), sva_formal_pred, &e);
   copy = vlog_rewrite(copy, NULL, sva_formal_rewrite, &e);
   copy = sva_local_init(decl, copy, &e, &loc);

   ACLEAR(e.orig);
   ACLEAR(e.clone);
   return copy;
}

static bool sva_is_decl(vlog_node_t d)
{
   return d != NULL && vlog_kind(d) == V_SVA_DECL;
}

static vlog_node_t p_sva_case(void)
{
   // case ( expression_or_dist ) property_case_item { property_case_item }
   //   endcase

   BEGIN("property case");

   consume(tCASE);
   consume(tLPAREN);

   vlog_node_t sel = p_expression_or_dist();

   consume(tRPAREN);

   vlog_node_t head = NULL, tail = NULL, deflt = NULL;
   while (not_at_token(tENDCASE)) {
      if (optional(tDEFAULT)) {
         optional(tCOLON);
         deflt = p_sva_expr(1);
         consume(tSEMI);
         continue;
      }

      vlog_node_t cond = NULL;
      do {
         vlog_node_t eq = vlog_new(V_BINARY);
         vlog_set_subkind(eq, V_BINARY_CASE_EQ);
         vlog_set_left(eq, sel);
         vlog_set_right(eq, p_expression());
         vlog_set_loc(eq, &state.last_loc);

         if (cond == NULL)
            cond = eq;
         else {
            vlog_node_t or = vlog_new(V_BINARY);
            vlog_set_subkind(or, V_BINARY_LOG_OR);
            vlog_set_left(or, cond);
            vlog_set_right(or, eq);
            vlog_set_loc(or, &state.last_loc);
            cond = or;
         }
      } while (optional(tCOMMA));

      consume(tCOLON);

      vlog_node_t item = sva_new(V_SVA_IF);
      vlog_set_value(item, cond);
      vlog_add_param(item, p_sva_expr(1));
      consume(tSEMI);

      vlog_set_loc(item, CURRENT_LOC);

      if (head == NULL)
         head = item;
      else
         vlog_add_param(tail, item);

      tail = item;
   }

   consume(tENDCASE);

   if (head == NULL) {
      error_at(&state.last_loc, "property case must have at least one item");
      return sva_number(1, &state.last_loc);
   }

   if (deflt != NULL)
      vlog_add_param(tail, deflt);

   return head;
}

static vlog_node_t p_sva_prefix(vlog_sva_op_t op, bool ranged, bool need_range)
{
   BEGIN("property operator");

   const loc_t loc = *CURRENT_LOC;
   consume(peek());

   vlog_node_t v = sva_new(op);

   if (ranged && optional(tLSQUARE)) {
      p_sva_repetition_range(v, true);
      consume(tRSQUARE);
   }
   else if (need_range)
      one_of(tLSQUARE);

   vlog_add_param(v, p_sva_expr(1));
   vlog_set_loc(v, &loc);
   return v;
}

static vlog_node_t p_sva_match_item(void)
{
   // variable_assignment | inc_or_dec_expression | subroutine_call

   BEGIN("sequence match item");

   if (peek() == tSYSTASK)
      return p_subroutine_call(V_SYS_TCALL);
   else if (peek() == tID && peek_nth(2) == tLPAREN)
      return p_subroutine_call(V_USER_TCALL);
   else if (scan(tPLUSPLUS, tMINUSMINUS))
      return p_inc_or_dec_expression(NULL);

   vlog_node_t lhs = p_variable_lvalue();
   if (scan(tPLUSPLUS, tMINUSMINUS))
      return p_inc_or_dec_expression(lhs);

   return p_blocking_assignment(lhs);
}

static vlog_node_t p_sva_match_items(vlog_node_t seq, const loc_t *loc)
{
   // ( sequence_expr { , sequence_match_item } )

   if (peek() != tCOMMA)
      return seq;

   vlog_node_t v = sva_new(V_SVA_MATCH);
   vlog_add_param(v, seq);

   while (optional(tCOMMA))
      vlog_add_param(v, p_sva_match_item());

   vlog_set_loc(v, loc);
   return v;
}

static vlog_node_t p_sva_unary(void)
{
   BEGIN("sequence or property operand");

   const loc_t loc = *CURRENT_LOC;
   vlog_node_t head;

   switch (peek()) {
   case tAT:
      {
         vlog_node_t ctrl = p_clocking_event();
         if (peek() == tCOMMA || peek() == tRPAREN)
            return ctrl;   // Actual argument for an event formal

         head = sva_unary(V_SVA_CLOCKED, p_sva_expr(1), &loc);
         vlog_set_value(head, ctrl);
         return head;
      }
   case tCYCLEDLY:
      {
         head = sva_new(V_SVA_DELAY);
         p_cycle_delay_range(head);
         vlog_add_param(head, p_sva_expr(10));
         vlog_set_loc(head, CURRENT_LOC);
         return head;
      }
   case tNOT:
      consume(tNOT);
      return sva_unary(V_SVA_NOT, p_sva_expr(1), &loc);
   case tSTRONG:
   case tWEAK:
      {
         const vlog_sva_op_t op = peek() == tSTRONG ? V_SVA_STRONG : V_SVA_WEAK;
         consume(peek());
         consume(tLPAREN);
         head = sva_unary(op, p_sva_expr(1), &loc);
         consume(tRPAREN);
         return head;
      }
   case tFIRSTMATCH:
      consume(tFIRSTMATCH);
      consume(tLPAREN);
      head = sva_unary(V_SVA_FIRST_MATCH,
                       p_sva_match_items(p_sva_expr(1), &loc), &loc);
      consume(tRPAREN);
      return p_boolean_abbrev(head);
   case tIF:
      {
         consume(tIF);
         consume(tLPAREN);
         head = sva_new(V_SVA_IF);
         vlog_set_value(head, p_expression_or_dist());
         consume(tRPAREN);
         vlog_add_param(head, p_sva_expr(1));
         if (optional(tELSE))
            vlog_add_param(head, p_sva_expr(1));
         vlog_set_loc(head, CURRENT_LOC);
         return head;
      }
   case tCASE:
      return p_sva_case();
   case tNEXTTIME:
      return p_sva_prefix(V_SVA_NEXTTIME, true, false);
   case tSNEXTTIME:
      return p_sva_prefix(V_SVA_S_NEXTTIME, true, false);
   case tALWAYS:
      return p_sva_prefix(V_SVA_ALWAYS, true, false);
   case tSALWAYS:
      return p_sva_prefix(V_SVA_S_ALWAYS, true, true);
   case tEVENTUALLY:
      return p_sva_prefix(V_SVA_EVENTUALLY, true, true);
   case tSEVENTUALLY:
      return p_sva_prefix(V_SVA_S_EVENTUALLY, true, false);
   case tACCEPTON:
   case tREJECTON:
   case tSYNCACCEPTON:
   case tSYNCREJECTON:
      {
         vlog_sva_op_t op;
         switch (peek()) {
         case tACCEPTON:     op = V_SVA_ACCEPT_ON; break;
         case tREJECTON:     op = V_SVA_REJECT_ON; break;
         case tSYNCACCEPTON: op = V_SVA_SYNC_ACCEPT_ON; break;
         default:            op = V_SVA_SYNC_REJECT_ON; break;
         }
         consume(peek());
         consume(tLPAREN);
         head = sva_new(op);
         vlog_set_value(head, p_expression_or_dist());
         consume(tRPAREN);
         vlog_add_param(head, p_sva_expr(1));
         vlog_set_loc(head, CURRENT_LOC);
         return head;
      }
   case tLPAREN:
      if (sva_paren_p()) {
         consume(tLPAREN);
         head = p_sva_match_items(p_sva_expr(1), &loc);
         consume(tRPAREN);
         return p_boolean_abbrev(head);
      }
      break;
   case tID:
      {
         vlog_node_t d = peek_reference();
         if (sva_is_decl(d) && peek_nth(2) == tDOT)
            parse_error(&state.last_loc, "sorry, sequence methods are not "
                        "supported");
         else if (sva_is_decl(d)) {
            head = p_sva_instance(d);
            return p_boolean_abbrev(head);
         }
      }
      break;
   default:
      break;
   }

   return p_boolean_abbrev(sva_future_gclk(p_expression_or_dist()));
}

static vlog_node_t p_sva_expr(int min_prec)
{
   BEGIN("sequence or property expression");

   vlog_node_t lhs = p_sva_unary();

   for (;;) {
      const token_t tok = peek();
      int prec;
      bool right;
      vlog_sva_op_t op;
      if (!sva_binop(tok, &prec, &right, &op) || prec < min_prec)
         return lhs;

      vlog_node_t v = sva_new(op);

      if (tok == tCYCLEDLY) {
         vlog_add_param(v, lhs);
         p_cycle_delay_range(v);
      }
      else {
         consume(tok);
         vlog_add_param(v, lhs);
      }

      vlog_add_param(v, p_sva_expr(right ? prec : prec + 1));
      vlog_set_loc(v, CURRENT_LOC);
      lhs = v;
   }
}

static vlog_node_t p_property_expr(void)
{
   BEGIN("property expression");

   return p_sva_expr(1);
}

static vlog_node_t p_cond_predicate(void)
{
   // expression_or_cond_pattern { &&& expression_or_cond_pattern }

   BEGIN("cond predicate");

   vlog_node_t v = p_expression();

   while (optional(tTRPLAMP)) {
      vlog_node_t and = vlog_new(V_BINARY);
      vlog_set_subkind(and, V_BINARY_LOG_AND);
      vlog_set_left(and, v);
      vlog_set_right(and, p_expression());
      vlog_set_loc(and, CURRENT_LOC);
      v = and;
   }

   return v;
}

static vlog_node_t p_event_control(void)
{
   // @ hierarchical_event_identifier | @ ( event_expression )
   //    | @* | @ (*) | @ ps_or_hierarchical_sequence_identifier

   BEGIN("event control");

   consume(tAT);

   vlog_node_t v = vlog_new(V_EVENT_CONTROL);

   switch (one_of(tLPAREN, tTIMES, tPARENSTAR, tID)) {
   case tID:
      {
         vlog_node_t event = vlog_new(V_EVENT);
         vlog_node_t decl = vlog_symtab_query(symtab, state.last_lval.ident);
         if (sva_is_decl(decl) && vlog_subkind(decl) == V_SVA_SEQUENCE
             && peek() != tDOT) {
            vlog_set_subkind(event, V_EVENT_POSEDGE);
            vlog_set_value(event, p_sva_trigger(decl));
         }
         else {
            vlog_set_subkind(event, V_EVENT_LEVEL);
            vlog_set_value(event, p_select(state.last_lval.ident));
         }
         vlog_set_loc(event, CURRENT_LOC);
         vlog_add_param(v, event);
      }
      break;
   case tLPAREN:
      if (peek() == tATTREND)
         consume(tATTREND);   // Lexing ambiguity
      else {
         p_event_expression(v);
         consume(tRPAREN);
      }
      break;
   case tTIMES:
   case tPARENSTAR:
      break;
   }

   vlog_set_loc(v, CURRENT_LOC);
   return v;
}

static vlog_node_t p_delay_value(void)
{
   // unsigned_number | real_number | ps_identifier | time_literal | 1step

   BEGIN("delay value");

   switch (peek()) {
   case tREAL:
      return p_real_number();
   case tID:
      {
         if (peek_nth(2) == tDOT)
            return p_select(p_identifier());   // Member of a class or struct

         vlog_node_t v = vlog_new(V_REF);
         vlog_set_ident(v, p_identifier());
         vlog_set_loc(v, CURRENT_LOC);

         vlog_symtab_lookup(symtab, v);
         return v;
      }
   case tTIMELIT:
      {
         uint64_t value, scale;
         unsigned frac;
         p_time_literal(&value, &scale, &frac);

         vlog_node_t v = vlog_new(V_NUMBER);
         vlog_set_number(v, number_from_int(
                            time_literal_to_fs(value, scale, frac,
                                               &state.last_loc)));
         vlog_set_flags(v, VLOG_F_TIMELIT);
         vlog_set_loc(v, CURRENT_LOC);
         return v;
      }
   case tONESTEP:
      {
         consume(tONESTEP);

         vlog_node_t v = vlog_new(V_NUMBER);
         vlog_set_number(v, number_from_int(1));
         vlog_set_flags(v, VLOG_F_ONESTEP);
         vlog_set_loc(v, CURRENT_LOC);
         return v;
      }
   case tUNSNUM:
      {
         consume(tUNSNUM);

         uint64_t value = 0;
         for (const char *p = state.last_lval.str; *p; p++) {
            if (*p != '_') {
               value *= 10;
               value += (*p - '0');
            }
         }

         free(state.last_lval.str);

         vlog_node_t v = vlog_new(V_NUMBER);
         vlog_set_number(v, number_from_int(value));
         vlog_set_loc(v, CURRENT_LOC);
         return v;
      }
   default:
      return p_unsigned_number();
   }
}

static vlog_node_t p_delay_control(void)
{
   // # delay_value | # ( mintypmax_expression )

   BEGIN("delay control");

   consume(tHASH);

   vlog_node_t v = vlog_new(V_DELAY_CONTROL);

   // Time literals are in femtoseconds but other values are in units of
   // the time scale so this is applied when they are used

   if (peek() != tLPAREN) {
      vlog_node_t value = p_delay_value();

      vlog_set_value(v, value);
   }
   else
      vlog_set_value(v, p_mintypmax_expression());

   vlog_set_ival(v, time_ival());

   vlog_set_loc(v, CURRENT_LOC);
   return v;
}

static vlog_node_t p_delay_list(int max)
{
   // # delay_value | # ( mintypmax_expression { , mintypmax_expression } )

   consume(tHASH);

   vlog_node_t v = vlog_new(V_DELAY_CONTROL);

   if (peek() != tLPAREN)
      vlog_set_value(v, p_delay_value());
   else {
      consume(tLPAREN);

      vlog_set_value(v, p_mintypmax_expression());

      for (int n = 1; n < max && optional(tCOMMA); n++)
         vlog_add_param(v, p_mintypmax_expression());

      consume(tRPAREN);
   }

   vlog_set_ival(v, time_ival());
   vlog_set_loc(v, CURRENT_LOC);
   return v;
}

static vlog_node_t p_delay3(void)
{
   // # delay_value | # ( mintypmax_expression [ , mintypmax_expression
   //   [ , mintypmax_expression ] ] )

   BEGIN("delay3");

   return p_delay_list(3);
}

static vlog_node_t p_delay2(void)
{
   // # delay_value | # ( mintypmax_expression [ , mintypmax_expression ] )

   BEGIN("delay2");

   return p_delay_list(2);
}

static vlog_node_t p_delay_or_event_control(void)
{
   // delay_control | event_control | repeat ( expression ) event_control

   BEGIN("delay or event control");

   switch (peek()) {
   case tAT:
      return p_event_control();
   case tREPEAT:
      {
         consume(tREPEAT);
         consume(tLPAREN);
         vlog_node_t count = p_expression();
         consume(tRPAREN);

         vlog_node_t v = p_event_control();
         vlog_set_value(v, count);
         return v;
      }
   default:
      return p_delay_control();
   }
}

static vlog_node_t p_variable_lvalue(void)
{
   // [ implicit_class_handle . | package_scope ]
   //      hierarchical_variable_identifier select
   //   | { variable_lvalue { , variable_lvalue } }
   //   | [ assignment_pattern_expression_type ]
   //      assignment_pattern_variable_lvalue
   //   | streaming_concatenation

   BEGIN("variable lvalue");

   if (peek() == tLBRACE
       && (peek_nth(2) == tSHIFTLL || peek_nth(2) == tSHIFTRL))
      return p_streaming_concatenation();
   else if (optional(tLBRACE)) {
      vlog_node_t v = vlog_new(V_CONCAT);

      do {
         vlog_add_param(v, p_variable_lvalue());
      } while (optional(tCOMMA));

      consume(tRBRACE);

      vlog_set_loc(v, CURRENT_LOC);
      return v;
   }
   else if (scan(tTHIS, tSUPER))
      return p_select(NULL);
   else {
      ident_t id = p_identifier();
      vlog_node_t v = p_select(id);

      vlog_set_loc(v, CURRENT_LOC);
      return v;
   }
}

static vlog_node_t p_net_lvalue(void)
{
   // ps_or_hierarchical_net_identifier constant_select
   //   | { net_lvalue { , net_lvalue } }
   //   | [ assignment_pattern_expression_type ] assignment_pattern_net_lvalue

   BEGIN("net lvalue");

   if (optional(tLBRACE)) {
      vlog_node_t v = vlog_new(V_CONCAT);

      do {
         vlog_add_param(v, p_net_lvalue());
      } while (optional(tCOMMA));

      consume(tRBRACE);

      vlog_set_loc(v, CURRENT_LOC);
      return v;
   }
   else {
      ident_t id = p_identifier();
      vlog_node_t v = p_constant_select(id);

      vlog_set_loc(v, CURRENT_LOC);
      return v;
   }
}

static vlog_node_t p_class_new(vlog_node_t dt)
{
   // [ class_scope ] new [ ( list_of_arguments ) ]
   //    | new expression

   BEGIN("class new");

   consume(tNEW);

   if (dt == NULL || (vlog_kind(dt) != V_CLASS_DECL
                      && !(vlog_kind(dt) == V_DATA_TYPE
                           && (vlog_subkind(dt) == DT_MAILBOX
                               || vlog_subkind(dt) == DT_SEMAPHORE)))) {
      if (dt != NULL)
         error_at(&state.last_loc, "new class expression must have class "
                  "type");
      dt = NULL;
   }

   vlog_node_t v = vlog_new(V_CLASS_NEW);
   vlog_set_type(v, dt);

   if (dt != NULL && vlog_kind(dt) == V_CLASS_DECL
       && strncmp(istr(vlog_ident(dt)), "__cvp_", 6) == 0) {
      // The covergroup of a class evaluates its expressions in the object
      vlog_node_t this = vlog_new(V_THIS);
      vlog_set_type(this, vlog_symtab_class(symtab));
      vlog_set_loc(this, &state.last_loc);
      vlog_add_param(v, this);
   }

   if (optional(tLPAREN)) {
      p_list_of_arguments(v);
      consume(tRPAREN);
   }
   else if (scan(tID, tTHIS, tSUPER)) {
      // Shallow copy of an existing object
      vlog_set_value(v, p_expression());
   }

   vlog_set_loc(v, CURRENT_LOC);
   return v;
}

static vlog_node_t p_dynamic_array_new(vlog_node_t dt)
{
   // new [ expression ] [ ( expression ) ]

   BEGIN("dynamic array new");

   consume(tNEW);
   consume(tLSQUARE);

   vlog_node_t v = vlog_new(V_DYNAMIC_NEW);
   vlog_set_type(v, dt);
   vlog_set_left(v, p_expression());

   consume(tRSQUARE);

   if (optional(tLPAREN)) {
      // The initial value is an array of the type being created
      vlog_set_value(v, dt != NULL ? p_expression_of_type(dt) : p_expression());
      consume(tRPAREN);
   }

   vlog_set_loc(v, CURRENT_LOC);
   return v;
}

static vlog_assign_t p_assignment_operator(void)
{
   // = | += | -= | *= | /= | %= | &= | |= | ^= | <<= | >>= | <<<= | >>>=

   BEGIN("assignment operator");

   switch (one_of(tEQ, tPLUSEQ, tMINUSEQ, tTIMESEQ, tDIVEQ, tPERCENTEQ, tAMPEQ,
                  tBAREQ, tCARETEQ, tLSLEQ, tLSREQ, tASLEQ, tASREQ)) {
   case tPLUSEQ:    return V_ASSIGN_PLUS;
   case tMINUSEQ:   return V_ASSIGN_MINUS;
   case tTIMESEQ:   return V_ASSIGN_TIMES;
   case tDIVEQ:     return V_ASSIGN_DIVIDE;
   case tPERCENTEQ: return V_ASSIGN_MOD;
   case tAMPEQ:     return V_ASSIGN_AND;
   case tBAREQ:     return V_ASSIGN_OR;
   case tCARETEQ:   return V_ASSIGN_XOR;
   case tLSLEQ:     return V_ASSIGN_SHIFT_LL;
   case tLSREQ:     return V_ASSIGN_SHIFT_RL;
   case tASLEQ:     return V_ASSIGN_SHIFT_LA;
   case tASREQ:     return V_ASSIGN_SHIFT_RA;
   default:         return V_ASSIGN_EQUALS;
   }
}

static vlog_node_t p_operator_assignment(vlog_node_t lhs)
{
   // variable_lvalue assignment_operator expression

   BEGIN_WITH_HEAD("operator assignment", lhs);

   vlog_node_t v = vlog_new(V_OP_ASSIGN);
   vlog_set_subkind(v, p_assignment_operator());
   vlog_set_target(v, lhs);

   vlog_set_value(v, p_expression());

   vlog_set_loc(v, CURRENT_LOC);

   if (is_real_view(lhs)) {
      // Arithmetic is done on the real value held in the bits
      vlog_binary_t op;
      switch (vlog_subkind(v)) {
      case V_ASSIGN_PLUS:   op = V_BINARY_PLUS; break;
      case V_ASSIGN_MINUS:  op = V_BINARY_MINUS; break;
      case V_ASSIGN_TIMES:  op = V_BINARY_TIMES; break;
      case V_ASSIGN_DIVIDE: op = V_BINARY_DIVIDE; break;
      default:
         return v;
      }

      vlog_node_t bin = vlog_new(V_BINARY);
      vlog_set_subkind(bin, op);
      vlog_set_left(bin, real_view_call("$bitstoreal", lhs));
      vlog_set_right(bin, vlog_value(v));
      vlog_set_loc(bin, vlog_loc(v));

      vlog_node_t assign = vlog_new(V_BASSIGN);
      vlog_set_target(assign, lhs);
      vlog_set_value(assign, real_view_call("$realtobits", bin));
      vlog_set_loc(assign, vlog_loc(v));
      return assign;
   }

   return v;
}

static vlog_node_t p_blocking_assignment(vlog_node_t lhs)
{
   // variable_lvalue = delay_or_event_control expression
   //   | nonrange_variable_lvalue = dynamic_array_new
   //   | [ implicit_class_handle . | class_scope | package_scope ]
   //         hierarchical_variable_identifier select = class_new
   //   | operator_assignment

   BEGIN_WITH_HEAD("blocking assignment", lhs);

   check_user_net_procedural(lhs);

   switch (peek()) {
   case tPLUSEQ:
   case tMINUSEQ:
   case tTIMESEQ:
   case tDIVEQ:
   case tPERCENTEQ:
   case tAMPEQ:
   case tBAREQ:
   case tCARETEQ:
   case tLSLEQ:
   case tLSREQ:
   case tASLEQ:
   case tASREQ:
      return p_operator_assignment(lhs);
   }

   consume(tEQ);

   vlog_node_t v = vlog_new(V_BASSIGN);
   vlog_set_target(v, lhs);

   switch (peek()) {
   case tNEW:
      if (peek_nth(2) == tLSQUARE)
         vlog_set_value(v, p_dynamic_array_new(pattern_type_of(lhs)));
      else
         vlog_set_value(v, p_class_new(vlog_get_type(lhs)));
      break;
   case tTICK:
   case tTAGGED:
      vlog_set_value(v, p_expression_of_type(pattern_type_of(lhs)));
      break;
   case tHASH:
   case tAT:
   case tREPEAT:
      vlog_set_delay(v, p_delay_or_event_control());
      // Fall-through
   default:
      if (is_real_view(lhs))
         vlog_set_value(v, real_view_call("$realtobits", p_expression()));
      else
         vlog_set_value(v, p_expression_of_type(pattern_type_of(lhs)));
      break;
   }

   vlog_set_loc(v, CURRENT_LOC);
   return v;
}

static vlog_node_t clocking_ref(vlog_node_t decl, const loc_t *loc);
static vlog_node_t clocking_assign(vlog_node_t target, vlog_node_t value,
                                   vlog_node_t delay, bool nba,
                                   const loc_t *loc);

static vlog_node_t clocking_event_of(vlog_node_t target)
{
   // The event of the clocking block which a clockvar belongs to

   const char *name = NULL;
   switch (vlog_kind(target)) {
   case V_REF:
   case V_MEMBER_REF:
   case V_HIER_REF:
      name = istr(vlog_ident(target));
      break;
   default:
      return NULL;
   }

   const char *dollar = strchr(name, '$');
   if (dollar == NULL || dollar == name)
      return NULL;

   ident_t evname = ident_sprintf("%.*s$ev", (int)(dollar - name), name);

   vlog_node_t ref;
   switch (vlog_kind(target)) {
   case V_REF:
      ref = vlog_new(V_REF);
      vlog_set_ident(ref, evname);
      vlog_set_loc(ref, vlog_loc(target));
      vlog_symtab_lookup(symtab, ref);
      break;
   case V_MEMBER_REF:
      ref = vlog_new(V_MEMBER_REF);
      vlog_set_ident(ref, evname);
      vlog_set_value(ref, vlog_copy(vlog_value(target), vlog_always_true_cb,
                                    NULL));
      vlog_set_loc(ref, vlog_loc(target));
      vlog_symtab_lookup(symtab, ref);
      break;
   default:
      // A new reference as the copy would keep the declaration of the
      // clockvar and so refer to it instead of the event
      ref = vlog_new(V_HIER_REF);
      vlog_set_ident(ref, evname);
      if (vlog_has_value(target))
         vlog_set_value(ref, vlog_copy(vlog_value(target),
                                       vlog_always_true_cb, NULL));
      vlog_set_loc(ref, vlog_loc(target));
      break;
   }

   vlog_node_t ev = vlog_new(V_EVENT);
   vlog_set_subkind(ev, V_EVENT_LEVEL);
   vlog_set_value(ev, ref);
   vlog_set_loc(ev, vlog_loc(target));

   vlog_node_t ctrl = vlog_new(V_EVENT_CONTROL);
   vlog_add_param(ctrl, ev);
   vlog_set_loc(ctrl, vlog_loc(target));
   return ctrl;
}

static vlog_node_t p_clocking_drive(vlog_node_t lhs)
{
   // clockvar_expression <= cycle_delay expression
   //
   // The value is evaluated now and drives the clockvar when the clocking
   // event has occurred the number of times given

   consume(tCYCLEDLY);

   vlog_node_t count;
   if (optional(tLPAREN)) {
      count = p_expression();
      consume(tRPAREN);
   }
   else
      count = p_delay_value();

   vlog_node_t rhs = p_expression_of_type(pattern_type_of(lhs));

   const loc_t *loc = CURRENT_LOC;

   vlog_node_t ctrl = clocking_event_of(lhs);
   if (ctrl == NULL) {
      parse_error(loc, "a cycle delay in an assignment is only allowed when "
                  "driving a clocking block signal");
      ctrl = vlog_new(V_EVENT_CONTROL);
   }

   vlog_node_t blk = vlog_new(V_SEQ_BLOCK);
   vlog_set_loc(blk, loc);

   // Variables a forked process refers to are copied when it starts but
   // a signal outside the unit is not so the value is saved in a local
   vlog_node_t value = rhs;
   vlog_node_t decl = NULL;
   if (vlog_kind(lhs) == V_REF && vlog_has_ref(lhs))
      decl = vlog_ref(lhs);
   else if (vlog_kind(lhs) == V_MEMBER_REF && vlog_has_ref(lhs))
      decl = vlog_member_decl(lhs);
   else if (vlog_kind(lhs) == V_MEMBER_REF && vlog_virtual_pending(lhs)
            && vlog_virtual_iface(vlog_get_type(vlog_value(lhs))) != NULL)
      decl = vlog_virtual_decl(lhs);
   if (decl != NULL && vlog_kind(decl) == V_VAR_DECL) {
      static unsigned count_tmp = 0;

      vlog_node_t tmp = vlog_new(V_LOCAL_DECL);
      vlog_set_ident(tmp, ident_sprintf("$cdv%u", count_tmp++));
      vlog_set_type(tmp, vlog_type(decl));
      vlog_set_loc(tmp, loc);
      const int nranges = vlog_ranges(decl);
      for (int i = 0; i < nranges; i++)
         vlog_add_range(tmp, vlog_range(decl, i));
      vlog_add_decl(blk, tmp);

      vlog_node_t set = vlog_new(V_BASSIGN);
      vlog_set_target(set, clocking_ref(tmp, loc));
      vlog_set_value(set, rhs);
      vlog_set_loc(set, loc);
      vlog_add_stmt(blk, set);

      value = clocking_ref(tmp, loc);
   }

   vlog_node_t wait = vlog_new(V_TIMING);
   vlog_set_value(wait, ctrl);
   vlog_set_loc(wait, loc);

   vlog_node_t rpt = vlog_new(V_REPEAT);
   vlog_set_value(rpt, count);
   vlog_add_stmt(rpt, wait);
   vlog_set_loc(rpt, loc);

   vlog_node_t body = vlog_new(V_SEQ_BLOCK);
   vlog_set_loc(body, loc);
   vlog_add_stmt(body, rpt);
   vlog_add_stmt(body, clocking_assign(lhs, value, NULL, true, loc));

   vlog_node_t fork = vlog_new(V_FORK);
   vlog_set_subkind(fork, V_FORK_JOIN_NONE);
   vlog_set_loc(fork, loc);
   vlog_add_stmt(fork, body);
   vlog_add_stmt(blk, fork);

   return blk;
}

static vlog_node_t p_nonblocking_assignment(vlog_node_t lhs)
{
   // variable_lvalue <= [ delay_or_event_control ] expression

   BEGIN_WITH_HEAD("non-blocking assignment", lhs);

   check_user_net_procedural(lhs);

   consume(tLE);

   if (peek() == tCYCLEDLY) {
      vlog_node_t drive = p_clocking_drive(lhs);
      return drive;
   }

   vlog_node_t v = vlog_new(V_NBASSIGN);
   vlog_set_target(v, lhs);

   if (scan(tHASH, tAT, tREPEAT))
      vlog_set_delay(v, p_delay_or_event_control());

   if (is_real_view(lhs))
      vlog_set_value(v, real_view_call("$realtobits", p_expression()));
   else
      vlog_set_value(v, p_expression_of_type(pattern_type_of(lhs)));

   vlog_set_loc(v, CURRENT_LOC);
   return v;
}

static vlog_node_t p_procedural_timing_control(void)
{
   // delay_control | event_control | cycle_delay

   BEGIN("procedural timing control");

   switch (peek()) {
   case tAT:
      return p_event_control();
   case tHASH:
      return p_delay_control();
   default:
      should_not_reach_here();
   }
}

static vlog_node_t p_procedural_timing_control_statement(void)
{
   // procedural_timing_control statement_or_null

   BEGIN("procedural timing control statement");

   vlog_node_t v = vlog_new(V_TIMING);
   vlog_set_value(v, p_procedural_timing_control());

   vlog_node_t s = p_statement_or_null();
   if (s != NULL)
      vlog_add_stmt(v, s);

   vlog_set_loc(v, CURRENT_LOC);
   return v;
}

static vlog_node_t p_seq_block(ident_t id)
{
   // begin [ : block_identifier ] { block_item_declaration }
   //   { statement_or_null } end [ : block_identifier ]

   BEGIN("sequential block");

   consume(tBEGIN);

   vlog_node_t v = vlog_new(V_SEQ_BLOCK);
   vlog_set_ident(v, id);
   vlog_set_loc(v, CURRENT_LOC);

   vlog_symtab_push(symtab, v);

   if (optional(tCOLON)) {
      ident_t name = p_identifier();
      if (vlog_has_ident(v))
         parse_error(&state.last_loc, "cannot specify both a statement label "
                     "and a block name");
      else
         vlog_set_ident(v, name);
   }

   skip_over_attributes();

   if (vlog_has_ident(v)) {
      vlog_symtab_put(symtab, v);
      note_block_name(vlog_ident(v));
   }

   while (scan_block_item_declaration()) {
      p_block_item_declaration(v);
      skip_over_attributes();
   }

   while (not_at_token(tEND)) {
      vlog_node_t s = p_statement_or_null();
      if (s != NULL)
         vlog_add_stmt(v, s);
   }

   vlog_symtab_pop(symtab);

   consume(tEND);

   if (optional(tCOLON)) {
      ident_t name = p_identifier();
      if (!vlog_has_ident(v))
         parse_error(&state.last_loc, "initial block does not have a label");
      else if (name != vlog_ident(v))
         parse_error(&state.last_loc, "'%s' does not match label '%s'",
                     istr(name), istr(vlog_ident(v)));
   }

   return v;
}

static vlog_node_t p_par_block(ident_t id)
{
   // fork [ : block_identifier ] { block_item_declaration }
   //   { statement_or_null } join_keyword [ : block_identifier ]

   BEGIN("parallel block");

   consume(tFORK);

   vlog_node_t v = vlog_new(V_FORK);
   vlog_set_ident(v, id);
   vlog_set_loc(v, CURRENT_LOC);

   if (optional(tCOLON)) {
      ident_t name = p_identifier();
      if (vlog_has_ident(v))
         parse_error(&state.last_loc, "cannot specify both a statement label "
                     "and a block name");
      else
         vlog_set_ident(v, name);
   }

   vlog_symtab_push(symtab, v);

   skip_over_attributes();

   if (vlog_has_ident(v)) {
      vlog_symtab_put(symtab, v);
      note_block_name(vlog_ident(v));
   }

   while (scan_block_item_declaration()) {
      p_block_item_declaration(v);
      skip_over_attributes();
   }

   while (not_at_token(tJOIN, tJOINNONE, tJOINANY)) {
      vlog_node_t s = p_statement_or_null();
      if (s != NULL)
         vlog_add_stmt(v, s);
   }

   vlog_symtab_pop(symtab);

   switch (one_of(tJOIN, tJOINNONE, tJOINANY)) {
   case tJOINNONE: vlog_set_subkind(v, V_FORK_JOIN_NONE); break;
   case tJOINANY:  vlog_set_subkind(v, V_FORK_JOIN_ANY); break;
   default:        vlog_set_subkind(v, V_FORK_JOIN); break;
   }

   if (optional(tCOLON)) {
      ident_t name = p_identifier();
      if (!vlog_has_ident(v))
         parse_error(&state.last_loc, "fork block does not have a label");
      else if (name != vlog_ident(v))
         parse_error(&state.last_loc, "'%s' does not match label '%s'",
                     istr(name), istr(vlog_ident(v)));
   }

   return v;
}

static bool is_call_result(vlog_node_t head)
{
   while (vlog_kind(head) == V_MEMBER_REF || vlog_kind(head) == V_BIT_SELECT
          || vlog_kind(head) == V_PART_SELECT)
      head = vlog_value(head);

   return vlog_kind(head) == V_USER_FCALL || vlog_kind(head) == V_METHOD_CALL;
}

static void call_select_warning(vlog_node_t call, const char *what)
{
   // The primary production (A.8.4) has no select on a function call

   diag_t *d = diag_new(DIAG_WARN, vlog_loc(call));
   diag_printf(d, "%s of the result of a function call is not allowed by "
               "IEEE 1800-2017", what);
   diag_hint(d, vlog_loc(call), "assign the result to a variable first");
   diag_emit(d);
}

static vlog_node_t p_call_result_chain(vlog_node_t head)
{
   // { . method_call_body | . member_identifier | [ expression ] } applied
   // to the value returned by a function

   while ((vlog_kind(head) == V_USER_FCALL
           || vlog_kind(head) == V_METHOD_CALL
           || vlog_kind(head) == V_MEMBER_REF
           || vlog_kind(head) == V_BIT_SELECT)
          && ((peek() == tDOT && peek_nth(2) == tID)
              || (peek() == tLSQUARE && is_call_result(head)))) {
      const bool is_call = vlog_kind(head) == V_USER_FCALL
         || vlog_kind(head) == V_METHOD_CALL;

      if (peek() == tLSQUARE) {
         if (is_call)
            call_select_warning(head, "a select");

         consume(tLSQUARE);

         vlog_node_t expr = p_expression();
         if (scan(tCOLON, tINDEXPOS, tINDEXNEG)) {
            vlog_node_t ps = vlog_new(V_PART_SELECT);
            vlog_set_left(ps, expr);
            vlog_set_value(ps, head);

            p_part_select_range(ps);

            consume(tRSQUARE);
            vlog_set_loc(ps, CURRENT_LOC);
            head = ps;
            continue;
         }

         vlog_node_t bs = vlog_new(V_BIT_SELECT);
         vlog_set_value(bs, head);
         vlog_add_param(bs, expr);

         consume(tRSQUARE);

         vlog_set_loc(bs, CURRENT_LOC);
         head = bs;
         continue;
      }

      consume(tDOT);

      vlog_node_t type = vlog_get_type(head);
      if (vlog_kind(head) == V_BIT_SELECT)
         type = vlog_select_type(head);

      const bool data = type != NULL && (vlog_kind(type) == V_CLASS_DECL
                                         || vlog_kind(type) == V_STRUCT_DECL
                                         || vlog_kind(type) == V_UNION_DECL);
      if (peek_nth(2) == tLPAREN || !data)
         head = p_method_call_tail(head);
      else {
         if (is_call && vlog_kind(type) != V_CLASS_DECL)
            call_select_warning(head, "a member select");

         vlog_node_t ref = vlog_new(V_MEMBER_REF);
         vlog_set_ident(ref, p_identifier());
         vlog_set_value(ref, head);
         vlog_set_loc(ref, CURRENT_LOC);

         vlog_symtab_lookup(symtab, ref);
         head = ref;
      }
   }

   return head;
}

static bool is_real_view(vlog_node_t v)
{
   return vlog_kind(v) == V_MEMBER_REF && vlog_has_ref(v)
      && vlog_kind(vlog_ref(v)) == V_VAR_DECL
      && (vlog_flags(vlog_ref(v)) & VLOG_F_REALVIEW);
}

static vlog_node_t real_view_call(const char *name, vlog_node_t arg)
{
   // A real member of a union is converted to and from its bits

   vlog_node_t call = vlog_new(V_SYS_FCALL);
   vlog_set_ident(call, ident_new(name));
   vlog_set_subkind(call, V_SYSTF_NONE);
   vlog_set_ival(call, 0);
   vlog_set_loc(call, vlog_loc(arg));
   vlog_add_param(call, arg);
   return call;
}

static bool call_is_chained(void)
{
   // A call with arguments directly followed by a method call

   if (peek() != tID || peek_nth(2) != tLPAREN)
      return false;

   int depth = 0;
   for (int n = 2; n < TOKENQ_SIZE - 8; n++) {
      switch (peek_nth(n)) {
      case tLPAREN:
         depth++;
         break;
      case tRPAREN:
         if (--depth == 0)
            return peek_nth(n + 1) == tDOT && peek_nth(n + 2) == tID;
         break;
      case tEOF:
      case tSEMI:
         return false;
      default:
         break;
      }
   }

   return false;
}

static vlog_node_t p_subroutine_call_statement(void)
{
   // subroutine_call ; | void ' ( function_subroutine_call ) ;

   BEGIN("subroutine call statement");

   vlog_node_t v;
   switch (peek()) {
   case tSYSTASK:
      v = p_subroutine_call(V_SYS_TCALL);
      break;
   case tVOID:
      {
         consume(tVOID);
         consume(tTICK);
         consume(tLPAREN);

         const vlog_kind_t kind =
            peek() == tSYSTASK ? V_SYS_FCALL : V_USER_FCALL;

         v = vlog_new(V_VOID_CALL);
         if (peek() == tID && (peek_nth(2) == tSCOPE || is_specialised_scope())
             && !is_std_randomize())
            qualify_scope = p_scope_prefix();

         if (peek() == tTHIS || peek() == tSUPER)
            vlog_set_value(v, p_select(NULL));
         else if (peek() == tID
                  && (peek_nth(2) == tDOT || peek_nth(2) == tLSQUARE))
            vlog_set_value(v, p_select(p_identifier()));
         else if (call_is_chained())
            vlog_set_value(v, p_call_result_chain(p_subroutine_call(kind)));
         else
            vlog_set_value(v, p_subroutine_call(kind));

         consume(tRPAREN);
      }
      break;
   case tID:
   default:
      if (call_is_chained()) {
         // A method of the value returned by a function
         v = p_call_result_chain(p_subroutine_call(V_USER_FCALL));
      }
      else
         v = p_subroutine_call(V_USER_TCALL);
      break;
   }

   consume(tSEMI);

   vlog_set_loc(v, CURRENT_LOC);
   return v;
}

static vlog_node_t p_conditional_statement(void)
{
   // [ unique_priority ] if ( cond_predicate ) statement_or_null
   //     { else if ( cond_predicate ) statement_or_null }
   //     [ else statement_or_null ]

   BEGIN("conditional statement");

   vlog_node_t v = vlog_new(V_IF);

   consume(tIF);
   consume(tLPAREN);

   const int depth = match_depth;

   vlog_node_t c0 = vlog_new(V_COND);
   vlog_set_value(c0, p_cond_predicate());
   vlog_add_cond(v, c0);

   consume(tRPAREN);

   vlog_node_t s0 = p_statement_or_null();
   if (s0 != NULL)
      vlog_add_stmt(c0, s0);

   match_unwind(depth);

   bool stop = false;
   while (!stop && optional(tELSE)) {
      vlog_node_t c = vlog_new(V_COND);
      vlog_add_cond(v, c);

      if (optional(tIF)) {
         consume(tLPAREN);
         vlog_set_value(c, p_cond_predicate());
         consume(tRPAREN);
      }
      else
         stop = true;

      vlog_node_t s = p_statement_or_null();
      if (s != NULL)
         vlog_add_stmt(c, s);

      match_unwind(depth);
   }

   vlog_set_loc(v, CURRENT_LOC);
   return v;
}

static vlog_node_t p_variable_assignment(vlog_kind_t kind)
{
   // variable_lvalue = expression

   BEGIN("variable assignment");

   vlog_node_t lhs = p_variable_lvalue();

   vlog_node_t v = vlog_new(kind);
   vlog_set_target(v, lhs);

   consume(tEQ);

   vlog_node_t rhs;
   if (is_real_view(lhs))
      rhs = real_view_call("$realtobits", p_expression());
   else
      rhs = p_expression_of_type(pattern_type_of(lhs));
   vlog_set_value(v, rhs);

   vlog_set_loc(v, CURRENT_LOC);
   return v;
}

static void p_list_of_variable_assignments(vlog_node_t parent)
{
   // variable_assignment { , variable_assignment }

   BEGIN("list of variable assignments");

   do {
      vlog_node_t v = p_variable_assignment(V_BASSIGN);
      vlog_add_stmt(parent, v);
   } while (optional(tCOMMA));
}

static void p_for_variable_declaration(vlog_node_t parent)
{
   // [ var ] data_type variable_identifier = expression
   //   { , variable_identifier = expression }

   BEGIN("for variable declaration");

   optional(tVAR);

   vlog_node_t dt = p_data_type();

   do {
      vlog_node_t v = vlog_new(V_LOCAL_DECL);
      vlog_set_ident(v, p_identifier());
      vlog_set_type(v, dt);

      consume(tEQ);

      // The variable is visible in its own initial value
      vlog_set_loc(v, CURRENT_LOC);
      vlog_symtab_put(symtab, v);

      vlog_set_value(v, p_expression());

      vlog_set_loc(v, CURRENT_LOC);
      vlog_add_decl(parent, v);

      // Another name of the same type or the start of the next declaration
      if (peek() == tCOMMA && peek_nth(2) == tID && peek_nth(3) == tEQ)
         consume(tCOMMA);
      else
         break;
   } while (true);
}

static vlog_node_t p_for_initialization(void)
{
   // list_of_variable_assignments
   //   | for_variable_declaration { , for_variable_declaration }

   BEGIN("for initialization");

   vlog_node_t v = vlog_new(V_FOR_INIT);

   if (scan(tREG, tSTRUCT, tUNION, tENUM, tSVINT, tINTEGER, tSVREAL,
            tSHORTREAL, tREALTIME, tLOGIC, tVAR, tBIT, tBYTE, tSHORTINT,
            tLONGINT, tTIME, tSTRINGK)
       || (peek() == tID && (peek_nth(2) == tID || peek_nth(2) == tHASH))) {
      do {
         p_for_variable_declaration(v);
      } while (optional(tCOMMA));
   }
   else
      p_list_of_variable_assignments(v);

   vlog_set_loc(v, CURRENT_LOC);
   return v;
}

static vlog_node_t p_for_step(void)
{
   // for_step_assignment { , for_step_assignment }

   BEGIN("for step");

   vlog_node_t v = vlog_new(V_FOR_STEP);

   do {
      switch (peek()) {
      case tPLUSPLUS:
      case tMINUSMINUS:
         vlog_add_stmt(v, p_inc_or_dec_expression(NULL));
         break;
      default:
         {
            vlog_node_t head = p_variable_lvalue();

            switch (peek()) {
            case tPLUSPLUS:
            case tMINUSMINUS:
               vlog_add_stmt(v, p_inc_or_dec_expression(head));
               break;
            default:
               vlog_add_stmt(v, p_operator_assignment(head));
               break;
            }
         }
         break;
      }
   } while (optional(tCOMMA));

   vlog_set_loc(v, CURRENT_LOC);
   return v;
}

#define MAX_FOREACH_DIMS 16

typedef struct {
   bool         dynamic;
   bool         string;
   vlog_node_t  assoc;
   vlog_node_t  left;
   vlog_node_t  right;
} foreach_dim_t;

static int foreach_dimensions(vlog_node_t decl, foreach_dim_t *dims)
{
   // Fixed unpacked dimensions, then dynamic ones, then packed ones

   int n = 0;

   // Unpacked dimensions, leftmost first
   const int nranges = vlog_kind(decl) == V_FUNC_DECL ? 0 : vlog_ranges(decl);
   for (int i = 0; i < nranges && n < MAX_FOREACH_DIMS; i++) {
      vlog_node_t r = vlog_range(decl, i);
      dims[n++] = (foreach_dim_t) {
         .dynamic = vlog_subkind(r) == V_DIM_UNSIZED,
         .left    = vlog_left(r),
         .right   = vlog_right(r),
      };
   }

   vlog_node_t t = vlog_get_type(vlog_type(decl));
   while (t != NULL && vlog_kind(t) == V_DATA_TYPE && n < MAX_FOREACH_DIMS
          && (vlog_subkind(t) == DT_DYNARRAY || vlog_subkind(t) == DT_QUEUE
              || vlog_subkind(t) == DT_ASSOC)) {
      if (vlog_subkind(t) == DT_DYNARRAY && vlog_ranges(t) == 1
          && vlog_subkind(vlog_range(t, 0)) != V_DIM_UNSIZED) {
         vlog_node_t r = vlog_range(t, 0);
         dims[n++] = (foreach_dim_t) {
            .left  = vlog_left(r),
            .right = vlog_right(r),
         };
      }
      else
         dims[n++] = (foreach_dim_t) {
            .dynamic = true,
            .assoc   = vlog_subkind(t) == DT_ASSOC ? t : NULL,
         };
      t = vlog_get_type(vlog_type(t));
   }

   if (t != NULL && vlog_kind(t) == V_DATA_TYPE) {
      if (vlog_subkind(t) == DT_STRING)
         dims[n++] = (foreach_dim_t) { .string = true };
      else {
         const int npacked = vlog_ranges(t);
         for (int i = 0; i < npacked && n < MAX_FOREACH_DIMS; i++) {
            vlog_node_t r = vlog_range(t, i);
            dims[n++] = (foreach_dim_t) {
               .left  = vlog_left(r),
               .right = vlog_right(r),
            };
         }
      }
   }

   return n;
}

static vlog_node_t foreach_number(int64_t value, const loc_t *loc)
{
   vlog_node_t v = vlog_new(V_NUMBER);
   vlog_set_number(v, number_from_int(value));
   vlog_set_loc(v, loc);
   return v;
}

static vlog_node_t foreach_ref(vlog_node_t decl, const loc_t *loc)
{
   vlog_node_t v = vlog_new(V_REF);
   vlog_set_ident(v, vlog_ident(decl));
   vlog_set_ref(v, decl);
   vlog_set_loc(v, loc);
   return v;
}

static vlog_node_t foreach_binary(vlog_binary_t op, vlog_node_t left,
                                  vlog_node_t right, const loc_t *loc)
{
   vlog_node_t v = vlog_new(V_BINARY);
   vlog_set_subkind(v, op);
   vlog_set_left(v, left);
   vlog_set_right(v, right);
   vlog_set_loc(v, loc);
   return v;
}

static vlog_node_t p_foreach_statement(void)
{
   // foreach ( ps_or_hierarchical_array_identifier [ loop_variables ] )
   //    statement

   // This is expanded to nested for loops with one automatic variable for
   // each named dimension

   BEGIN("foreach statement");

   consume(tFOREACH);
   consume(tLPAREN);

   if (peek() == tID && (peek_nth(2) == tSCOPE || is_specialised_scope()))
      qualify_scope = p_scope_prefix();

   foreach_array = true;
   vlog_node_t arr = p_select(scan(tTHIS, tSUPER) ? NULL : p_identifier());
   foreach_array = false;

   consume(tLSQUARE);

   ident_t names[MAX_FOREACH_DIMS];
   loc_t locs[MAX_FOREACH_DIMS];
   int nvars = 0;
   do {
      if (nvars == MAX_FOREACH_DIMS) {
         error_at(&state.last_loc, "too many loop variables in foreach");
         break;
      }

      if (peek() == tID) {
         names[nvars] = p_identifier();
         locs[nvars] = state.last_loc;
      }
      else
         names[nvars] = NULL;

      nvars++;
   } while (optional(tCOMMA));

   consume(tRSQUARE);
   consume(tRPAREN);

   const loc_t *loc = CURRENT_LOC;

   foreach_dim_t dims[MAX_FOREACH_DIMS];
   int ndims = 0, nselected = 0;
   vlog_node_t decl = NULL;
   switch (vlog_kind(arr)) {
   case V_REF:
   case V_MEMBER_REF:
      if (vlog_has_ref(arr))
         decl = vlog_ref(arr);
      break;
   case V_HIER_REF:
      decl = hier_member_decl(arr);
      if (decl == NULL)
         error_at(loc, "sorry, the array of this foreach cannot be found "
                  "from its hierarchical name");
      break;
   case V_BIT_SELECT:
      {
         // The loop variables are for the dimensions of an element
         vlog_node_t root = arr;
         while (vlog_kind(root) == V_BIT_SELECT) {
            nselected += vlog_params(root);
            root = vlog_value(root);
         }

         if ((vlog_kind(root) == V_REF || vlog_kind(root) == V_MEMBER_REF)
             && vlog_has_ref(root))
            decl = vlog_ref(root);
         else
            error_at(loc, "foreach requires the name of an array");
      }
      break;
   default:
      error_at(loc, "foreach requires the name of an array");
      break;
   }

   if (decl == NULL && vlog_kind(arr) == V_MEMBER_REF
       && vlog_virtual_pending(arr)) {
      // The interface is declared later so the bounds are found by
      // querying the member once it is known
      const char *queries[] = { "$left", "$right" };
      for (int i = 0; i < nvars && i < MAX_FOREACH_DIMS; i++) {
         vlog_node_t bound[2];
         for (int j = 0; j < 2; j++) {
            vlog_node_t q = vlog_new(V_SYS_FCALL);
            vlog_set_ident(q, ident_new(queries[j]));
            vlog_set_subkind(q, V_SYSTF_NONE);
            vlog_set_ival(q, time_ival());
            vlog_add_param(q, arr);
            vlog_add_param(q, foreach_number(i + 1, loc));
            vlog_set_loc(q, loc);
            bound[j] = q;
         }

         dims[ndims++] = (foreach_dim_t) { .left = bound[0], .right = bound[1] };
      }
   }

   if (decl != NULL) {
      switch (vlog_kind(decl)) {
      case V_VAR_DECL:
      case V_LOCAL_DECL:
      case V_NET_DECL:
      case V_PORT_DECL:
      case V_TF_PORT_DECL:
      case V_FUNC_DECL:   // The return value variable
      case V_PARAM_DECL:
      case V_LOCALPARAM:
         ndims = foreach_dimensions(decl, dims);
         if (nselected > 0 && nselected < ndims) {
            memmove(dims, dims + nselected,
                    (ndims - nselected) * sizeof(foreach_dim_t));
            ndims -= nselected;
         }
         else if (nselected > 0) {
            error_at(loc, "foreach has more loop variables than the element "
                     "has dimensions");
            ndims = 0;
         }

         if (nvars > ndims) {
            error_at(loc, "foreach has more loop variables than the array "
                     "has dimensions");
            ndims = 0;
         }
         break;
      default:
         error_at(loc, "foreach requires the name of an array");
         break;
      }
   }

   vlog_node_t top = NULL, inner = NULL;
   vlog_node_t idx[MAX_FOREACH_DIMS] = {};
   int depth = 0;
   bool ok = ndims > 0;

   for (int i = 0; ok && i < nvars; i++) {
      if (names[i] == NULL)
         continue;

      const loc_t *vloc = &locs[i];
      const foreach_dim_t *dim = &dims[i];

      vlog_node_t init, cond;
      vlog_incdec_t step_op;
      bool unknown_dir = false;
      vlog_node_t dyn_left = NULL, dyn_right = NULL;
      if (dim->assoc != NULL) {
         // The index takes the value of each key in order
         vlog_node_t kt = vlog_assoc_key(dim->assoc);
         if (kt == NULL) {
            error_at(vloc, "foreach over an associative array with a "
                     "wildcard index is not allowed");
            ok = false;
            break;
         }

         for (int j = 0; j < i; j++) {
            if (idx[j] == NULL) {
               error_at(vloc, "sorry, a dynamic dimension after a skipped "
                        "loop variable is not supported");
               ok = false;
            }
         }

         if (!ok)
            break;

         vlog_node_t prefix = arr;
         if (i > 0) {
            prefix = vlog_new(V_BIT_SELECT);
            vlog_set_value(prefix, arr);
            vlog_set_loc(prefix, vloc);
            for (int j = 0; j < i; j++)
               vlog_add_param(prefix, foreach_ref(idx[j], vloc));
         }

         vlog_node_t loop = vlog_new(V_FOR_LOOP);
         vlog_set_loc(loop, loc);

         vlog_symtab_push(symtab, loop);
         depth++;

         vlog_node_t d = vlog_new(V_LOCAL_DECL);
         vlog_set_ident(d, names[i]);
         vlog_set_loc(d, vloc);
         vlog_set_type(d, kt);
         vlog_symtab_put(symtab, d);
         idx[i] = d;

         // A hidden variable holds whether there is another key
         vlog_node_t more = vlog_new(V_LOCAL_DECL);
         vlog_set_ident(more, ident_uniq("more"));
         vlog_set_loc(more, vloc);
         vlog_set_type(more, make_integer_atom_type(DT_INT));

         vlog_node_t first = vlog_new(V_METHOD_CALL);
         vlog_set_value(first, prefix);
         vlog_set_ident(first, ident_new("first"));
         vlog_set_subkind(first, V_METHOD_ASSOC_FIRST);
         vlog_add_param(first, foreach_ref(d, vloc));
         vlog_set_loc(first, vloc);
         vlog_set_value(more, first);

         vlog_symtab_put(symtab, more);

         vlog_node_t finit = vlog_new(V_FOR_INIT);
         vlog_add_decl(finit, d);
         vlog_add_decl(finit, more);
         vlog_set_loc(finit, vloc);
         vlog_set_left(loop, finit);

         vlog_set_value(loop, foreach_ref(more, vloc));

         vlog_node_t next = vlog_new(V_METHOD_CALL);
         vlog_set_value(next, prefix);
         vlog_set_ident(next, ident_new("next"));
         vlog_set_subkind(next, V_METHOD_ASSOC_NEXT);
         vlog_add_param(next, foreach_ref(d, vloc));
         vlog_set_loc(next, vloc);

         vlog_node_t step = vlog_new(V_OP_ASSIGN);
         vlog_set_subkind(step, V_ASSIGN_EQUALS);
         vlog_set_target(step, foreach_ref(more, vloc));
         vlog_set_value(step, next);
         vlog_set_loc(step, vloc);

         vlog_node_t fstep = vlog_new(V_FOR_STEP);
         vlog_add_stmt(fstep, step);
         vlog_set_loc(fstep, vloc);
         vlog_set_right(loop, fstep);

         if (top == NULL)
            top = loop;
         else
            vlog_add_stmt(inner, loop);

         inner = loop;
         continue;
      }
      else if (dim->dynamic || dim->string) {
         // The size of an inner dimension depends on the outer indices
         for (int j = 0; j < i; j++) {
            if (idx[j] == NULL) {
               error_at(vloc, "sorry, a dynamic dimension after a skipped "
                        "loop variable is not supported");
               ok = false;
            }
         }

         if (!ok)
            break;

         vlog_node_t prefix = arr;
         if (i > 0) {
            prefix = vlog_new(V_BIT_SELECT);
            vlog_set_value(prefix, arr);
            vlog_set_loc(prefix, vloc);
            for (int j = 0; j < i; j++)
               vlog_add_param(prefix, foreach_ref(idx[j], vloc));
         }

         vlog_node_t size = vlog_new(V_METHOD_CALL);
         vlog_set_value(size, prefix);
         vlog_set_ident(size, ident_new(dim->string ? "len" : "size"));
         vlog_set_subkind(size, dim->string ? V_METHOD_STR_LEN : V_METHOD_SIZE);
         vlog_set_loc(size, vloc);

         init = foreach_number(0, vloc);
         cond = size;   // Completed below once the variable exists
         step_op = V_INCDEC_PLUS;
      }
      else {
         // A bound which names a parameter may differ in another instance
         int64_t l = 0, r = 0;
            const bool known = vlog_kind(dim->left) == V_NUMBER
            && vlog_kind(dim->right) == V_NUMBER
            && vlog_get_const(dim->left, &l) && vlog_get_const(dim->right, &r);

         init = dim->left;
         cond = dim->right;
         step_op = known && l > r ? V_INCDEC_MINUS : V_INCDEC_PLUS;

         unknown_dir = !known;
         if (unknown_dir) {
            dyn_left = dim->left;
            dyn_right = dim->right;
         }
      }

      vlog_node_t loop = vlog_new(V_FOR_LOOP);
      vlog_set_loc(loop, loc);

      vlog_symtab_push(symtab, loop);
      depth++;

      vlog_node_t d = vlog_new(V_LOCAL_DECL);
      vlog_set_ident(d, names[i]);
      vlog_set_loc(d, vloc);

      vlog_node_t dt = make_integer_atom_type(DT_INT);
      vlog_set_flags(dt, VLOG_F_SIGNED);
      vlog_set_type(d, dt);
      vlog_set_value(d, init);

      vlog_symtab_put(symtab, d);
      idx[i] = d;

      vlog_node_t finit = vlog_new(V_FOR_INIT);
      vlog_add_decl(finit, d);
      vlog_set_loc(finit, vloc);
      vlog_set_left(loop, finit);

      vlog_binary_t op = V_BINARY_LEQ;
      if (dim->dynamic || dim->string)
         op = V_BINARY_LT;
      else if (step_op == V_INCDEC_MINUS)
         op = V_BINARY_GEQ;

      vlog_node_t fstep = vlog_new(V_FOR_STEP);
      vlog_set_loc(fstep, vloc);

      if (unknown_dir) {
         // Count up or down to the right bound depending on its order
         vlog_node_t gt = foreach_binary(V_BINARY_GT, dyn_left, dyn_right,
                                         vloc);

         vlog_node_t down = foreach_binary(V_BINARY_GEQ, foreach_ref(d, vloc),
                                           cond, vloc);
         vlog_node_t up = foreach_binary(
            V_BINARY_LEQ, foreach_ref(d, vloc), dyn_right, vloc);

         vlog_node_t test = vlog_new(V_COND_EXPR);
         vlog_set_value(test, gt);
         vlog_set_left(test, down);
         vlog_set_right(test, up);
         vlog_set_loc(test, vloc);
         vlog_set_value(loop, test);

         vlog_node_t gt2 = foreach_binary(V_BINARY_GT, dyn_left, dyn_right,
                                          vloc);

         vlog_node_t delta = vlog_new(V_COND_EXPR);
         vlog_set_value(delta, gt2);
         vlog_set_left(delta, foreach_number(-1, vloc));
         vlog_set_right(delta, foreach_number(1, vloc));
         vlog_set_loc(delta, vloc);

         vlog_node_t step = vlog_new(V_OP_ASSIGN);
         vlog_set_subkind(step, V_ASSIGN_PLUS);
         vlog_set_target(step, foreach_ref(d, vloc));
         vlog_set_value(step, delta);
         vlog_set_loc(step, vloc);
         vlog_add_stmt(fstep, step);
      }
      else {
         vlog_set_value(loop, foreach_binary(op, foreach_ref(d, vloc), cond,
                                             vloc));

         vlog_node_t post = vlog_new(V_POSTFIX);
         vlog_set_subkind(post, step_op);
         vlog_set_target(post, foreach_ref(d, vloc));
         vlog_set_loc(post, vloc);
         vlog_add_stmt(fstep, post);
      }

      vlog_set_right(loop, fstep);

      if (top == NULL)
         top = loop;
      else
         vlog_add_stmt(inner, loop);

      inner = loop;
   }

   vlog_node_t body = p_statement_or_null();

   for (int i = 0; i < depth; i++)
      vlog_symtab_pop(symtab);

   if (top == NULL) {
      // Nothing to iterate over or an earlier error
      top = vlog_new(V_SEQ_BLOCK);
      vlog_set_loc(top, loc);
      if (body != NULL && !ok)
         vlog_add_stmt(top, body);
      return top;
   }

   if (body != NULL)
      vlog_add_stmt(inner, body);

   return top;
}

static vlog_node_t p_loop_statement_body(void)
{
   // forever statement_or_null
   //   | repeat ( expression ) statement_or_null
   //   | while ( expression ) statement_or_null
   //   | for ( [ for_initialization ] ; [ expression ] ; [ for_step ] )
   //       statement_or_null
   //   | do statement_or_null while ( expression ) ;
   //   | foreach ( ps_or_hierarchical_array_identifier [ loop_variables ] )
   //       statement

   BEGIN("loop statement");

   if (peek() == tFOREACH)
      return p_foreach_statement();

   switch (one_of(tFOREVER, tWHILE, tREPEAT, tDO, tFOR)) {
   case tFOREVER:
      {
         vlog_node_t v = vlog_new(V_FOREVER);

         vlog_node_t s = p_statement_or_null();
         if (s != NULL)
            vlog_add_stmt(v, s);

         vlog_set_loc(v, CURRENT_LOC);
         return v;
      }

   case tWHILE:
      {
         vlog_node_t v = vlog_new(V_WHILE);

         consume(tLPAREN);
         vlog_set_value(v, p_expression());
         consume(tRPAREN);

         vlog_node_t s = p_statement_or_null();
         if (s != NULL)
            vlog_add_stmt(v, s);

         vlog_set_loc(v, CURRENT_LOC);
         return v;
      }

   case tREPEAT:
      {
         vlog_node_t v = vlog_new(V_REPEAT);

         consume(tLPAREN);
         vlog_set_value(v, p_expression());
         consume(tRPAREN);

         vlog_node_t s = p_statement_or_null();
         if (s != NULL)
            vlog_add_stmt(v, s);

         vlog_set_loc(v, CURRENT_LOC);
         return v;
      }

   case tDO:
      {
         vlog_node_t v = vlog_new(V_DO_WHILE);

         vlog_node_t s = p_statement_or_null();
         if (s != NULL)
            vlog_add_stmt(v, s);

         consume(tWHILE);

         consume(tLPAREN);
         vlog_set_value(v, p_expression());
         consume(tRPAREN);
         consume(tSEMI);

         vlog_set_loc(v, CURRENT_LOC);
         return v;
      }

   case tFOR:
      {
         vlog_node_t v = vlog_new(V_FOR_LOOP);

         vlog_symtab_push(symtab, v);

         consume(tLPAREN);

         if (not_at_token(tSEMI))
            vlog_set_left(v, p_for_initialization());
         else
            vlog_set_left(v, vlog_new(V_FOR_INIT));

         consume(tSEMI);

         if (not_at_token(tSEMI))
            vlog_set_value(v, p_expression());
         else {
            // No condition is the same as true
            vlog_node_t one = vlog_new(V_NUMBER);
            vlog_set_number(one, number_from_int(1));
            vlog_set_loc(one, &state.last_loc);
            vlog_set_value(v, one);
         }

         consume(tSEMI);

         if (not_at_token(tRPAREN))
            vlog_set_right(v, p_for_step());
         else
            vlog_set_right(v, vlog_new(V_FOR_STEP));

         consume(tRPAREN);

         vlog_node_t s = p_statement_or_null();
         if (s != NULL)
            vlog_add_stmt(v, s);

         vlog_symtab_pop(symtab);

         vlog_set_loc(v, CURRENT_LOC);
         return v;
      }

   default:
      should_not_reach_here();
   }
}

static vlog_node_t p_wait_statement(void)
{
   // wait ( expression ) statement_or_null
   //   | wait fork ;
   //   | wait_order ( hierarchical_identifier { , hierarchical_identifier } )
   //       action_block

   BEGIN("wait statement");

   consume(tWAIT);

   if (optional(tFORK)) {
      vlog_node_t v = vlog_new(V_FORK_CTRL);
      vlog_set_subkind(v, V_FORK_WAIT);
      consume(tSEMI);
      vlog_set_loc(v, CURRENT_LOC);
      return v;
   }

   vlog_node_t v = vlog_new(V_WAIT);

   consume(tLPAREN);

   vlog_set_value(v, p_expression());

   consume(tRPAREN);

   vlog_node_t s = p_statement_or_null();
   if (s != NULL)
      vlog_add_stmt(v, s);

   vlog_set_loc(v, CURRENT_LOC);
   return v;
}

static vlog_node_t p_case_item(bool inside)
{
   // case_item_expression { , case_item_expression } : statement_or_null
   //   | default [ : ] statement_or_null

   BEGIN("case item");

   vlog_node_t v = vlog_new(V_CASE_ITEM);

   if (optional(tDEFAULT))
      optional(tCOLON);
   else {
      do {
         if (inside && peek() == tLSQUARE)
            vlog_add_param(v, p_value_range());
         else
            vlog_add_param(v, p_expression());
      } while (optional(tCOMMA));

      consume(tCOLON);
   }

   vlog_set_loc(v, CURRENT_LOC);

   vlog_node_t s = p_statement_or_null();
   if (s != NULL)
      vlog_add_stmt(v, s);

   return v;
}

static vlog_node_t p_case_matches(vlog_case_kind_t kind, vlog_node_t expr)
{
   // matches case_pattern_item { case_pattern_item } endcase
   //
   // case_pattern_item ::= pattern [ &&& expression ] : statement_or_null
   //                     | default [ : ] statement_or_null
   //
   // This is expanded to a conditional statement which tests each pattern
   // in order against a copy of the case expression.

   BEGIN("case pattern statement");

   consume(tMATCHES);

   vlog_node_t type = vlog_get_type(expr);

   vlog_node_t block = vlog_new(V_SEQ_BLOCK);
   vlog_set_ident(block, default_label("match"));
   vlog_set_loc(block, &state.last_loc);

   vlog_node_t tmp = vlog_new(V_LOCAL_DECL);
   vlog_set_ident(tmp, ident_uniq("match"));
   vlog_set_loc(tmp, vlog_loc(expr));
   if (type != NULL)
      vlog_set_type(tmp, type);
   else
      parse_error(vlog_loc(expr), "cannot determine the type of the case "
                  "expression");
   vlog_add_decl(block, tmp);

   vlog_node_t init = vlog_new(V_BASSIGN);
   vlog_node_t target = vlog_new(V_REF);
   vlog_set_ident(target, vlog_ident(tmp));
   vlog_set_ref(target, tmp);
   vlog_set_loc(target, vlog_loc(expr));
   vlog_set_target(init, target);
   vlog_set_value(init, expr);
   vlog_set_loc(init, vlog_loc(expr));
   vlog_add_stmt(block, init);

   vlog_node_t chain = vlog_new(V_IF);
   vlog_node_t fallback = NULL;

   do {
      const int depth = match_depth;

      if (optional(tDEFAULT)) {
         optional(tCOLON);

         vlog_node_t s = p_statement_or_null();
         if (s != NULL) {
            if (fallback == NULL)
               fallback = vlog_new(V_COND);
            vlog_add_stmt(fallback, s);
         }
         continue;
      }

      vlog_node_t m = vlog_new(V_MATCHES);
      vlog_set_subkind(m, kind);
      vlog_set_loc(m, &state.last_loc);

      vlog_node_t ref = vlog_new(V_REF);
      vlog_set_ident(ref, vlog_ident(tmp));
      vlog_set_ref(ref, tmp);
      vlog_set_loc(ref, vlog_loc(expr));
      vlog_set_value(m, ref);

      vlog_symtab_push(symtab, m);
      match_depth++;

      vlog_set_left(m, p_pattern(type, m));
      vlog_set_loc(m, CURRENT_LOC);

      vlog_node_t test = m;
      while (optional(tTRPLAMP)) {
         vlog_node_t and = vlog_new(V_BINARY);
         vlog_set_subkind(and, V_BINARY_LOG_AND);
         vlog_set_left(and, test);
         vlog_set_right(and, p_expression());
         vlog_set_loc(and, CURRENT_LOC);
         test = and;
      }

      consume(tCOLON);

      vlog_node_t c = vlog_new(V_COND);
      vlog_set_value(c, test);
      vlog_add_cond(chain, c);

      vlog_node_t s = p_statement_or_null();
      if (s != NULL)
         vlog_add_stmt(c, s);

      match_unwind(depth);
   } while (not_at_token(tENDCASE));

   consume(tENDCASE);

   if (fallback != NULL && vlog_conds(chain) > 0)
      vlog_add_cond(chain, fallback);
   else if (fallback != NULL) {
      const int nstmts = vlog_stmts(fallback);
      for (int i = 0; i < nstmts; i++)
         vlog_add_stmt(block, vlog_stmt(fallback, i));
   }

   if (vlog_conds(chain) > 0) {
      vlog_set_loc(chain, CURRENT_LOC);
      vlog_add_stmt(block, chain);
   }

   vlog_set_loc(block, CURRENT_LOC);
   return block;
}

static vlog_node_t p_case_statement(void)
{
   // [ unique_priority ] case_keyword ( case_expression )
   //        case_item { case_item } endcase
   //   | [ unique_priority ] case_keyword ( case_expression ) matches
   //        case_pattern_item { case_pattern_item } endcase
   //   | [ unique_priority ] case ( case_expression ) inside case_inside_item
   //        { case_inside_item } endcase

   BEGIN("case statement");

   vlog_case_kind_t kind = V_CASE_NORMAL;
   switch (one_of(tCASE, tCASEX, tCASEZ)) {
   case tCASEX: kind = V_CASE_X; break;
   case tCASEZ: kind = V_CASE_Z; break;
   }

   vlog_node_t v = vlog_new(V_CASE);
   vlog_set_subkind(v, kind);

   consume(tLPAREN);

   vlog_set_value(v, p_expression());

   consume(tRPAREN);

   if (peek() == tMATCHES)
      return p_case_matches(kind, vlog_value(v));

   if (kind == V_CASE_NORMAL && optional(tINSIDE))
      vlog_set_subkind(v, V_CASE_INSIDE);

   do {
      vlog_add_stmt(v, p_case_item(vlog_subkind(v) == V_CASE_INSIDE));
   } while (not_at_token(tENDCASE));

   consume(tENDCASE);

   vlog_set_loc(v, CURRENT_LOC);
   return v;
}

static vlog_node_t p_randcase_item(void)
{
   // expression : statement_or_null

   BEGIN("randcase item");

   vlog_node_t v = vlog_new(V_RANDCASE_ITEM);
   vlog_set_value(v, p_expression());

   consume(tCOLON);

   vlog_node_t s = p_statement_or_null();
   if (s != NULL)
      vlog_add_stmt(v, s);

   vlog_set_loc(v, CURRENT_LOC);
   return v;
}

static vlog_node_t p_randcase_statement(void)
{
   // randcase randcase_item { randcase_item } endcase

   BEGIN("randcase statement");

   consume(tRANDCASE);

   vlog_node_t v = vlog_new(V_RANDCASE);

   do {
      vlog_add_stmt(v, p_randcase_item());
   } while (not_at_token(tENDCASE));

   consume(tENDCASE);

   vlog_set_loc(v, CURRENT_LOC);
   return v;
}

static vlog_node_t p_event_trigger(void)
{
   // -> hierarchical_event_identifier ;
   //   | ->> [ delay_or_event_control ] hierarchical_event_identifier ;

   BEGIN("event trigger");

   consume(tIFIMPL);

   const bool nonblocking = optional(tGT);

   vlog_node_t delay = NULL, count = NULL;
   if (nonblocking) {
      if (peek() == tHASH)
         delay = p_delay_control();
      else if (peek() == tREPEAT) {
         consume(tREPEAT);
         consume(tLPAREN);
         count = p_expression();
         consume(tRPAREN);
         delay = p_event_control();
      }
      else if (peek() == tAT)
         delay = p_event_control();
   }

   vlog_node_t v = vlog_new(V_EVENT_TRIGGER);
   vlog_node_t target = p_select(p_identifier());
   if (vlog_kind(target) == V_REF)
      vlog_set_ident(v, vlog_ident(target));
   else
      vlog_set_ident(v, ident_new("<event>"));
   vlog_set_value(v, target);
   vlog_set_subkind(v, nonblocking ? 1 : 0);

   consume(tSEMI);

   const loc_t *loc = CURRENT_LOC;
   vlog_set_loc(v, loc);

   if (delay == NULL)
      return v;
   else if (vlog_kind(delay) == V_DELAY_CONTROL) {
      vlog_set_delay(v, delay);
      return v;
   }

   // The trigger is delayed until the event control occurs without
   // blocking the current process

   vlog_node_t wait = vlog_new(V_TIMING);
   vlog_set_value(wait, delay);
   vlog_set_loc(wait, loc);

   vlog_node_t body = vlog_new(V_SEQ_BLOCK);
   vlog_set_loc(body, loc);

   if (count != NULL) {
      vlog_node_t rpt = vlog_new(V_REPEAT);
      vlog_set_value(rpt, count);
      vlog_add_stmt(rpt, wait);
      vlog_set_loc(rpt, loc);
      vlog_add_stmt(body, rpt);
   }
   else
      vlog_add_stmt(body, wait);

   vlog_add_stmt(body, v);

   vlog_node_t fork = vlog_new(V_FORK);
   vlog_set_subkind(fork, V_FORK_JOIN_NONE);
   vlog_set_loc(fork, loc);
   vlog_add_stmt(fork, body);
   return fork;
}

static vlog_node_t rs_if(vlog_node_t cond, vlog_node_t then,
                         vlog_node_t otherwise, const loc_t *loc);

static hash_t *pca_vars = NULL, *pca_scopes = NULL;
static unsigned pca_count = 0;

static void *pca_hier_key(vlog_node_t target)
{
   // Assignments to the same hierarchical name share their state

   LOCAL_TEXT_BUF tb = tb_new();
   for (;;) {
      tb_printf(tb, "%s.", istr(vlog_ident(target)));
      vlog_node_t prefix = vlog_value(target);
      if (vlog_kind(prefix) == V_HIER_REF)
         target = prefix;
      else {
         for (;;) {
            tb_printf(tb, "%s.", istr(vlog_ident(prefix)));
            if (vlog_kind(prefix) != V_MOD_REF || !vlog_has_value(prefix))
               break;
            prefix = vlog_value(prefix);
         }
         break;
      }
   }

   return (void *)ident_new(tb_get(tb));
}

static void *pca_base(vlog_node_t target)
{
   while (vlog_kind(target) == V_BIT_SELECT || vlog_kind(target) == V_PART_SELECT)
      target = vlog_value(target);

   if (vlog_kind(target) == V_HIER_REF)
      return pca_hier_key(target);

   if (vlog_kind(target) != V_REF || !vlog_has_ref(target))
      return NULL;

   vlog_node_t d = vlog_ref(target);
   if (vlog_kind(d) != V_VAR_DECL && vlog_kind(d) != V_NET_DECL)
      return NULL;

   return d;
}

static vlog_node_t pca_ref(vlog_node_t var, const loc_t *loc)
{
   vlog_node_t ref = vlog_new(V_REF);
   vlog_set_ident(ref, vlog_ident(var));
   vlog_set_ref(ref, var);
   vlog_set_loc(ref, loc);
   return ref;
}

static vlog_node_t pca_set(vlog_node_t var, int value, const loc_t *loc)
{
   vlog_node_t num = vlog_new(V_NUMBER);
   vlog_set_number(num, number_from_int(value));
   vlog_set_loc(num, loc);

   vlog_node_t a = vlog_new(V_BASSIGN);
   vlog_set_target(a, pca_ref(var, loc));
   vlog_set_value(a, num);
   vlog_set_loc(a, loc);
   return a;
}

static vlog_node_t p_sva_trigger(vlog_node_t decl)
{
   // The sequence named by the next token as an event or in a method call:
   // a variable which is true in the time step where the sequence matched

   static hash_t *vars = NULL;
   static unsigned count = 0;

   const loc_t loc = state.last_loc;

   if (vars == NULL)
      vars = hash_new(16);

   vlog_node_t var = hash_get(vars, decl);
   if (var == NULL) {
      if (current_module == NULL || vlog_ports(decl) > 0
          || !vlog_has_value(decl)) {
         error_at(&loc, "sorry, a sequence used as an event or with the "
                  "triggered method must be declared in a module and have no "
                  "formal arguments");
         return sva_number(0, &loc);
      }

      var = vlog_new(V_VAR_DECL);
      vlog_set_ident(var, ident_sprintf("$trigger%u", count++));
      vlog_set_type(var, logic_type());
      vlog_set_loc(var, &loc);

      vlog_node_t zero = vlog_new(V_NUMBER);
      vlog_set_number(zero, number_from_int(0));
      vlog_set_loc(zero, &loc);
      vlog_set_value(var, zero);

      vlog_add_decl(current_module, var);

      vlog_node_t spec = vlog_new(V_PROP_SPEC);
      vlog_set_value(spec, vlog_value(decl));
      vlog_set_loc(spec, &loc);
      sva_apply_defaults(spec);

      vlog_node_t cover = vlog_new(V_COVER);
      vlog_set_subkind(cover, V_ASSERT_COVER_SEQ);
      vlog_set_value(cover, spec);
      vlog_set_loc(cover, &loc);

      vlog_node_t wait = vlog_new(V_SYS_TCALL);
      vlog_set_ident(wait, ident_new("$__postponed"));
      vlog_set_subkind(wait, V_SYSTF_NONE);
      vlog_set_ival(wait, 0);
      vlog_set_loc(wait, &loc);

      vlog_node_t c0 = vlog_new(V_COND);
      vlog_add_stmt(c0, pca_set(var, 1, &loc));
      vlog_add_stmt(c0, wait);
      vlog_add_stmt(c0, pca_set(var, 0, &loc));
      vlog_add_cond(cover, c0);

      vlog_node_t p = p_assertion_process(cover);
      if (p != NULL)
         vlog_add_stmt(current_module, p);

      hash_put(vars, decl, var);
   }

   return pca_ref(var, &loc);
}

static vlog_node_t pca_var(void *base, int which, const loc_t *loc)
{
   // One variable records the active assign and another the active force

   if (pca_vars == NULL) {
      pca_vars = hash_new(16);
      pca_scopes = hash_new(16);
   }

   void *key = (char *)base + which;
   vlog_node_t var = hash_get(pca_vars, key);
   if (var != NULL && hash_get(pca_scopes, var) != current_module)
      var = NULL;   // Declared in another scope

   if (var == NULL) {
      var = vlog_new(V_VAR_DECL);
      vlog_set_ident(var, ident_sprintf("$pca%u", ++pca_count));
      vlog_set_type(var, make_integer_atom_type(DT_INT));
      vlog_set_loc(var, loc);
      vlog_add_decl(current_module, var);
      hash_put(pca_vars, key, var);
      hash_put(pca_scopes, var, current_module);
   }

   return var;
}

static vlog_node_t pca_eq(vlog_node_t var, int value, const loc_t *loc)
{
   vlog_node_t num = vlog_new(V_NUMBER);
   vlog_set_number(num, number_from_int(value));
   vlog_set_loc(num, loc);

   vlog_node_t cond = vlog_new(V_BINARY);
   vlog_set_subkind(cond, V_BINARY_LOG_EQ);
   vlog_set_left(cond, pca_ref(var, loc));
   vlog_set_right(cond, num);
   vlog_set_loc(cond, loc);
   return cond;
}

static vlog_node_t pca_and(vlog_node_t a, vlog_node_t b, const loc_t *loc)
{
   vlog_node_t v = vlog_new(V_BINARY);
   vlog_set_subkind(v, V_BINARY_LOG_AND);
   vlog_set_left(v, a);
   vlog_set_right(v, b);
   vlog_set_loc(v, loc);
   return v;
}

static vlog_node_t rewrite_pca(vlog_node_t stmt, bool is_assign)
{
   // A procedural continuous assignment or force follows the value of
   // its expression until it is released.  The statement records which
   // assignment is active and a process repeats it when an operand
   // changes.  A force overrides an assign and releasing it reveals the
   // assign again, deassign does not affect a force.

   const loc_t *loc = vlog_loc(stmt);

   void *base = pca_base(vlog_target(stmt));
   if (base == NULL || current_module == NULL)
      return stmt;

   vlog_node_t avar = pca_var(base, 0, loc);
   vlog_node_t fvar = pca_var(base, 1, loc);

   vlog_node_t blk = vlog_new(V_SEQ_BLOCK);
   vlog_set_loc(blk, loc);

   if (vlog_kind(stmt) == V_RELEASE) {
      vlog_node_t mine = is_assign ? avar : fvar;
      vlog_node_t other = is_assign ? fvar : avar;
      vlog_add_stmt(blk, pca_set(mine, 0, loc));
      vlog_add_stmt(blk, rs_if(pca_eq(other, 0, loc), stmt, NULL, loc));
      return blk;
   }

   const unsigned n = ++pca_count;

   vlog_node_t ctrl = vlog_new(V_EVENT_CONTROL);
   vlog_set_loc(ctrl, loc);

   for (int i = 0; i < (is_assign ? 2 : 1); i++) {
      vlog_node_t ev = vlog_new(V_EVENT);
      vlog_set_subkind(ev, V_EVENT_LEVEL);
      vlog_set_value(ev, pca_ref(i == 0 ? (is_assign ? avar : fvar) : fvar,
                                 loc));
      vlog_set_loc(ev, loc);
      vlog_add_param(ctrl, ev);
   }

   vlog_node_t rhs = vlog_value(stmt);
   if (monitor_arg_sensitive(rhs)) {
      vlog_node_t ev = vlog_new(V_EVENT);
      vlog_set_subkind(ev, V_EVENT_LEVEL);
      vlog_set_value(ev, rhs);
      vlog_set_loc(ev, loc);
      vlog_add_param(ctrl, ev);
   }

   vlog_node_t cond;
   if (is_assign) {
      vlog_add_stmt(blk, pca_set(avar, n, loc));
      vlog_add_stmt(blk, rs_if(pca_eq(fvar, 0, loc), stmt, NULL, loc));
      cond = pca_and(pca_eq(avar, n, loc), pca_eq(fvar, 0, loc), loc);
   }
   else {
      vlog_add_stmt(blk, pca_set(fvar, n, loc));
      vlog_add_stmt(blk, stmt);
      cond = pca_eq(fvar, n, loc);
   }

   vlog_node_t timing = vlog_new(V_TIMING);
   vlog_set_value(timing, ctrl);
   vlog_set_loc(timing, loc);
   vlog_add_stmt(timing, rs_if(cond, stmt, NULL, loc));

   vlog_node_t always = vlog_new(V_ALWAYS);
   vlog_set_subkind(always, V_ALWAYS_PLAIN);
   vlog_set_ident(always, ident_sprintf("$pca_always%u", n));
   vlog_set_loc(always, loc);
   vlog_add_stmt(always, timing);
   vlog_add_stmt(current_module, always);

   return blk;
}

static vlog_node_t p_procedural_continuous_assignment(void)
{
   // assign variable_assignment | deassign variable_lvalue
   //   | force variable_assignment | force net_assignment
   //   | release variable_lvalue | release net_lvalue

   BEGIN("procedural continuous assignment");

   const token_t type = one_of(tASSIGN, tDEASSIGN, tFORCE, tRELEASE);
   switch (type) {
   case tASSIGN:
   default:
      {
         vlog_node_t a = p_variable_assignment(V_ASSIGN);

         // The effect is that of a force of the variable
         vlog_node_t v = vlog_new(V_FORCE);
         vlog_set_target(v, vlog_target(a));
         vlog_set_value(v, vlog_value(a));
         vlog_set_loc(v, vlog_loc(a));
         return rewrite_pca(v, true);
      }
   case tDEASSIGN:
   case tRELEASE:
      {
         vlog_node_t v = vlog_new(V_RELEASE);
         vlog_set_target(v, p_variable_lvalue());
         vlog_set_loc(v, CURRENT_LOC);
         return rewrite_pca(v, type == tDEASSIGN);
      }
   case tFORCE:
      return rewrite_pca(p_variable_assignment(V_FORCE), false);
   }
}

static void check_pending_disables(void)
{
   // A name may refer to a block declared later in the module

   for (int i = 0; i < pending_disables.count; i++) {
      vlog_node_t d = pending_disables.items[i];
      if (block_names == NULL || !hset_contains(block_names, vlog_ident(d)))
         error_at(vlog_loc(d), "no visible label %pQ", vlog_ident(d));
   }

   ACLEAR(pending_disables);
   if (block_names != NULL)
      hset_free(block_names);
   block_names = NULL;
}

static void note_block_name(ident_t name)
{
   if (block_names == NULL)
      block_names = hset_new(16);

   hset_insert(block_names, name);
}

static vlog_node_t p_disable_statement(void)
{
   // disable hierarchical_task_identifier ;
   //   | disable hierarchical_block_identifier ;
   //   | disable fork ;

   BEGIN("disable statement");

   consume(tDISABLE);

   if (optional(tFORK)) {
      vlog_node_t v = vlog_new(V_FORK_CTRL);
      vlog_set_subkind(v, V_FORK_DISABLE);
      consume(tSEMI);
      vlog_set_loc(v, CURRENT_LOC);
      return v;
   }

   ident_t id = p_identifier();

   vlog_node_t decl = vlog_symtab_query(symtab, id);

   if (decl != NULL && vlog_kind(decl) == V_TASK_DECL && peek() == tDOT) {
      // A named block inside a task
      consume(tDOT);
      id = p_identifier();
      decl = NULL;
   }
   else if ((decl != NULL && (vlog_kind(decl) == V_MOD_INST
                              || vlog_kind(decl) == V_IFACE_PORT))
            || (decl == NULL && (peek() == tDOT || at_instance_index(true)))) {
      // Subroutine or block in another instance
      vlog_node_t target = p_hierarchical_identifier(id);

      vlog_node_t v = vlog_new(V_DISABLE);
      vlog_set_ident(v, vlog_ident(target));
      vlog_set_value(v, target);

      consume(tSEMI);

      vlog_set_loc(v, CURRENT_LOC);
      return v;
   }

   if (decl != NULL) {
      switch (vlog_kind(decl)) {
      case V_SEQ_BLOCK:
      case V_FORK:
      case V_TASK_DECL:
         break;
      default:
         {
            diag_t *d = diag_new(DIAG_ERROR, &state.last_loc);
            diag_printf(d, "cannot disable %pQ", id);
            diag_hint(d, vlog_loc(decl), "%pQ declared here", id);
            diag_emit(d);
         }
         break;
      }
   }

   vlog_node_t v = vlog_new(V_DISABLE);
   vlog_set_ident(v, id);
   if (decl != NULL && vlog_kind(decl) == V_TASK_DECL)
      vlog_set_ref(v, decl);

   consume(tSEMI);

   vlog_set_loc(v, CURRENT_LOC);
   if (decl == NULL)
      APUSH(pending_disables, v);
   return v;
}

static vlog_node_t p_loop_statement(void)
{
   rs_loop_depth++;
   vlog_node_t v = p_loop_statement_body();
   rs_loop_depth--;
   return v;
}

static vlog_node_t p_jump_statement(void)
{
   // return [ expression ] ; | break ; | continue ;

   BEGIN("jump statement");

   const token_t tok = one_of(tRETURN, tBREAK, tCONTINUE);

   if (tok == tBREAK && rs_break_name != NULL && rs_loop_depth == 0) {
      // Leaves the random sequence
      vlog_node_t v = vlog_new(V_DISABLE);
      vlog_set_ident(v, rs_break_name);
      consume(tSEMI);
      vlog_set_loc(v, CURRENT_LOC);
      return v;
   }
   else if (tok == tRETURN && rs_return_name != NULL && peek() == tSEMI) {
      // Leaves the production
      vlog_node_t v = vlog_new(V_DISABLE);
      vlog_set_ident(v, rs_return_name);
      consume(tSEMI);
      vlog_set_loc(v, CURRENT_LOC);
      return v;
   }

   switch (tok) {
   case tBREAK:
   case tCONTINUE:
      {
         vlog_node_t v = vlog_new(V_LOOP_JUMP);
         vlog_set_subkind(v, tok == tBREAK ? V_JUMP_BREAK : V_JUMP_CONTINUE);
         consume(tSEMI);
         vlog_set_loc(v, CURRENT_LOC);
         return v;
      }
   case tRETURN:
      {
         vlog_node_t v = vlog_new(V_RETURN);

         vlog_node_t subr = vlog_symtab_subr(symtab);
         if (subr == NULL)
            parse_error(&state.last_loc, "return statement can only be used "
                        "in a subroutine");
         else
            vlog_set_ref(v, subr);

         if (peek() == tTAGGED || peek() == tTICK) {
            vlog_node_t rtype = NULL;
            if (subr != NULL && vlog_kind(subr) == V_FUNC_DECL
                && vlog_has_type(subr))
               rtype = vlog_get_type(vlog_type(subr));
            vlog_set_value(v, p_expression_of_type(rtype));
         }
         else if (peek() != tSEMI)
            vlog_set_value(v, p_expression());

         consume(tSEMI);

         vlog_set_loc(v, CURRENT_LOC);
         return v;
      }
   default:
      should_not_reach_here();
   }
}

static vlog_node_t p_expect_statement(void)
{
   // expect ( property_spec ) action_block

   BEGIN("expect statement");

   consume(tEXPECT);
   consume(tLPAREN);

   vlog_node_t v = vlog_new(V_ASSERT);
   vlog_set_subkind(v, V_ASSERT_EXPECT);

   vlog_node_t spec = p_property_spec();
   sva_apply_defaults(spec);
   vlog_set_value(v, spec);

   consume(tRPAREN);

   if (!vlog_has_target(spec))
      error_at(&state.last_loc, "no clocking event specified for expect "
               "statement and no default clocking in this scope");

   p_action_block(v);

   vlog_set_loc(v, CURRENT_LOC);

   if (current_module != NULL)
      APUSH(module_asserts, v);

   return v;
}

static vlog_node_t p_procedural_concurrent_assertion(void)
{
   // assert property ( property_spec ) action_block
   //
   // An assertion at the top level of an always block with an event
   // control is checked at that clock.  Anywhere else it is not checked.

   BEGIN("procedural concurrent assertion");

   vlog_node_t v = p_concurrent_assertion_statement(NULL);
   if (v == NULL)
      return NULL;

   vlog_node_t stub = vlog_new(vlog_kind(v));
   vlog_set_subkind(stub, V_ASSERT_PROCEDURAL);
   vlog_set_value(stub, vlog_value(v));
   vlog_set_loc(stub, vlog_loc(v));

   if (always_depth > 0) {
      APUSH(proc_asserts, v);
      APUSH(proc_asserts, stub);
   }
   else {
      static bool warned = false;
      if (!warned) {
         warn_at(vlog_loc(v), "concurrent assertions in this procedure "
                 "are not checked");
         warned = true;
      }
   }

   return stub;
}

static bool find_assert_guards(vlog_node_t v, vlog_node_t target,
                               vlog_node_t *guard)
{
   // The assertion is reached through blocks and the first branch of
   // conditional statements: the conditions are collected in guard

   const int nstmts = vlog_stmts(v);
   for (int i = 0; i < nstmts; i++) {
      vlog_node_t s = vlog_stmt(v, i);
      if (s == target)
         return true;
      else if (vlog_kind(s) == V_SEQ_BLOCK
               && find_assert_guards(s, target, guard))
         return true;
      else if (vlog_kind(s) == V_IF && vlog_conds(s) > 0) {
         vlog_node_t c0 = vlog_cond(s, 0);
         if (vlog_has_value(c0) && find_assert_guards(c0, target, guard)) {
            vlog_node_t cond = vlog_value(c0);
            if (*guard != NULL) {
               vlog_node_t both = vlog_new(V_BINARY);
               vlog_set_subkind(both, V_BINARY_LOG_AND);
               vlog_set_left(both, cond);
               vlog_set_right(both, *guard);
               vlog_set_loc(both, vlog_loc(cond));
               cond = both;
            }

            *guard = cond;
            return true;
         }
      }
   }

   return false;
}

static void check_module_scope_cb(vlog_node_t v, void *ctx)
{
   bool *ok = ctx;

   if (!vlog_has_ref(v))
      return;

   vlog_node_t d = vlog_ref(v);
   if (vlog_kind(d) != V_VAR_DECL)
      return;

   const int ndecls = vlog_decls(current_module);
   for (int i = 0; i < ndecls; i++) {
      if (vlog_decl(current_module, i) == d)
         return;
   }

   *ok = false;
}

static void hoist_procedural_assertions(vlog_node_t always, int first)
{
   vlog_node_t timing = NULL;
   if (vlog_stmts(always) == 1 && vlog_kind(vlog_stmt(always, 0)) == V_TIMING
       && vlog_kind(vlog_value(vlog_stmt(always, 0))) == V_EVENT_CONTROL)
      timing = vlog_stmt(always, 0);

   for (int i = first; i < proc_asserts.count; i += 2) {
      vlog_node_t v = proc_asserts.items[i];
      vlog_node_t stub = proc_asserts.items[i + 1];
      vlog_node_t spec = vlog_value(v);

      bool module_scope = true;
      vlog_visit_only(v, check_module_scope_cb, &module_scope, V_REF);

      vlog_node_t guard = NULL;
      bool top = module_scope && timing != NULL
         && !vlog_has_target(spec) && find_assert_guards(timing, stub, &guard);
      if (top && guard != NULL)
         vlog_visit_only(guard, check_module_scope_cb, &top, V_REF);

      if (top) {
         vlog_set_target(spec, vlog_value(timing));

         if (guard != NULL) {
            // Attempted only when the procedure reaches the statement
            vlog_node_t gated =
               sva_new(vlog_kind(v) == V_COVER ? V_SVA_AND : V_SVA_IMPL_OVER);
            vlog_add_param(gated, guard);
            vlog_add_param(gated, vlog_value(spec));
            vlog_set_loc(gated, vlog_loc(v));
            vlog_set_value(spec, gated);
         }

         vlog_node_t p = p_assertion_process(v);
         if (p != NULL)
            vlog_add_stmt(current_module, p);
         continue;
      }

      static bool warned = false;
      if (!warned) {
         warn_at(vlog_loc(v), "concurrent assertions in this procedure "
                 "are not checked");
         warned = true;
      }
   }

   proc_asserts.count = first;
}

static vlog_node_t p_deferred_immediate_assertion_statement(void)
{
   // assert #0 ( expression ) action_block | assert final ( expression )
   //   action_block | assume ... | cover ...

   BEGIN("immediate assertion statement");

   deferred_inner = NULL;

   vlog_kind_t kind;
   switch (one_of(tASSERT, tASSUME, tCOVER)) {
   case tASSUME: kind = V_ASSUME; break;
   case tCOVER:  kind = V_COVER; break;
   default:      kind = V_ASSERT; break;
   }

   vlog_node_t v = vlog_new(kind);
   vlog_set_subkind(v, V_ASSERT_SIMPLE);

   if (optional(tHASH)) {
      vlog_node_t delay = p_primary();
      if (vlog_kind(delay) != V_NUMBER)
         error_at(vlog_loc(delay), "deferred assertion delay must be 0");
      vlog_set_subkind(v, V_ASSERT_DEFERRED);
   }
   else if (optional(tFINAL))
      vlog_set_subkind(v, V_ASSERT_FINAL);

   consume(tLPAREN);

   vlog_set_value(v, p_expression());

   consume(tRPAREN);

   if (kind == V_COVER) {
      vlog_node_t stmt = p_statement_or_null();
      if (stmt != NULL) {
         vlog_node_t c0 = vlog_new(V_COND);
         vlog_add_stmt(c0, stmt);
         vlog_add_cond(v, c0);
      }
   }
   else
      p_action_block(v);

   vlog_set_loc(v, CURRENT_LOC);

   if (vlog_subkind(v) != V_ASSERT_SIMPLE) {
      return rewrite_deferred_assertion(v);
   }

   return v;
}

static vlog_node_t p_immediate_assertion_statement(void)
{
   // simple_immediate_assertion_statement
   //    | deferred_immediate_assertion_statement

   BEGIN("immediate assertion statement");

   return p_deferred_immediate_assertion_statement();
}

static vlog_node_t p_procedural_assertion_statement(void)
{
   // concurrent_assertion_statement | immediate_assertion_statement
   //   | checker_instantiation

   BEGIN("procedural assertion statement");

   return p_immediate_assertion_statement();
}

// ---------------------------------------------------------------------------
// Random sequences
//
// The productions are parsed into a small tree and the sequence is expanded
// into ordinary statements where every use of a production is a copy of its
// rules, so a recursive production is not supported.

typedef struct _rs_item rs_item_t;

typedef struct {
   vlog_node_t *vals;    // Empty for the default
   int          nvals;
   rs_item_t   *item;
} rs_case_t;

struct _rs_item {
   enum { RS_CALL, RS_CODE, RS_IF, RS_REPEAT, RS_CASE } kind;
   loc_t        loc;
   ident_t      name;
   vlog_node_t *args;
   int          nargs;
   vlog_node_t  code;
   rs_item_t   *a, *b;
   rs_case_t   *cases;
   int          ncases;
};

typedef struct {
   rs_item_t   **items;
   int           nitems;
   vlog_node_t   weight;
   vlog_node_t   wcode;
   bool          join;
} rs_rule_t;

typedef struct {
   ident_t       name;
   loc_t         loc;
   vlog_node_t  *formals;
   int           nformals;
   rs_rule_t    *rules;
   int           nrules;
   ident_t       block;
   bool          active;
} rs_prod_t;

typedef struct {
   rs_prod_t    *prods;
   int           nprods;
   int           id;
} rs_seq_t;

static rs_item_t *p_rs_item(void);

static vlog_node_t p_rs_code_block(void)
{
   // { { data_declaration } { statement_or_null } }

   BEGIN("random sequence code block");

   consume(tLBRACE);

   vlog_node_t blk = vlog_new(V_SEQ_BLOCK);
   vlog_set_loc(blk, &state.last_loc);

   vlog_symtab_push(symtab, blk);

   const int saved_depth = rs_loop_depth;
   rs_loop_depth = 0;

   skip_over_attributes();

   while (scan_block_item_declaration()) {
      p_block_item_declaration(blk);
      skip_over_attributes();
   }

   while (not_at_token(tRBRACE)) {
      vlog_node_t s = p_statement_or_null();
      if (s != NULL)
         vlog_add_stmt(blk, s);
   }

   rs_loop_depth = saved_depth;

   vlog_symtab_pop(symtab);

   consume(tRBRACE);

   vlog_set_loc(blk, CURRENT_LOC);
   return blk;
}

static bool rs_at_item(void)
{
   switch (peek()) {
   case tID:
   case tLBRACE:
   case tIF:
   case tREPEAT:
   case tCASE:
      return true;
   default:
      return false;
   }
}

static rs_item_t *p_rs_production_item(void)
{
   // production_identifier [ ( list_of_arguments ) ]

   BEGIN("production item");

   rs_item_t *it = xcalloc(sizeof(rs_item_t));
   it->kind = RS_CALL;
   it->name = p_identifier();
   it->loc = state.last_loc;

   if (optional(tLPAREN)) {
      vlog_node_t tmp = vlog_new(V_USER_TCALL);
      p_list_of_arguments(tmp);
      consume(tRPAREN);

      it->nargs = vlog_params(tmp);
      it->args = xmalloc_array(MAX(it->nargs, 1), sizeof(vlog_node_t));
      for (int i = 0; i < it->nargs; i++)
         it->args[i] = vlog_param(tmp, i);
   }

   return it;
}

static rs_item_t *p_rs_item(void)
{
   // production_item | rs_code_block | rs_if_else | rs_repeat | rs_case

   BEGIN("random sequence item");

   switch (peek()) {
   case tLBRACE:
      {
         rs_item_t *it = xcalloc(sizeof(rs_item_t));
         it->kind = RS_CODE;
         it->loc = *CURRENT_LOC;
         it->code = p_rs_code_block();
         return it;
      }
   case tIF:
   case tREPEAT:
      {
         rs_item_t *it = xcalloc(sizeof(rs_item_t));
         it->loc = *CURRENT_LOC;
         if (peek() == tIF) {
            consume(tIF);
            it->kind = RS_IF;
         }
         else {
            consume(tREPEAT);
            it->kind = RS_REPEAT;
         }

         it->args = xmalloc(sizeof(vlog_node_t));
         it->nargs = 1;

         consume(tLPAREN);
         it->args[0] = it->kind == RS_IF ? p_cond_predicate() : p_expression();
         consume(tRPAREN);

         it->a = p_rs_item();
         if (it->kind == RS_IF && optional(tELSE))
            it->b = p_rs_item();
         return it;
      }
   case tCASE:
      {
         rs_item_t *it = xcalloc(sizeof(rs_item_t));
         it->kind = RS_CASE;
         it->loc = *CURRENT_LOC;
         consume(tCASE);

         it->args = xmalloc(sizeof(vlog_node_t));
         it->nargs = 1;

         consume(tLPAREN);
         it->args[0] = p_expression();
         consume(tRPAREN);

         while (not_at_token(tENDCASE)) {
            rs_case_t c = { 0 };
            if (optional(tDEFAULT))
               optional(tCOLON);
            else {
               do {
                  c.vals = xrealloc_array(c.vals, c.nvals + 1,
                                          sizeof(vlog_node_t));
                  c.vals[c.nvals++] = p_expression();
               } while (optional(tCOMMA));
               consume(tCOLON);
            }

            c.item = p_rs_item();
            consume(tSEMI);

            it->cases = xrealloc_array(it->cases, it->ncases + 1,
                                       sizeof(rs_case_t));
            it->cases[it->ncases++] = c;
         }

         consume(tENDCASE);
         return it;
      }
   default:
      return p_rs_production_item();
   }
}

static void p_rs_rule(rs_prod_t *prod)
{
   // rs_production_list [ := weight_specification [ rs_code_block ] ]
   //   | rand join [ ( expression ) ] production_item production_item
   //       { production_item }

   BEGIN("random sequence rule");

   rs_rule_t rule = { 0 };

   if (peek() == tRAND && peek_nth(2) == tJOIN) {
      consume(tRAND);
      consume(tJOIN);
      rule.join = true;

      if (optional(tLPAREN)) {
         (void)p_expression();   // The ratio has no effect on atomic items
         consume(tRPAREN);
      }

      while (peek() == tID) {
         rule.items = xrealloc_array(rule.items, rule.nitems + 1,
                                     sizeof(rs_item_t *));
         rule.items[rule.nitems++] = p_rs_production_item();
      }

      if (rule.nitems < 2)
         error_at(&state.last_loc, "rand join requires at least two "
                  "productions");
      else if (rule.nitems > 3)
         error_at(&state.last_loc, "sorry, rand join of more than three "
                  "productions is not supported");
   }
   else {
      while (rs_at_item()) {
         rule.items = xrealloc_array(rule.items, rule.nitems + 1,
                                     sizeof(rs_item_t *));
         rule.items[rule.nitems++] = p_rs_item();
      }
   }

   if (optional(tWALRUS) || (peek() == tCOLON && peek_nth(2) == tEQ
                             && consume(tCOLON) && consume(tEQ))) {
      if (peek() == tLPAREN) {
         consume(tLPAREN);
         rule.weight = p_expression();
         consume(tRPAREN);
      }
      else
         rule.weight = p_primary();

      if (peek() == tLBRACE)
         rule.wcode = p_rs_code_block();
   }

   prod->rules = xrealloc_array(prod->rules, prod->nrules + 1,
                                sizeof(rs_rule_t));
   prod->rules[prod->nrules++] = rule;
}

static void p_rs_production(rs_seq_t *seq)
{
   // [ data_type_or_void ] production_identifier [ ( tf_port_list ) ] :
   //   rs_rule { | rs_rule } ;

   BEGIN("production");

   bool typed = false;
   if (optional(tVOID))
      ;
   else if (peek() != tID || peek_nth(2) == tID) {
      (void)p_data_type();
      typed = true;
   }

   seq->prods = xrealloc_array(seq->prods, seq->nprods + 1,
                               sizeof(rs_prod_t));
   rs_prod_t *prod = &(seq->prods[seq->nprods++]);
   memset(prod, 0, sizeof(rs_prod_t));

   prod->name = p_identifier();
   prod->loc = state.last_loc;
   prod->block = ident_sprintf("$rs%d$%s", seq->id, istr(prod->name));

   if (typed)
      error_at(&prod->loc, "sorry, a production which returns a value is "
               "not supported");

   // The scope holds the arguments of the production
   vlog_node_t scope = vlog_new(V_SEQ_BLOCK);
   vlog_set_loc(scope, &prod->loc);

   if (optional(tLPAREN)) {
      vlog_node_t hdr = vlog_new(V_TASK_DECL);
      vlog_set_ident(hdr, prod->name);
      vlog_set_loc(hdr, &prod->loc);

      vlog_symtab_push(symtab, hdr);
      p_tf_port_list(hdr);
      vlog_symtab_pop(symtab);

      consume(tRPAREN);

      const int nports = vlog_ports(hdr);
      prod->formals = xmalloc_array(MAX(nports, 1), sizeof(vlog_node_t));
      for (int i = 0; i < nports; i++) {
         vlog_node_t port = vlog_port(hdr, i);
         vlog_node_t d = vlog_new(V_LOCAL_DECL);
         vlog_set_ident(d, vlog_ident(port));
         vlog_set_type(d, vlog_type(port));
         vlog_set_loc(d, vlog_loc(port));
         if (vlog_has_value(port))
            vlog_set_value(d, vlog_value(port));
         prod->formals[prod->nformals++] = d;
      }
   }

   consume(tCOLON);

   vlog_symtab_push(symtab, scope);
   for (int i = 0; i < prod->nformals; i++)
      vlog_symtab_put(symtab, prod->formals[i]);

   ident_t saved_return = rs_return_name;
   rs_return_name = prod->block;

   do {
      p_rs_rule(prod);
   } while (optional(tBAR));

   rs_return_name = saved_return;

   vlog_symtab_pop(symtab);

   consume(tSEMI);
}

static vlog_node_t rs_number(int value, const loc_t *loc)
{
   vlog_node_t v = vlog_new(V_NUMBER);
   vlog_set_number(v, number_from_int(value));
   vlog_set_loc(v, loc);
   return v;
}

static vlog_node_t rs_block(const loc_t *loc)
{
   vlog_node_t v = vlog_new(V_SEQ_BLOCK);
   vlog_set_loc(v, loc);
   return v;
}

static vlog_node_t rs_local(const char *prefix, vlog_node_t value,
                            const loc_t *loc)
{
   vlog_node_t d = vlog_new(V_LOCAL_DECL);
   vlog_set_ident(d, ident_uniq(prefix));
   vlog_set_type(d, make_integer_atom_type(DT_INT));
   vlog_set_value(d, value);
   vlog_set_loc(d, loc);
   return d;
}

static vlog_node_t rs_if(vlog_node_t cond, vlog_node_t then,
                         vlog_node_t otherwise, const loc_t *loc)
{
   vlog_node_t v = vlog_new(V_IF);
   vlog_set_loc(v, loc);

   vlog_node_t c0 = vlog_new(V_COND);
   vlog_set_value(c0, cond);
   vlog_set_loc(c0, loc);
   vlog_add_stmt(c0, then);
   vlog_add_cond(v, c0);

   if (otherwise != NULL) {
      vlog_node_t c1 = vlog_new(V_COND);
      vlog_set_loc(c1, loc);
      vlog_add_stmt(c1, otherwise);
      vlog_add_cond(v, c1);
   }

   return v;
}

static vlog_node_t rs_expand_prod(rs_seq_t *seq, rs_prod_t *prod,
                                  vlog_node_t *args, int nargs,
                                  const loc_t *loc);

static vlog_node_t rs_expand_item(rs_seq_t *seq, rs_item_t *it)
{
   switch (it->kind) {
   case RS_CODE:
      return it->code;

   case RS_CALL:
      {
         rs_prod_t *prod = NULL;
         for (int i = 0; i < seq->nprods; i++) {
            if (seq->prods[i].name == it->name)
               prod = &(seq->prods[i]);
         }

         if (prod == NULL) {
            error_at(&it->loc, "no production named %pQ", it->name);
            return rs_block(&it->loc);
         }
         else if (prod->active) {
            error_at(&it->loc, "sorry, the recursive production %pQ is not "
                     "supported", it->name);
            return rs_block(&it->loc);
         }

         return rs_expand_prod(seq, prod, it->args, it->nargs, &it->loc);
      }

   case RS_IF:
      {
         vlog_node_t then = rs_expand_item(seq, it->a);
         vlog_node_t otherwise = it->b ? rs_expand_item(seq, it->b) : NULL;
         return rs_if(it->args[0], then, otherwise, &it->loc);
      }

   case RS_REPEAT:
      {
         vlog_node_t v = vlog_new(V_REPEAT);
         vlog_set_value(v, it->args[0]);
         vlog_add_stmt(v, rs_expand_item(seq, it->a));
         vlog_set_loc(v, &it->loc);
         return v;
      }

   case RS_CASE:
      {
         // The expression is evaluated once
         vlog_node_t blk = rs_block(&it->loc);
         vlog_node_t sel = rs_local("rscase", it->args[0], &it->loc);
         vlog_add_decl(blk, sel);

         vlog_node_t dflt = NULL;
         vlog_node_t chain = NULL;
         for (int i = it->ncases - 1; i >= 0; i--) {
            rs_case_t *c = &(it->cases[i]);
            vlog_node_t body = rs_expand_item(seq, c->item);

            if (c->nvals == 0) {
               dflt = body;
               continue;
            }

            vlog_node_t cond = NULL;
            for (int j = 0; j < c->nvals; j++) {
               vlog_node_t eq = vlog_new(V_BINARY);
               vlog_set_subkind(eq, V_BINARY_CASE_EQ);
               vlog_set_left(eq, foreach_ref(sel, &it->loc));
               vlog_set_right(eq, c->vals[j]);
               vlog_set_loc(eq, &it->loc);

               if (cond == NULL)
                  cond = eq;
               else {
                  vlog_node_t o = vlog_new(V_BINARY);
                  vlog_set_subkind(o, V_BINARY_LOG_OR);
                  vlog_set_left(o, cond);
                  vlog_set_right(o, eq);
                  vlog_set_loc(o, &it->loc);
                  cond = o;
               }
            }

            chain = rs_if(cond, body, chain ?: dflt, &it->loc);
         }

         if (chain != NULL)
            vlog_add_stmt(blk, chain);
         else if (dflt != NULL)
            vlog_add_stmt(blk, dflt);

         return blk;
      }
   }

   should_not_reach_here();
}

static vlog_node_t rs_expand_rule(rs_seq_t *seq, rs_rule_t *rule,
                                  const loc_t *loc)
{
   vlog_node_t blk = rs_block(loc);

   if (!rule->join || rule->nitems < 2) {
      for (int i = 0; i < rule->nitems; i++)
         vlog_add_stmt(blk, rs_expand_item(seq, rule->items[i]));
   }
   else {
      // The items are atomic so any order is a legal interleaving
      static const int perms[6][3] = {
         { 0, 1, 2 }, { 0, 2, 1 }, { 1, 0, 2 },
         { 1, 2, 0 }, { 2, 0, 1 }, { 2, 1, 0 },
      };

      const int n = MIN(rule->nitems, 3);
      const int nperm = n == 2 ? 2 : 6;

      vlog_node_t *body = xmalloc_array(n, sizeof(vlog_node_t));
      for (int i = 0; i < n; i++)
         body[i] = rs_expand_item(seq, rule->items[i]);

      vlog_node_t call = vlog_new(V_SYS_FCALL);
      vlog_set_ident(call, ident_new("$urandom_range"));
      vlog_set_subkind(call, V_SYSTF_NONE);
      vlog_set_ival(call, time_ival());
      vlog_add_param(call, rs_number(nperm - 1, loc));
      vlog_add_param(call, rs_number(0, loc));
      vlog_set_loc(call, loc);

      vlog_node_t sel = rs_local("rsjoin", call, loc);
      vlog_add_decl(blk, sel);

      vlog_node_t chain = NULL;
      for (int p = nperm - 1; p >= 0; p--) {
         vlog_node_t order = rs_block(loc);
         for (int i = 0; i < n; i++) {
            // Order of two items is a swap of the first two permutations
            const int idx = n == 2 ? (p == 0 ? i : 1 - i) : perms[p][i];
            vlog_add_stmt(order, body[idx]);
         }

         if (chain == NULL)
            chain = order;
         else {
            vlog_node_t eq = vlog_new(V_BINARY);
            vlog_set_subkind(eq, V_BINARY_CASE_EQ);
            vlog_set_left(eq, foreach_ref(sel, loc));
            vlog_set_right(eq, rs_number(p, loc));
            vlog_set_loc(eq, loc);
            chain = rs_if(eq, order, chain, loc);
         }
      }

      free(body);
      vlog_add_stmt(blk, chain);
   }

   if (rule->wcode != NULL)
      vlog_add_stmt(blk, rule->wcode);

   return blk;
}

static vlog_node_t rs_expand_prod(rs_seq_t *seq, rs_prod_t *prod,
                                  vlog_node_t *args, int nargs,
                                  const loc_t *loc)
{
   vlog_node_t blk = rs_block(loc);
   vlog_set_ident(blk, prod->block);

   if (nargs > prod->nformals)
      error_at(loc, "too many arguments for production %pQ", prod->name);

   // The code of the production refers to the declaration of each argument
   // which is shared by every use so the value is assigned in the block
   for (int i = 0; i < prod->nformals; i++) {
      vlog_node_t f = prod->formals[i];
      vlog_add_decl(blk, f);

      if (i < nargs) {
         vlog_node_t a = vlog_new(V_BASSIGN);
         vlog_set_target(a, foreach_ref(f, loc));
         vlog_set_value(a, args[i]);
         vlog_set_loc(a, loc);
         vlog_add_stmt(blk, a);
      }
      else if (!vlog_has_value(f))
         error_at(loc, "missing argument for %pQ of production %pQ",
                  vlog_ident(f), prod->name);
   }

   prod->active = true;

   if (prod->nrules == 1)
      vlog_add_stmt(blk, rs_expand_rule(seq, &(prod->rules[0]), loc));
   else if (prod->nrules > 1) {
      // Pick a rule at random with the probability given by its weight
      vlog_node_t total = NULL;
      vlog_node_t *cum = xmalloc_array(prod->nrules, sizeof(vlog_node_t));
      for (int i = 0; i < prod->nrules; i++) {
         vlog_node_t w = prod->rules[i].weight ?: rs_number(1, loc);
         if (total == NULL)
            total = w;
         else {
            vlog_node_t sum = vlog_new(V_BINARY);
            vlog_set_subkind(sum, V_BINARY_PLUS);
            vlog_set_left(sum, total);
            vlog_set_right(sum, w);
            vlog_set_loc(sum, loc);
            total = sum;
         }

         cum[i] = total;
      }

      vlog_node_t max = vlog_new(V_BINARY);
      vlog_set_subkind(max, V_BINARY_MINUS);
      vlog_set_left(max, total);
      vlog_set_right(max, rs_number(1, loc));
      vlog_set_loc(max, loc);

      vlog_node_t call = vlog_new(V_SYS_FCALL);
      vlog_set_ident(call, ident_new("$urandom_range"));
      vlog_set_subkind(call, V_SYSTF_NONE);
      vlog_set_ival(call, time_ival());
      vlog_add_param(call, max);
      vlog_add_param(call, rs_number(0, loc));
      vlog_set_loc(call, loc);

      vlog_node_t sel = rs_local("rssel", call, loc);
      vlog_add_decl(blk, sel);

      vlog_node_t chain = NULL;
      for (int i = prod->nrules - 1; i >= 0; i--) {
         vlog_node_t body = rs_expand_rule(seq, &(prod->rules[i]), loc);
         if (chain == NULL)
            chain = body;
         else {
            vlog_node_t lt = vlog_new(V_BINARY);
            vlog_set_subkind(lt, V_BINARY_LT);
            vlog_set_left(lt, foreach_ref(sel, loc));
            vlog_set_right(lt, cum[i]);
            vlog_set_loc(lt, loc);
            chain = rs_if(lt, body, chain, loc);
         }
      }

      free(cum);
      vlog_add_stmt(blk, chain);
   }

   prod->active = false;
   return blk;
}

static vlog_node_t p_randsequence_statement(void)
{
   // randsequence ( [ production_identifier ] ) production { production }
   //   endsequence

   BEGIN("randsequence statement");

   static int next_id = 0;

   consume(tRANDSEQUENCE);
   consume(tLPAREN);

   ident_t start = NULL;
   if (peek() == tID)
      start = p_identifier();
   const loc_t start_loc = state.last_loc;

   consume(tRPAREN);

   rs_seq_t seq = { .id = next_id++ };

   ident_t saved_break = rs_break_name;
   const int saved_depth = rs_loop_depth;
   rs_break_name = ident_sprintf("$rs%d$break", seq.id);

   do {
      p_rs_production(&seq);
   } while (not_at_token(tENDSEQUENCE));

   consume(tENDSEQUENCE);

   rs_break_name = saved_break;
   rs_loop_depth = saved_depth;

   // The block which break leaves
   vlog_node_t top = rs_block(CURRENT_LOC);
   vlog_set_ident(top, ident_sprintf("$rs%d$break", seq.id));

   rs_prod_t *entry = NULL;
   for (int i = 0; i < seq.nprods; i++) {
      if (start == NULL || seq.prods[i].name == start) {
         entry = &(seq.prods[i]);
         break;
      }
   }

   if (entry == NULL)
      error_at(&start_loc, "no production named %pQ", start);
   else
      vlog_add_stmt(top, rs_expand_prod(&seq, entry, NULL, 0, &start_loc));

   for (int i = 0; i < seq.nprods; i++) {
      free(seq.prods[i].formals);
      free(seq.prods[i].rules);
   }
   free(seq.prods);

   return top;
}

static vlog_node_t p_cycle_delay_statement(void)
{
   // ## integral_number | ## identifier | ## ( expression )

   BEGIN("cycle delay statement");

   consume(tCYCLEDLY);

   vlog_node_t count;
   if (optional(tLPAREN)) {
      count = p_expression();
      consume(tRPAREN);
   }
   else
      count = p_delay_value();

   vlog_node_t ctrl = default_cycle != NULL ? default_cycle : default_clock;
   if (ctrl == NULL) {
      error_at(&state.last_loc, "cycle delay requires a default clocking in "
               "this scope");
      ctrl = vlog_new(V_EVENT_CONTROL);
   }

   vlog_node_t wait = vlog_new(V_TIMING);
   vlog_set_value(wait, vlog_copy(ctrl, vlog_always_true_cb, NULL));
   vlog_set_loc(wait, CURRENT_LOC);

   vlog_node_t rpt = vlog_new(V_REPEAT);
   vlog_set_value(rpt, count);
   vlog_add_stmt(rpt, wait);
   vlog_set_loc(rpt, CURRENT_LOC);

   vlog_node_t blk = vlog_new(V_SEQ_BLOCK);
   vlog_add_stmt(blk, rpt);

   vlog_node_t s = p_statement_or_null();
   if (s != NULL)
      vlog_add_stmt(blk, s);

   vlog_set_loc(blk, CURRENT_LOC);
   return blk;
}

static vlog_node_t p_statement_item(ident_t id)
{
   // blocking_assignment ; | nonblocking_assignment ;
   //   | procedural_continuous_assignment ; | case_statement
   //   | conditional_statement | inc_or_dec_expression ;
   //   | subroutine_call_statement | disable_statement
   //   | event_trigger | loop_statement | jump_statement
   //   | par_block | procedural_timing_control_statement
   //   | seq_block | wait_statement | procedural_assertion_statement
   //   | clocking_drive ; | randsequence_statement | randcase_statement
   //   | expect_property_statement

   BEGIN("statement item");

   switch (peek()) {
   case tTHIS:
   case tSUPER:
      if (peek() == tSUPER && peek_nth(3) == tNEW) {
         consume(tSUPER);
         consume(tDOT);
         consume(tNEW);

         vlog_node_t v = vlog_new(V_SUPER_CALL);
         if (optional(tLPAREN)) {
            p_list_of_arguments(v);
            consume(tRPAREN);
         }
         consume(tSEMI);

         vlog_set_loc(v, CURRENT_LOC);
         return v;
      }
      // Fall-through
   case tID:
      if (at_checker_call())
         return p_checker_call();

      if (is_std_randomize())
         return p_subroutine_call_statement();

      if (is_process_name() && peek_nth(2) == tSCOPE) {
         vlog_node_t v = p_process_scope();
         consume(tSEMI);
         return v;
      }

      if (peek_nth(2) == tSCOPE || is_specialised_scope())
         qualify_scope = p_scope_prefix();

      switch (peek_nth(2)) {
      case tLPAREN:
      case tATTRBEGIN:
         return p_subroutine_call_statement();
      case tSEMI:
         if (peek() == tID)
            return p_subroutine_call_statement();
         // Fall-through
      default:
         {
            vlog_node_t lhs = p_variable_lvalue(), v;

            if (peek() == tSEMI && vlog_kind(lhs) == V_METHOD_CALL) {
               consume(tSEMI);
               return lhs;
            }
            else if (peek() == tSEMI && vlog_kind(lhs) == V_HIER_REF) {
               // Call of a subroutine by hierarchical name without
               // parentheses
               consume(tSEMI);

               vlog_node_t call = vlog_new(V_METHOD_CALL);
               vlog_set_value(call, lhs);
               vlog_set_ident(call, vlog_ident(lhs));
               vlog_set_loc(call, vlog_loc(lhs));
               vlog_set_global_flags(call, VLOG_GF_HIER_REF);
               return call;
            }
            else if (peek() == tSEMI && vlog_kind(lhs) == V_MEMBER_REF) {
               // Method call without parentheses
               consume(tSEMI);

               vlog_node_t call = vlog_new(V_METHOD_CALL);
               vlog_set_value(call, vlog_value(lhs));
               vlog_set_ident(call, vlog_ident(lhs));
               vlog_set_loc(call, vlog_loc(lhs));
               vlog_symtab_lookup(symtab, call);
               return call;
            }

            switch (peek()) {
            case tLE:
               v = p_nonblocking_assignment(lhs);
               break;
            case tPLUSPLUS:
            case tMINUSMINUS:
               v = p_inc_or_dec_expression(lhs);
               break;
            default:
               v = p_blocking_assignment(lhs);
               break;
            }

            consume(tSEMI);
            return v;
         }
      }
   case tPLUSPLUS:
   case tMINUSMINUS:
      {
         vlog_node_t v = p_inc_or_dec_expression(NULL);
         consume(tSEMI);
         return v;
      }
   case tLBRACE:
      {
         vlog_node_t lhs = p_variable_lvalue(), v;

         if (peek() == tLE)
            v = p_nonblocking_assignment(lhs);
         else
            v = p_blocking_assignment(lhs);

         consume(tSEMI);
         return v;
      }
   case tDISABLE:
      return p_disable_statement();
   case tAT:
   case tHASH:
      return p_procedural_timing_control_statement();
   case tCYCLEDLY:
      return p_cycle_delay_statement();
   case tBEGIN:
      return p_seq_block(id);
   case tFORK:
      return p_par_block(id);
   case tSYSTASK:
   case tVOID:
      return p_subroutine_call_statement();
   case tIF:
      return p_conditional_statement();
   case tFOREVER:
   case tWHILE:
   case tREPEAT:
   case tDO:
   case tFOR:
   case tFOREACH:
      return p_loop_statement();
   case tWAIT:
      return p_wait_statement();
   case tCASE:
   case tCASEX:
   case tCASEZ:
      return p_case_statement();
   case tIFIMPL:
      return p_event_trigger();
   case tUNIQUE:
   case tUNIQUE0:
   case tPRIORITY:
      {
         vlog_flags_t flags;
         switch (peek()) {
         case tUNIQUE:  flags = VLOG_F_UNIQUE; break;
         case tUNIQUE0: flags = VLOG_F_UNIQUE0; break;
         default:       flags = VLOG_F_PRIORITY; break;
         }

         drop_token(&state);

         vlog_node_t v;
         switch (peek()) {
         case tIF:
            v = p_conditional_statement();
            break;
         case tCASE:
         case tCASEX:
         case tCASEZ:
            v = p_case_statement();
            break;
         default:
            one_of(tIF, tCASE, tCASEX, tCASEZ);
            drop_tokens_until(&state, tSEMI);
            return vlog_new(V_SEQ_BLOCK);  // Dummy statement
         }

         vlog_set_flags(v, flags);
         return v;
      }
   case tRANDCASE:
      return p_randcase_statement();
   case tRANDSEQUENCE:
      return p_randsequence_statement();
   case tASSIGN:
   case tDEASSIGN:
   case tFORCE:
   case tRELEASE:
      {
         vlog_node_t v = p_procedural_continuous_assignment();
         consume(tSEMI);
         return v;
      }
   case tRETURN:
   case tBREAK:
   case tCONTINUE:
      return p_jump_statement();
   case tEXPECT:
      return p_expect_statement();
   case tASSERT:
   case tASSUME:
   case tCOVER:
      if (peek_nth(2) == tPROPERTY || peek_nth(2) == tSEQUENCE)
         return p_procedural_concurrent_assertion();
      if (id != NULL)
         note_block_name(id);
      deferred_label = id;
      return p_procedural_assertion_statement();
   default:
      one_of(tID, tAT, tHASH, tBEGIN, tFORK, tSYSTASK, tVOID, tIF, tFOREVER,
             tWHILE, tREPEAT, tDO, tFOR, tWAIT, tCASE, tCASEX, tCASEZ, tIFIMPL,
             tASSIGN, tDEASSIGN, tFORCE, tRELEASE, tRETURN, tASSERT, tBREAK,
             tCONTINUE, tTHIS, tSUPER, tRANDCASE, tFOREACH, tUNIQUE,
             tUNIQUE0, tPRIORITY);
      drop_tokens_until(&state, tSEMI);
      return vlog_new(V_SEQ_BLOCK);  // Dummy statement
   }
}

static vlog_node_t p_statement(void)
{
   // [ block_identifier : ] { attribute_instance } statement_item

   BEGIN("statement");

   ident_t id = NULL;
   if (peek() == tID && peek_nth(2) == tCOLON) {
      id = p_identifier();
      consume(tCOLON);
   }

   optional_attributes();

   // A label on a statement other than a block names the statement so that
   // a disable can leave it: treat as a block holding only that statement
   bool wrap = false;
   switch (peek()) {
   case tIF:
   case tFOREVER:
   case tWHILE:
   case tREPEAT:
   case tDO:
   case tFOR:
   case tFOREACH:
   case tCASE:
   case tCASEX:
   case tCASEZ:
   case tUNIQUE:
   case tUNIQUE0:
   case tPRIORITY:
      wrap = id != NULL;
      break;
   default:
      break;
   }

   const int depth = match_depth;
   vlog_node_t v = p_statement_item(id);
   match_unwind(depth);

   if (wrap && v != NULL) {
      vlog_node_t b = vlog_new(V_SEQ_BLOCK);
      vlog_set_ident(b, id);
      vlog_set_loc(b, vlog_loc(v));
      vlog_add_stmt(b, v);
      vlog_symtab_put(symtab, b);
      note_block_name(id);
      return b;
   }

   return v;
}

static vlog_node_t p_statement_or_null(void)
{
   // statement | { attribute_instance } ;

   BEGIN("statement or null");

   if (optional(tSEMI))
      return NULL;
   else
      return p_statement();
}

static vlog_node_t p_always_construct(void)
{
   // always_keyword statement

   BEGIN("always construct");

   vlog_node_t v = vlog_new(V_ALWAYS);

   switch (one_of(tALWAYS, tALWAYSCOMB, tALWAYSFF, tALWAYSLATCH)) {
   case tALWAYSCOMB:  vlog_set_subkind(v, V_ALWAYS_COMB);  break;
   case tALWAYSFF:    vlog_set_subkind(v, V_ALWAYS_FF);    break;
   case tALWAYSLATCH: vlog_set_subkind(v, V_ALWAYS_LATCH); break;
   default:           vlog_set_subkind(v, V_ALWAYS_PLAIN); break;
   }

   vlog_set_ident(v, default_label("always"));

   const int first = proc_asserts.count;
   always_depth++;
   vlog_add_stmt(v, p_statement());
   always_depth--;
   hoist_procedural_assertions(v, first);

   vlog_set_loc(v, CURRENT_LOC);
   return v;
}

static vlog_node_t p_final_construct(void)
{
   // final function_statement

   BEGIN("final construct");

   consume(tFINAL);

   vlog_node_t v = vlog_new(V_FINAL);
   vlog_set_ident(v, default_label("final"));
   vlog_add_stmt(v, p_statement());

   vlog_set_loc(v, CURRENT_LOC);
   return v;
}

static vlog_node_t p_initial_construct(void)
{
   // initial statement_or_null

   BEGIN("initial construct");

   consume(tINITIAL);

   vlog_node_t v = vlog_new(V_INITIAL);
   vlog_set_ident(v, default_label("initial"));

   vlog_node_t s = p_statement_or_null();
   if (s != NULL)
      vlog_add_stmt(v, s);

   vlog_set_loc(v, CURRENT_LOC);
   return v;
}

static vlog_node_t p_net_assignment(vlog_node_t delay, vlog_node_t strength)
{
   // net_lvalue = expression

   BEGIN("net assignment");

   vlog_symtab_set_implicit(symtab, implicit_kind);

   vlog_node_t v = vlog_new(V_ASSIGN);
   vlog_set_target(v, user_net_driver(p_net_lvalue()));
   vlog_set_ident(v, default_label("assign"));
   vlog_set_delay(v, delay);
   if (strength != NULL)
      vlog_add_param(v, strength);

   vlog_symtab_set_implicit(symtab, V_NET_NONE);

   consume(tEQ);

   vlog_set_value(v, p_expression_of_type(pattern_type_of(vlog_target(v))));

   vlog_set_loc(v, CURRENT_LOC);
   return v;
}

static void p_list_of_net_assignments(vlog_node_t mod, vlog_node_t delay,
                                      vlog_node_t strength)
{
   // net_assignment { , net_assignment }

   BEGIN("list of net assignments");

   do {
      vlog_add_stmt(mod, p_net_assignment(delay, strength));
   } while (optional(tCOMMA));
}

static void p_continuous_assign(vlog_node_t mod)
{
   // assign [ drive_strength ] [ delay3 ] list_of_net_assignments ;
   //   | assign [ delay_control ] list_of_variable_assignments ;

   BEGIN("continuous assignment");

   consume(tASSIGN);

   vlog_node_t delay = NULL;
   vlog_node_t strength = NULL;
   if (peek() == tLPAREN) {
      strength = p_drive_strength();
      if (peek() == tHASH)
         delay = p_delay3();
   } else if (peek() == tHASH)
      delay = p_delay_control();

   p_list_of_net_assignments(mod, delay, strength);

   consume(tSEMI);
}

static vlog_net_kind_t p_net_type(void)
{
   // supply0 | supply1 | tri | triand | trior | trireg | tri0
   //   | tri1 | uwire | wire | wand | wor

   BEGIN("net type");

   switch (one_of(tWIRE, tUWIRE, tSUPPLY0, tSUPPLY1, tTRI, tTRIAND,
                  tTRIOR, tTRIREG, tTRI0, tTRI1, tWAND, tWOR)) {
   case tSUPPLY0: return V_NET_SUPPLY0;
   case tSUPPLY1: return V_NET_SUPPLY1;
   case tTRI:     return V_NET_TRI;
   case tTRIAND:  return V_NET_TRIAND;
   case tTRIOR:   return V_NET_TRIOR;
   case tTRIREG:  return V_NET_TRIREG;
   case tTRI0:    return V_NET_TRI0;
   case tTRI1:    return V_NET_TRI1;
   case tUWIRE:   return V_NET_UWIRE;
   case tWAND:    return V_NET_WAND;
   case tWOR:     return V_NET_WOR;
   case tWIRE:
   default:       return V_NET_WIRE;
   }
}

static vlog_node_t p_net_decl_assignment(vlog_net_kind_t kind,
                                         vlog_node_t datatype,
                                         vlog_node_t strength,
                                         vlog_node_t *array_init)
{
   // net_identifier { unpacked_dimension } [ = expression ]

   BEGIN("net declaration assignment");

   // The members of an unpacked structure are kept in memory so the net
   // is a variable that the ports and assignments connected to it drive
   vlog_node_t sdecl = vlog_get_type(datatype);
   const bool is_struct = sdecl != NULL
      && (vlog_kind(sdecl) == V_STRUCT_DECL || vlog_kind(sdecl) == V_UNION_DECL)
      && !(vlog_flags(sdecl) & VLOG_F_PACKED);

   vlog_node_t v = vlog_new(is_struct ? V_VAR_DECL : V_NET_DECL);
   if (!is_struct)
      vlog_set_subkind(v, kind);
   vlog_set_type(v, datatype);
   vlog_set_ident(v, p_identifier());

   while (peek() == tLSQUARE)
      vlog_add_range(v, p_unpacked_dimension());

   if (optional(tEQ)) {
      vlog_node_t init;
      if (peek() == tTICK && peek_nth(2) == tLBRACE
          && vlog_get_type(datatype) != NULL)
         init = p_assignment_pattern_expression(vlog_get_type(datatype));
      else
         init = p_expression();

      if (vlog_kind(v) == V_NET_DECL && vlog_ranges(v) > 0) {
         // The value of an array of nets is a continuous assignment to the
         // whole array
         *array_init = init;
      }
      else {
         vlog_set_value(v, init);

         if (strength != NULL && vlog_kind(v) == V_NET_DECL)
            vlog_add_param(v, strength);
      }
   }

   vlog_set_loc(v, CURRENT_LOC);
   vlog_symtab_put(symtab, v);
   return v;
}

static void p_list_of_net_decl_assignments(vlog_node_t mod,
                                           vlog_net_kind_t kind,
                                           vlog_node_t datatype,
                                           vlog_node_t delay,
                                           vlog_node_t strength)
{
   // net_decl_assignment { , net_decl_assignment }

   BEGIN("list of net declaration assignments");

   do {
      vlog_node_t init = NULL;
      vlog_node_t v = p_net_decl_assignment(kind, datatype, strength, &init);
      if (init == NULL && delay != NULL)
         vlog_set_delay(v, delay);
      vlog_add_decl(mod, v);

      if (init != NULL) {
         vlog_node_t target = vlog_new(V_REF);
         vlog_set_ident(target, vlog_ident(v));
         vlog_set_ref(target, v);
         vlog_set_loc(target, vlog_loc(v));

         vlog_node_t a = vlog_new(V_ASSIGN);
         vlog_set_target(a, target);
         vlog_set_ident(a, default_label("assign"));
         if (delay != NULL)
            vlog_set_delay(a, delay);
         if (strength != NULL)
            vlog_add_param(a, strength);
         vlog_set_value(a, init);
         vlog_set_loc(a, vlog_loc(v));
         vlog_add_stmt(mod, a);
      }
   } while (optional(tCOMMA));
}

static vlog_node_t p_drive_strength(void)
{
   //( strength0 , strength1 ) | ( strength1 , strength0 )
   //| ( strength0 , highz1 )  | ( strength1 , highz0 )
   //| ( highz0 , strength1 )  | ( highz1 , strength0 )

   BEGIN("drive strength");

   consume(tLPAREN);

   vlog_strength_t s0, s1;
   switch (peek()) {
   case tSUPPLY0:
   case tSTRONG0:
   case tPULL0:
   case tWEAK0:
      {
         s0 = p_strength0();
         consume(tCOMMA);
         if (optional(tHIGHZ1))
            s1 = V_STRENGTH_HIGHZ;
         else
            s1 = p_strength1();
      }
      break;
   case tHIGHZ0:
      {
         s0 = V_STRENGTH_HIGHZ;
         consume(tHIGHZ0);
         consume(tCOMMA);
         s1 = p_strength1();
      }
      break;
   case tSUPPLY1:
   case tSTRONG1:
   case tPULL1:
   case tWEAK1:
      {
         s1 = p_strength1();
         consume(tCOMMA);
         if (optional(tHIGHZ0))
            s0 = V_STRENGTH_HIGHZ;
         else
            s0 = p_strength0();
      }
      break;
   case tHIGHZ1:
      {
         s1 = V_STRENGTH_HIGHZ;
         consume(tHIGHZ1);
         consume(tCOMMA);
         s0 = p_strength0();
      }
      break;
   default:
      should_not_reach_here();
   }

   consume(tRPAREN);

   vlog_node_t v = vlog_new(V_STRENGTH);
   vlog_set_subkind(v, MAKE_STRENGTH(s0, s1));
   vlog_set_loc(v, CURRENT_LOC);

   return v;
}

static vlog_strength_t p_charge_strength(void)
{
   // ( small ) | ( medium ) | ( large )

   BEGIN("drive charge");

   consume(tLPAREN);

   vlog_strength_t s;
   switch (one_of(tSMALL, tMEDIUM, tLARGE)) {
   default:
   case tSMALL:  s = V_STRENGTH_SMALL;
   case tMEDIUM: s = V_STRENGTH_MEDIUM;
   case tLARGE:  s = V_STRENGTH_LARGE;
   }

   consume(tRPAREN);

   return s;
}

static void p_net_declaration(vlog_node_t mod)
{
   // net_type [ drive_strength | charge_strength ] [ vectored | scalared ]
   //     data_type_or_implicit [ delay3 ] list_of_net_decl_assignments ;
   //  | net_type_identifier [ delay_control ] list_of_net_decl_assignments ;
   //  | interconnect implicit_data_type [ # delay_value ] net_identifier
   //     { unpacked_dimension } [ , net_identifier { unpacked_dimension } ] ;

   BEGIN("net declaration");

   ident_t id;
   vlog_node_t dt, strength = NULL;
   vlog_net_kind_t kind = V_NET_WIRE;

   switch (peek()) {
   case tINTERCONNECT:
      {
         consume(tINTERCONNECT);

         dt = p_implicit_data_type();
         vlog_set_flags(dt, vlog_flags(dt) | VLOG_F_USERNET);

         if (optional(tHASH))
            p_delay_value();

         p_list_of_net_decl_assignments(mod, V_NET_WIRE, dt, NULL, NULL);
      }
      break;

   case tID:
      {
         id = p_identifier();
         dt = vlog_symtab_query(symtab, id);
         if (dt == NULL)
            should_not_reach_here();
         // TODO check that the identifier is actually
         // a user declared nettype

         if (peek() == tHASH)
            p_delay_control();

         p_list_of_net_decl_assignments(mod, kind, dt, NULL, NULL);
      }
      break;

   default:
      {
         kind = p_net_type();

         if (peek() == tLPAREN) {
            switch (peek_nth(2)) {
            case tHIGHZ0:
            case tHIGHZ1:
            case tSUPPLY0:
            case tSUPPLY1:
            case tSTRONG0:
            case tSTRONG1:
            case tPULL0:
            case tPULL1:
            case tWEAK0:
            case tWEAK1:
               strength = p_drive_strength();
               break;
            case tSMALL:
            case tMEDIUM:
            case tLARGE:
               if (kind != V_NET_TRIREG)
                  parse_error(&state.last_loc, "charge strength only allowed "
                              "with the trireg keyword");
               p_charge_strength();
               break;
            default:
               one_of(tHIGHZ0, tHIGHZ1, tSUPPLY0, tSUPPLY1, tSTRONG0, tSTRONG1,
                     tPULL0, tPULL1, tWEAK0, tWEAK1, tSMALL, tMEDIUM, tLARGE);
               drop_tokens_until(&state, tSEMI);
               return;
            }
         }

         bool need_packed = false;
         if (optional(tVECTORED) || optional(tSCALARED))
            need_packed = true;

         dt = p_data_type_or_implicit();

         if (need_packed) {
            bool has_packed = false;
            unsigned ranges = vlog_ranges(dt);
            for (unsigned i = 0; i < ranges; i++) {
               vlog_node_t r = vlog_range(dt, i);
               if (vlog_subkind(r) == V_DIM_PACKED)
                  has_packed |= true;
            }
            if (!has_packed)
               parse_error(&state.last_loc, "vectored and scalared keywords "
                           "are only allowed with at least a packed dimension");
         }

         vlog_node_t delay = NULL;
         if (peek() == tHASH)
            delay = p_delay3();

         p_list_of_net_decl_assignments(mod, kind, dt, delay, strength);
      }
   }

   consume(tSEMI);
}

static vlog_node_t p_queue_dimension(void)
{
   // [ $ [ : constant_expression ] ]

   BEGIN("queue dimension");

   consume(tLSQUARE);
   consume(tDOLLAR);

   if (optional(tCOLON))
      (void)p_constant_expression();

   consume(tRSQUARE);

   vlog_node_t v = vlog_new(V_DIMENSION);
   vlog_set_subkind(v, V_DIM_UNSIZED);
   vlog_set_flags(v, VLOG_F_PACKED);   // Marks a queue
   vlog_set_loc(v, CURRENT_LOC);

   return v;
}

static vlog_node_t p_unsized_dimension(void)
{
   // [ ]

   BEGIN("unsized dimension");

   consume(tLSQUARE);
   consume(tRSQUARE);

   vlog_node_t v = vlog_new(V_DIMENSION);
   vlog_set_subkind(v, V_DIM_UNSIZED);
   vlog_set_loc(v, CURRENT_LOC);

   return v;
}

static bool is_associative_dimension(void)
{
   // [ * ] or [ data_type ]

   assert(peek() == tLSQUARE);

   switch (peek_nth(2)) {
   case tTIMES:
      return peek_nth(3) == tRSQUARE;
   case tBIT:
   case tLOGIC:
   case tREG:
   case tBYTE:
   case tSHORTINT:
   case tSVINT:
   case tLONGINT:
   case tINTEGER:
   case tTIME:
   case tSVREAL:
   case tREALTIME:
   case tSHORTREAL:
   case tSTRUCT:
   case tUNION:
   case tENUM:
   case tSTRINGK: case tCHANDLE:
   case tEVENT:
      return true;
   case tID:
      {
         if (peek_nth(3) == tSCOPE && peek_nth(4) == tID
             && peek_nth(5) == tRSQUARE) {
            // A type declared in a package or class
            vlog_node_t scope = vlog_symtab_query(symtab, peek_ident_nth(2));
            if (scope == NULL)
               scope = package_by_name(peek_ident_nth(2));
            else if (vlog_kind(scope) != V_CLASS_DECL)
               scope = NULL;
            vlog_node_t d = scope ? vlog_scope_member(scope, peek_ident_nth(4))
               : NULL;
            return d != NULL && is_data_type(d);
         }

         // A name which is a type rather than a constant expression
         if (peek_nth(3) != tRSQUARE && peek_nth(3) != tLSQUARE)
            return false;

         vlog_node_t d = vlog_symtab_query(symtab, peek_ident_nth(2));
         return d != NULL && is_data_type(d);
      }
   default:
      return false;
   }
}

static vlog_node_t p_associative_dimension(void)
{
   // [ data_type ] | [ * ]

   BEGIN("associative dimension");

   consume(tLSQUARE);

   vlog_node_t v = vlog_new(V_DIMENSION);
   vlog_set_subkind(v, V_DIM_ASSOC);

   if (optional(tTIMES))
      vlog_set_flags(v, VLOG_F_WILDCARD);
   else {
      vlog_node_t key = p_data_type();
      vlog_node_t kt = vlog_get_type(key);

      bool ok = false;
      if (kt != NULL) {
         switch (vlog_kind(kt)) {
         case V_ENUM_DECL:
            ok = true;
            break;
         case V_CLASS_DECL:
            ok = true;
            break;
         case V_STRUCT_DECL:
         case V_UNION_DECL:
            ok = !!(vlog_flags(kt) & VLOG_F_PACKED)
               || vlog_kind(kt) == V_STRUCT_DECL;
            if (!ok) {
               error_at(&state.last_loc, "sorry, an unpacked union as "
                        "the index of an associative array is not supported");
               ok = true;
            }
            break;
         case V_DATA_TYPE:
            switch (vlog_subkind(kt)) {
            case DT_REAL:
            case DT_SHORTREAL:
            case DT_REALTIME:
            case DT_EVENT:
            case DT_DYNARRAY:
            case DT_QUEUE:
            case DT_MAILBOX:
            case DT_SEMAPHORE:
            case DT_VIRTUAL:
            case DT_ASSOC:
               break;
            default:
               ok = true;
               break;
            }
            break;
         default:
            break;
         }
      }

      if (!ok)
         parse_error(&state.last_loc, "the index of an associative array "
                     "must be an integral type or string");

      vlog_set_left(v, key);
   }

   consume(tRSQUARE);

   vlog_set_loc(v, CURRENT_LOC);
   return v;
}

static vlog_node_t p_variable_dimension(void)
{
   // unsized_dimension | unpacked_dimension | associative_dimension
   //   | queue_dimension

   BEGIN("variable dimension");

   if (is_associative_dimension())
      return p_associative_dimension();

   switch (peek_nth(2)) {
   case tRSQUARE:
      return p_unsized_dimension();
   case tDOLLAR:
      return p_queue_dimension();
   default:
      return p_unpacked_dimension();
   }
}


static bool is_fixed_unpacked(vlog_node_t decl, vlog_node_t dt)
{
   // An unpacked array with a fixed size which is either a dimension of
   // the declaration or part of the type

   if (vlog_ranges(decl) > 0)
      return true;

   vlog_node_t type = vlog_get_type(dt);
   return type != NULL && vlog_kind(type) == V_DATA_TYPE
      && vlog_subkind(type) == DT_DYNARRAY && vlog_ranges(type) == 1
      && vlog_subkind(vlog_range(type, 0)) != V_DIM_UNSIZED;
}

static vlog_node_t p_variable_dimensions(vlog_node_t dt, vlog_node_t decl)
{
   // Dynamic dimensions become part of the type so that the element
   // type of an expression can always be found from its data type

   LOCAL_TEXT_BUF unused = NULL;
   (void)unused;

   SCOPED_A(vlog_node_t) dims = AINIT;
   while (peek() == tLSQUARE)
      APUSH(dims, p_variable_dimension());

   // The dimensions written after the name of a type come last
   for (int i = 0; i < typedef_dims.count; i++)
      APUSH(dims, typedef_dims.items[i]);

   // An unpacked array of structures, strings, classes, or queues cannot be
   // a vector of signals so it is a dynamic array with a fixed size
   bool wrap_sized = false;
   if (decl != NULL && dt != NULL) {
      vlog_node_t base = vlog_get_type(dt);
      if (base != NULL && vlog_is_dynamic_type(base)) {
         switch (vlog_kind(base)) {
         case V_DATA_TYPE:
            switch (vlog_subkind(base)) {
            case DT_PROCESS:
            case DT_CHANDLE:
               break;
            default:
               wrap_sized = true;
               break;
            }
            break;
         default:
            wrap_sized = true;
            break;
         }
      }
   }

   // A fixed dimension to the right of a dynamic one is indexed after it
   // so it cannot be a range of the declaration
   int first_dyn = dims.count;
   for (int i = dims.count - 1; i >= 0; i--) {
      if (vlog_subkind(dims.items[i]) == V_DIM_ASSOC
          || vlog_subkind(dims.items[i]) == V_DIM_UNSIZED)
         first_dyn = i;
   }

   // A member of a class is not a signal so an array of queues or dynamic
   // arrays is a dynamic array of them
   if (decl != NULL && class_depth > 0 && first_dyn < dims.count)
      wrap_sized = true;

   for (int i = dims.count - 1; i >= 0; i--) {
      vlog_node_t d = dims.items[i];
      if (vlog_subkind(d) == V_DIM_ASSOC) {
         vlog_node_t wrap = vlog_new(V_DATA_TYPE);
         vlog_set_subkind(wrap, DT_ASSOC);
         vlog_set_type(wrap, dt);
         if (vlog_has_left(d))
            vlog_set_ref(wrap, vlog_left(d));
         else
            vlog_set_flags(wrap, VLOG_F_WILDCARD);
         vlog_set_loc(wrap, vlog_loc(d));
         dt = wrap;
      }
      else if (vlog_subkind(d) == V_DIM_UNSIZED) {
         vlog_node_t wrap = vlog_new(V_DATA_TYPE);
         vlog_set_subkind(wrap, (vlog_flags(d) & VLOG_F_PACKED)
                          ? DT_QUEUE : DT_DYNARRAY);
         vlog_set_type(wrap, dt);
         vlog_set_loc(wrap, vlog_loc(d));
         dt = wrap;
      }
      else if (decl != NULL && !wrap_sized && i < first_dyn)
         continue;
      else {
         // Sized dimension in a typedef, or of elements which are not held
         // in a signal: a dynamic array which starts with this many elements
         vlog_node_t wrap = vlog_new(V_DATA_TYPE);
         vlog_set_subkind(wrap, DT_DYNARRAY);
         vlog_set_type(wrap, dt);
         vlog_add_range(wrap, d);
         vlog_set_loc(wrap, vlog_loc(d));
         dt = wrap;
      }
   }

   // In the order they were written with the leftmost index first
   for (int i = 0; decl != NULL && !wrap_sized && i < first_dyn; i++) {
      if (vlog_subkind(dims.items[i]) != V_DIM_UNSIZED
          && vlog_subkind(dims.items[i]) != V_DIM_ASSOC)
         vlog_add_range(decl, dims.items[i]);
   }

   return dt;
}

static bool defer_property_init(vlog_node_t decl);

static vlog_node_t p_variable_decl_assignment(vlog_node_t datatype,
                                              vlog_kind_t kind)
{
   // variable_identifier { variable_dimension } [ = expression ]
   //   | dynamic_array_variable_identifier unsized_dimension
   //       { variable_dimension } [ = dynamic_array_new ]
   //   | class_variable_identifier [ = class_new ]

   BEGIN("variable declaration assignment");

   vlog_node_t v = vlog_new(kind);
   vlog_set_ident(v, p_identifier());

   vlog_node_t dt = p_variable_dimensions(datatype, v);
   vlog_set_type(v, dt);

   // The variable is visible in its own initial value
   vlog_set_loc(v, CURRENT_LOC);
   vlog_symtab_put(symtab, v);

   if (optional(tEQ)) {
      if (kind == V_VAR_DECL && (class_depth > 0 || forward_classes.count > 0)
          && !in_subroutine && defer_property_init(v))
         ;   // A call may be to a method declared later in the class
      else if (peek() == tNEW) {
         if (peek_nth(2) == tLSQUARE)
            vlog_set_value(v, p_dynamic_array_new(vlog_get_type(dt)));
         else
            vlog_set_value(v, p_class_new(vlog_get_type(dt)));
      }
      else if (peek() == tLBRACE && peek_nth(2) != tSHIFTLL
               && peek_nth(2) != tSHIFTRL && is_fixed_unpacked(v, dt)
               && vlog_get_type(dt) != NULL) {
         pattern_array_elems = true;
         vlog_set_value(v, p_assignment_pattern_expression(vlog_get_type(dt)));
         pattern_array_elems = false;
      }
      else {
         pattern_array_elems = peek() == tTICK && peek_nth(2) == tLBRACE
            && is_fixed_unpacked(v, dt);
         vlog_set_value(v, p_expression_of_type(vlog_get_type(dt)));
         pattern_array_elems = false;
      }
   }

   vlog_set_loc(v, CURRENT_LOC);
   return v;
}

static void p_list_of_variable_decl_assignments(vlog_node_t parent,
                                                vlog_node_t datatype,
                                                vlog_kind_t kind)
{
   // variable_decl_assignment { , variable_decl_assignment }

   BEGIN("list of variable declaration assignments");

   do {
      vlog_add_decl(parent, p_variable_decl_assignment(datatype, kind));
   } while (optional(tCOMMA));
}

static void static_init_local_cb(vlog_node_t v, void *ctx)
{
   if (vlog_kind(v) != V_REF || !vlog_has_ref(v))
      return;

   switch (vlog_kind(vlog_ref(v))) {
   case V_TF_PORT_DECL:
   case V_LOCAL_DECL:
      *(vlog_node_t *)ctx = v;
      break;
   default:
      break;
   }
}

typedef struct {
   vlog_node_t home;
} static_init_args_t;

static void static_init_arg_cb(vlog_node_t v, void *ctx)
{
   static_init_args_t *ia = ctx;

   if (vlog_kind(v) != V_REF || !vlog_has_ref(v)
       || vlog_kind(vlog_ref(v)) != V_TF_PORT_DECL)
      return;

   // A static variable with the type of the argument and no initial value
   vlog_node_t arg = vlog_ref(v);
   vlog_node_t h = vlog_new(V_VAR_DECL);
   vlog_set_ident(h, ident_uniq("arginit"));
   vlog_set_type(h, vlog_type(arg));
   vlog_set_loc(h, vlog_loc(arg));
   vlog_set_flags(h, VLOG_F_STATIC);

   const int nranges = vlog_ranges(arg);
   for (int i = 0; i < nranges; i++)
      vlog_add_range(h, vlog_range(arg, i));

   if (vlog_kind(ia->home) == V_MODULE || vlog_kind(ia->home) == V_PACKAGE)
      APUSH(pending_statics, h);
   else
      vlog_add_decl(ia->home, h);

   vlog_set_ref(v, h);
   vlog_set_ident(v, vlog_ident(h));
}

static void p_static_local_decls(vlog_node_t parent, vlog_node_t home,
                                 vlog_node_t datatype, bool is_static)
{
   // A static variable in a subroutine or block keeps its value and is
   // only initialised once so it lives in the enclosing design unit or
   // class but is only visible by name in this scope

   BEGIN("static variable declarations");

   do {
      vlog_node_t d = p_variable_decl_assignment(datatype, V_VAR_DECL);

      if (!is_static && vlog_has_value(d)) {
         // A variable in a static subroutine or block is static by
         // default but one with an initial value must say so (6.21).
         // This is only a warning as such code is common and the
         // default lifetime gives it a meaning
         diag_t *dg = diag_new(DIAG_WARN, vlog_loc(d));
         diag_printf(dg, "initialised variable %pQ in a static %s must be "
                     "declared static or automatic", vlog_ident(d),
                     in_subroutine ? "subroutine" : "block");
         diag_hint(dg, vlog_loc(d), "IEEE 1800-2017 section 6.21 requires "
                   "an explicit lifetime here");
         diag_hint(dg, NULL, "the variable is static and its initial "
                   "value is assigned once at the start of simulation");
         diag_emit(dg);
      }

      vlog_node_t local = NULL;
      if (vlog_has_value(d))
         vlog_visit(vlog_value(d), static_init_local_cb, &local);

      if (local != NULL && vlog_kind(vlog_ref(local)) == V_TF_PORT_DECL
          && !proc_automatic) {
         // The arguments of a static subroutine are static variables so
         // before time 0 an initial value sees the initial value of the
         // argument (6.21)
         static_init_args_t ia = { .home = home };
         vlog_visit(vlog_value(d), static_init_arg_cb, &ia);
         local = NULL;
      }

      if (local != NULL) {
         const bool arg = vlog_kind(vlog_ref(local)) == V_TF_PORT_DECL;
         if (arg)
            error_at(vlog_loc(local), "initial value of static variable %pQ "
                     "cannot refer to automatic argument %pQ", vlog_ident(d),
                     vlog_ident(local));
         else
            error_at(vlog_loc(local), "sorry, the initial value of static "
                     "variable %pQ cannot refer to automatic variable %pQ",
                     vlog_ident(d), vlog_ident(local));
         vlog_set_value(d, NULL);
      }

      const loc_t *loc = vlog_loc(d);
      vlog_set_ident(d, ident_sprintf("%s$static%u_%u", istr(vlog_ident(d)),
                                      loc->first_line, loc->first_column));
      vlog_set_flags(d, VLOG_F_STATIC);

      bool added = false;
      if (vlog_kind(home) == V_CLASS_DECL)
         vlog_set_ref(d, home);
      else if (current_func != NULL) {
         vlog_add_decl(current_func, d);   // For constant evaluation
         added = current_func == parent;
      }

      if (!added && (vlog_kind(parent) == V_FUNC_DECL
                     || vlog_kind(parent) == V_TASK_DECL))
         vlog_add_decl(parent, d);         // Visible as subroutine.name

      if (vlog_kind(home) == V_MODULE || vlog_kind(home) == V_PACKAGE)
         APUSH(pending_statics, d);        // Added at the end of the unit
      else
         vlog_add_decl(home, d);
   } while (optional(tCOMMA));
}

static vlog_node_t nettype_lookup(void)
{
   // The net type declaration named by the next token

   if (peek() != tID || peek_nth(2) == tSCOPE)
      return NULL;

   vlog_node_t d = vlog_symtab_query(symtab, peek_ident_nth(1));
   if (d != NULL && vlog_kind(d) == V_TYPE_DECL
       && (vlog_flags(d) & VLOG_F_NETTYPE))
      return d;

   return NULL;
}

static vlog_node_t nettype_root(vlog_node_t d)
{
   // A net type declared as another name for an existing one
   while (vlog_has_type(d) && vlog_kind(vlog_type(d)) == V_TYPE_DECL
          && (vlog_flags(vlog_type(d)) & VLOG_F_NETTYPE))
      d = vlog_type(d);

   return d;
}

static bool nettype_type_ok(vlog_node_t t)
{
   switch (vlog_kind(t)) {
   case V_TYPE_DECL:
      return vlog_has_type(t) ? nettype_type_ok(vlog_type(t)) : true;
   case V_STRUCT_DECL:
   case V_UNION_DECL:
      {
         const int ndecls = vlog_decls(t);
         for (int i = 0; i < ndecls; i++) {
            vlog_node_t m = vlog_decl(t, i);
            if (vlog_kind(m) == V_VAR_DECL && vlog_has_type(m)
                && !nettype_type_ok(vlog_type(m)))
               return false;
         }
         return true;
      }
   case V_CLASS_DECL:
      return false;
   case V_DATA_TYPE:
      switch (vlog_subkind(t)) {
      case DT_STRING:
      case DT_DYNARRAY:
      case DT_QUEUE:
      case DT_ASSOC:
      case DT_MAILBOX:
      case DT_SEMAPHORE:
      case DT_PROCESS:
      case DT_CHANDLE:
      case DT_VIRTUAL:
      case DT_EVENT:
         return false;
      default:
         return true;
      }
   default:
      return true;
   }
}

static vlog_node_t nettype_peel(vlog_node_t t)
{
   while (vlog_kind(t) == V_TYPE_DECL && vlog_has_type(t))
      t = vlog_type(t);
   return t;
}

static bool nettype_same_type(vlog_node_t a, vlog_node_t b)
{
   a = nettype_peel(a);
   b = nettype_peel(b);

   if (a == b)
      return true;
   else if (vlog_kind(a) != V_DATA_TYPE || vlog_kind(b) != V_DATA_TYPE)
      return false;

   const bool ra = vlog_subkind(a) == DT_REAL || vlog_subkind(a) == DT_SHORTREAL
      || vlog_subkind(a) == DT_REALTIME;
   const bool rb = vlog_subkind(b) == DT_REAL || vlog_subkind(b) == DT_SHORTREAL
      || vlog_subkind(b) == DT_REALTIME;

   return ra == rb && (ra || vlog_size(a) == vlog_size(b));
}

static void nettype_check_resolver(vlog_node_t v, ident_t fname,
                                   const loc_t *loc)
{
   vlog_node_t f = vlog_symtab_query(symtab, fname);
   if (f == NULL)
      return;

   if (vlog_kind(f) != V_FUNC_DECL) {
      if (vlog_kind(f) == V_TASK_DECL)
         error_at(loc, "'%s' is not a function", istr(fname));
      return;
   }

   bool ok = vlog_ports(f) == 1;
   if (ok) {
      vlog_node_t port = vlog_port(f, 0);
      ok = vlog_subkind(port) == V_PORT_INPUT;
      if (ok) {
         bool dyn = vlog_has_type(port)
            && vlog_kind(nettype_peel(vlog_type(port))) == V_DATA_TYPE
            && vlog_subkind(nettype_peel(vlog_type(port))) == DT_DYNARRAY;
         const int nranges = vlog_ranges(port);
         for (int i = 0; i < nranges; i++) {
            vlog_node_t r = vlog_range(port, i);
            if (vlog_kind(r) == V_DIMENSION
                && vlog_subkind(r) == V_DIM_UNSIZED)
               dyn = true;
         }
         ok = dyn;
      }
   }

   if (!ok) {
      error_at(loc, "resolution function '%s' must have a single input "
               "argument that is a dynamic array of the data type of "
               "nettype %s", istr(fname), istr(vlog_ident(v)));
      return;
   }

   if (!vlog_has_type(f) || !nettype_same_type(vlog_type(f), vlog_type(v)))
      error_at(loc, "return type of resolution function '%s' must be the "
               "data type of nettype %s", istr(fname), istr(vlog_ident(v)));
}

static vlog_node_t p_net_type_declaration(void)
{
   // nettype data_type net_type_identifier [ with [ package_scope
   //   | class_scope ] tf_identifier ] ;
   //   | nettype [ package_scope | class_scope ] net_type_identifier
   //       net_type_identifier ;

   BEGIN("net type declaration");

   consume(tNETTYPE);

   ACLEAR(typedef_dims);

   vlog_node_t v = vlog_new(V_TYPE_DECL);
   vlog_set_flags(v, VLOG_F_NETTYPE);

   vlog_node_t orig = nettype_lookup();
   if (orig != NULL && peek_nth(2) == tID) {
      // Another name for an existing net type
      consume(tID);
      vlog_set_type(v, orig);
      vlog_set_ident(v, p_identifier());
      consume(tSEMI);
      vlog_set_loc(v, CURRENT_LOC);
      vlog_symtab_put(symtab, v);
      return v;
   }

   vlog_set_type(v, p_data_type());

   vlog_set_ident(v, p_identifier());

   {
      vlog_node_t dt = nettype_peel(vlog_type(v));
      if (vlog_kind(dt) == V_DATA_TYPE && vlog_subkind(dt) == DT_STRING)
         error_at(&state.last_loc, "invalid data type string for nettype %s",
                  istr(vlog_ident(v)));
      else if (!nettype_type_ok(dt))
         error_at(&state.last_loc, "invalid data type for nettype %s",
                  istr(vlog_ident(v)));
   }

   if (optional(tWITH)) {
      hier_owner_t owner;
      vlog_node_t pkg;
      (void)vlog_symtab_hier_suffix(symtab, &owner, &pkg);

      LOCAL_TEXT_BUF tb = tb_new();
      vlog_node_t scope = NULL;
      if (peek() == tID && peek_nth(2) == tSCOPE) {
         scope = p_scope_prefix();
         if (scope != NULL && vlog_has_ident2(scope))
            tb_printf(tb, "%s::", istr(vlog_ident2(scope)));
      }
      else if (owner == HIER_PACKAGE && pkg != NULL)
         tb_printf(tb, "%s::", istr(vlog_ident2(pkg)));

      const bool qualified = tb_len(tb) > 0;
      ident_t fname = p_identifier();
      tb_cat(tb, istr(fname));
      vlog_set_ident2(v, ident_new(tb_get(tb)));

      vlog_node_t f = scope != NULL ? vlog_scope_member(scope, fname)
         : vlog_symtab_query(symtab, fname);
      if (f != NULL && vlog_kind(f) == V_FUNC_DECL)
         vlog_set_ref(v, f);

      if (!qualified)
         nettype_check_resolver(v, fname, &state.last_loc);
   }

   consume(tSEMI);

   vlog_set_loc(v, CURRENT_LOC);
   vlog_symtab_put(symtab, v);
   return v;
}

static void scope_prefix_text(text_buf_t *tb)
{
   // The prefix which $typename puts before the name of the type

   ident_t qual = vlog_symtab_display_scope(symtab);
   if (qual == NULL)
      tb_cat(tb, "$unit::");   // The compilation-unit scope (3.12.1, 26.3)
   else
      tb_cat(tb, istr(qual));
}

static void typedef_scope_name(vlog_node_t v)
{
   LOCAL_TEXT_BUF tb = tb_new();
   scope_prefix_text(tb);
   vlog_set_ident2(v, ident_new(tb_get(tb)));
}

static void name_anonymous_type(vlog_node_t v, bool is_enum)
{
   // A structure, union or enumeration which is not declared by a typedef
   // is numbered in each scope as it appears

   static hash_t *counts = NULL;
   if (counts == NULL)
      counts = hash_new(16);

   LOCAL_TEXT_BUF tb = tb_new();
   scope_prefix_text(tb);

   ident_t key = ident_new(tb_get(tb));
   ident_t group = ident_prefix(key, ident_new(is_enum ? "enum" : "struct"), '#');
   const int n = (int)(intptr_t)hash_get(counts, group) + 1;
   hash_put(counts, group, (void *)(intptr_t)n);

   tb_printf(tb, "%c$%d", is_enum ? 'e' : 's', n);
   vlog_set_ident(v, ident_new(tb_get(tb)));
}

static vlog_node_t p_type_declaration(void)
{
   // typedef data_type type_identifier { variable_dimension } ;
   //   | typedef interface_instance_identifier constant_bit_select .
   //       type_identifier type_identifier ;
   //   | typedef [ enum | struct | union | class | interface class ]
   //       type_identifier ;

   BEGIN("type declaration");

   consume(tTYPEDEF);

   if (peek() == tCLASS || (peek() == tINTERFACE && peek_nth(2) == tCLASS)) {
      // The class must be declared later in this scope
      (void)optional(tINTERFACE);
      consume(tCLASS);

      ident_t id = p_identifier();
      const loc_t loc = state.last_loc;

      consume(tSEMI);

      if (vlog_symtab_query(symtab, id) == NULL) {
         vlog_node_t c = vlog_new(V_CLASS_DECL);
         vlog_set_ident(c, id);
         vlog_set_flags(c, VLOG_F_FORWARD);
         vlog_set_loc(c, &loc);
         vlog_symtab_put(symtab, c);
         APUSH(forward_classes, c);
      }

      return NULL;
   }

   if (((peek() == tSTRUCT || peek() == tUNION || peek() == tENUM)
        && peek_nth(2) == tID && peek_nth(3) == tSEMI)
       || (peek() == tID && peek_nth(2) == tSEMI)) {
      // Forward declaration of a type which is declared in full later
      if (peek() != tID)
         drop_token(&state);
      ident_t id = p_identifier();
      consume(tSEMI);

      if (vlog_symtab_query(symtab, id) == NULL) {
         forward_type_t fwd = { .id = id, .loc = state.last_loc };
         APUSH(forward_types, fwd);
      }

      return NULL;
   }

   ACLEAR(typedef_dims);

   vlog_node_t v = vlog_new(V_TYPE_DECL);
   typedef_depth++;
   vlog_node_t dt = p_data_type();
   typedef_depth--;
   vlog_set_ident(v, p_identifier());
   typedef_scope_name(v);

   for (int i = 0; i < forward_types.count; i++) {
      if (forward_types.items[i].id == vlog_ident(v)) {
         forward_types.items[i] = forward_types.items[--forward_types.count];
         i--;
      }
   }
   vlog_set_type(v, p_variable_dimensions(dt, NULL));
   ACLEAR(typedef_dims);

   consume(tSEMI);

   vlog_set_loc(v, CURRENT_LOC);
   vlog_symtab_put(symtab, v);
   return v;
}

static vlog_node_t p_package_import_item(void)
{
   // package_identifier :: identifier | package_identifier :: *

   BEGIN("package import item");

   vlog_node_t pack = p_package_identifier();

   vlog_node_t v = vlog_new(V_IMPORT_DECL);
   vlog_set_ref(v, pack);

   consume(tSCOPE);

   if (peek() == tID)
      vlog_set_ident(v, p_identifier());
   else
      one_of(tTIMES, tID);

   vlog_set_loc(v, CURRENT_LOC);
   vlog_symtab_import(symtab, v);
   return v;
}

static void add_scope_init(vlog_node_t mod, ident_t name)
{
   // A process that initialises a package as soon as the simulation starts
   // since a package can have an effect even if it is not referenced

   vlog_node_t call = vlog_new(V_SYS_TCALL);
   vlog_set_ident(call, ident_sprintf("$__init:%s", istr(name)));
   vlog_set_subkind(call, V_SYSTF_NONE);
   vlog_set_ival(call, 0);
   vlog_set_loc(call, vlog_loc(mod));

   vlog_node_t v = vlog_new(V_INITIAL);
   vlog_set_ident(v, ident_sprintf("$scope_init%d", vlog_stmts(mod)));
   vlog_add_stmt(v, call);
   vlog_set_loc(v, vlog_loc(mod));

   vlog_add_stmt(mod, v);
}

static void p_package_import_declaration(vlog_node_t parent)
{
   // import package_import_item { , package_import_item } ;

   BEGIN("package import declaration");

   consume(tIMPORT);

   if (peek() == tSTRING) {
      // import "DPI-C" [ context | pure ] [ c_identifier = ] prototype ;
      consume(tSTRING);

      bool is_context = false;
      if (peek() == tID && peek_ident_nth(1) == ident_new("context")) {
         consume(tID);
         is_context = true;
      }
      else
         optional(tVPURE);

      ident_t cname = NULL;
      if (peek() == tID && peek_nth(2) == tEQ) {
         cname = p_identifier();
         consume(tEQ);
      }

      const bool saved_proto = parsing_prototype;
      const bool saved_dpi = parsing_dpi_import;
      const int saved_depth = class_depth;
      parsing_prototype = true;
      parsing_dpi_import = true;
      class_depth = 0;

      vlog_node_t subr = peek() == tTASK
         ? p_task_declaration() : p_function_declaration();

      parsing_prototype = saved_proto;
      parsing_dpi_import = saved_dpi;
      class_depth = saved_depth;

      if (subr != NULL) {
         vlog_set_flags(subr, vlog_flags(subr) | VLOG_F_DPI
                        | (is_context ? VLOG_F_CONTEXT : 0));
         if (cname != NULL)
            vlog_set_ident3(subr, cname);
         vlog_add_decl(parent, subr);
      }

      return;
   }

   do {
      vlog_node_t item = p_package_import_item();
      vlog_add_decl(parent, item);

      if (vlog_kind(parent) == V_MODULE && vlog_has_ref(item))
         add_scope_init(parent, vlog_ident(vlog_ref(item)));
   } while (optional(tCOMMA));

   consume(tSEMI);
}

static int atom_width(vlog_node_t dt)
{
   switch (vlog_subkind(dt)) {
   case DT_BYTE:     return 8;
   case DT_SHORTINT: return 16;
   case DT_INT:
   case DT_INTEGER:  return 32;
   case DT_LONGINT:  return 64;
   default:          return 0;
   }
}

static vlog_node_t p_typedef_dimensions(vlog_node_t dt)
{
   // type_identifier packed_dimension { packed_dimension }

   vlog_node_t base = dt;
   while (base != NULL && vlog_kind(base) == V_TYPE_DECL)
      base = vlog_type(base);

   if (base == NULL || dt == base)
      return dt;   // Not a type identifier

   vlog_node_t vec = base;
   if (vlog_kind(base) == V_ENUM_DECL && vlog_has_type(base)) {
      // A packed array of enumeration values is a vector of elements which
      // are the enumeration, so that an element has the methods of one
      vlog_node_t bt = vlog_get_type(vlog_type(base));
      if (bt != NULL && vlog_kind(bt) == V_DATA_TYPE) {
         vlog_node_t v = vlog_new(V_DATA_TYPE);
         vlog_set_subkind(v, vlog_is_two_state(bt) ? DT_BIT : DT_LOGIC);
         vlog_set_type(v, dt);
         vlog_set_loc(v, vlog_loc(dt));

         while (peek() == tLSQUARE)
            vlog_add_range(v, p_packed_dimension());

         return v;
      }

      // A packed array of enumeration values is a vector of their base type
      vec = vlog_get_type(vlog_type(base));
      if (vec != NULL && vlog_kind(vec) == V_DATA_TYPE
          && vlog_subkind(vec) != DT_LOGIC && vlog_subkind(vec) != DT_BIT
          && vlog_ranges(vec) == 0 && atom_width(vec) > 0) {
         vlog_node_t two = vlog_new(V_DATA_TYPE);
         vlog_set_subkind(two, DT_BIT);
         vlog_set_flags(two, vlog_flags(vec));
         vlog_set_loc(two, vlog_loc(base));

         vlog_node_t dim = vlog_new(V_DIMENSION);
         vlog_set_subkind(dim, V_DIM_PACKED);
         vlog_set_left(dim, foreach_number(atom_width(vec) - 1, vlog_loc(base)));
         vlog_set_right(dim, foreach_number(0, vlog_loc(base)));
         vlog_set_loc(dim, vlog_loc(base));
         vlog_add_range(two, dim);
         vec = two;
      }
   }

   if (vec != NULL && vlog_kind(vec) == V_DATA_TYPE
       && (vlog_subkind(vec) == DT_LOGIC || vlog_subkind(vec) == DT_BIT)) {
      // A packed array of a packed array
      vlog_node_t v = vlog_new(V_DATA_TYPE);
      vlog_set_subkind(v, vlog_subkind(vec));
      vlog_set_flags(v, vlog_flags(vec));
      vlog_set_loc(v, vlog_loc(dt));

      while (peek() == tLSQUARE)
         vlog_add_range(v, p_packed_dimension());

      const int nranges = vlog_ranges(vec);
      for (int i = 0; i < nranges; i++)
         vlog_add_range(v, vlog_range(vec, i));

      return v;
   }
   else if ((vlog_kind(base) == V_STRUCT_DECL
             || vlog_kind(base) == V_UNION_DECL)
            && (vlog_flags(base) & VLOG_F_PACKED)) {
      // A packed array of packed structures or unions is a vector made of
      // elements
      vlog_node_t v = vlog_new(V_DATA_TYPE);
      vlog_set_subkind(v, vlog_is_two_state(base) ? DT_BIT : DT_LOGIC);
      vlog_set_type(v, dt);
      vlog_set_loc(v, vlog_loc(dt));

      while (peek() == tLSQUARE)
         vlog_add_range(v, p_packed_dimension());

      return v;
   }

   return dt;
}

static void p_data_declaration(vlog_node_t parent, vlog_kind_t kind)
{
   // [ const ] [ var ] [ lifetime ] data_type_or_implicit
   //     list_of_variable_decl_assignments ;
   //  | type_declaration | package_import_declaration | net_type_declaration

   BEGIN("data declaration");

   switch (peek()) {
   case tTYPEDEF:
      {
         vlog_node_t td = p_type_declaration();
         if (td != NULL)
            vlog_add_decl(parent, td);
      }
      break;
   case tNETTYPE:
      vlog_add_decl(parent, p_net_type_declaration());
      break;
   case tIMPORT:
      p_package_import_declaration(parent);
      break;
   default:
      {
         const bool is_const = optional(tCONST);
         optional(tVAR);

         bool is_static = false, is_automatic = false;
         if (scan(tSTATIC, tAUTOMATIC)) {
            is_static = one_of(tSTATIC, tAUTOMATIC) == tSTATIC;
            is_automatic = !is_static;
         }

         ACLEAR(typedef_dims);

         struct_dims_deferred = true;
         vlog_node_t nettype = nettype_lookup();

         vlog_node_t dt;
         if (nettype != NULL) {
            consume(tID);
            dt = nettype;
         }
         else
            dt = p_data_type_or_implicit();

         if (peek() == tLSQUARE)
            dt = p_typedef_dimensions(dt);
         struct_dims_deferred = false;

         const bool implicit = !is_static && !is_automatic && !is_const
            && !proc_automatic;

         vlog_node_t home = NULL;
         if ((is_static || implicit) && kind == V_LOCAL_DECL)
            home = vlog_symtab_static_home(symtab) ?: current_namespace;

         const int first = vlog_decls(parent);

         if (home != NULL)
            p_static_local_decls(parent, home, dt, is_static);
         else
            p_list_of_variable_decl_assignments(parent, dt, kind);

         if (nettype != NULL) {
            const int ndecls = vlog_decls(parent);
            for (int i = first; i < ndecls; i++) {
               vlog_node_t d = vlog_decl(parent, i);
               if (vlog_kind(parent) != V_MODULE || vlog_kind(d) != V_VAR_DECL)
                  parse_error(vlog_loc(d), "sorry, nets of a user-defined "
                              "net type are only supported in a module");
               else
                  register_user_net(d, nettype_root(nettype));
            }
         }

         ACLEAR(typedef_dims);

         consume(tSEMI);
      }
   }
}

static v_port_kind_t p_port_direction(void)
{
   // input | output | inout | ref

   BEGIN("port direction");

   switch (one_of(tINPUT, tOUTPUT, tINOUT, tREF)) {
   case tINPUT:  return V_PORT_INPUT;
   case tOUTPUT: return V_PORT_OUTPUT;
   case tINOUT:  return V_PORT_INOUT;
   case tREF:    return V_PORT_REF;
   default:      return V_PORT_INPUT;
   }
}

static bool tf_port_const;

static v_port_kind_t p_tf_port_direction(void)
{
   // port_direction | const ref

   BEGIN("task or function port direction");

   const bool is_const = optional(tCONST);

   const v_port_kind_t kind = p_port_direction();
   tf_port_const = is_const && kind == V_PORT_REF;
   return kind;
}

static void default_arg_formal_cb(vlog_node_t v, void *ctx)
{
   if (vlog_kind(v) == V_REF && vlog_has_ref(v)
       && vlog_kind(vlog_ref(v)) == V_TF_PORT_DECL)
      *(vlog_node_t *)ctx = vlog_ref(v);
}

static vlog_node_t p_tf_port_item(v_port_kind_t *kind, vlog_node_t *dt)
{
   // { attribute_instance } [ tf_port_direction ] [ var ]
   //    data_type_or_implicit [ port_identifier { variable_dimension }
   //    [ = expression ] ]

   BEGIN("task or function port item");

   vlog_node_t v = vlog_new(V_TF_PORT_DECL);
   if (!proc_automatic)
      vlog_set_flags(v, VLOG_F_IMPLICIT_STATIC);

   skip_over_attributes();

   if (scan(tINPUT, tOUTPUT, tINOUT, tREF, tCONST)) {
      *kind = p_tf_port_direction();
      *dt = p_data_type_or_implicit();
   }
   else if (peek() != tID || peek_nth(2) == tID || peek_nth(2) == tHASH
            || is_scoped_type_start()
            || (peek_nth(2) == tLSQUARE && is_type_name(peek_reference())))
      *dt = p_data_type_or_implicit();
   else if (*dt == NULL)
      *dt = p_implicit_data_type();

   vlog_set_subkind(v, *kind);

   if (*kind == V_PORT_REF && tf_port_const)
      vlog_set_flags(v, vlog_flags(v) | VLOG_F_CONST);

   if (peek() == tID) {
      vlog_set_ident(v, p_identifier());
      vlog_set_type(v, p_variable_dimensions(*dt, v));

      if (optional(tEQ)) {
         if (peek() == tTICK && peek_nth(2) == tLBRACE)
            vlog_set_value(v, p_assignment_pattern_expression(vlog_type(v)));
         else
            vlog_set_value(v, p_expression());

         // The default is evaluated in the scope containing the
         // declaration of the subroutine (13.5.3) where its arguments
         // are not visible
         vlog_node_t formal = NULL;
         vlog_visit(vlog_value(v), default_arg_formal_cb, &formal);
         if (formal != NULL) {
            diag_t *d = diag_new(DIAG_WARN, vlog_loc(vlog_value(v)));
            diag_printf(d, "default value of argument %s refers to "
                        "argument %s", istr(vlog_ident(v)),
                        istr(vlog_ident(formal)));
            diag_hint(d, NULL, "IEEE 1800-2017 evaluates a default value "
                      "in the scope containing the subroutine declaration");
            diag_emit(d);
         }
      }
   }
   else
      vlog_set_type(v, *dt);

   vlog_set_loc(v, CURRENT_LOC);
   return v;
}

static void p_tf_port_list(vlog_node_t tf)
{
   // tf_port_item { , tf_port_item }

   BEGIN("task or function port list");

   v_port_kind_t kind = V_PORT_INPUT;
   vlog_node_t dt = NULL;
   do {
      vlog_node_t v = p_tf_port_item(&kind, &dt);
      vlog_add_port(tf, v);

      if (vlog_has_ident(v))  // Ignore unnamed ports
         vlog_symtab_put(symtab, v);
   } while (optional(tCOMMA));
}

static void p_list_of_tf_variable_identifiers(vlog_node_t tf,
                                              v_port_kind_t kind,
                                              vlog_node_t dt)
{
   // port_identifier { variable_dimension } [ = expression ]
   //    { , port_identifier { variable_dimension } [ = expression ] }

   BEGIN("list of task or function variable identifiers");

   do {
      vlog_node_t v = vlog_new(V_TF_PORT_DECL);
      if (!proc_automatic)
         vlog_set_flags(v, VLOG_F_IMPLICIT_STATIC);
      vlog_set_subkind(v, kind);
      vlog_set_ident(v, p_identifier());
      vlog_set_loc(v, &state.last_loc);
      vlog_set_type(v, p_variable_dimensions(dt, v));

      if (optional(tEQ))
         vlog_set_value(v, p_expression());

      vlog_add_port(tf, v);
      vlog_symtab_put(symtab, v);
   } while (optional(tCOMMA));
}

static void p_tf_port_declaration(vlog_node_t tf)
{
   // { attribute_instance } tf_port_direction [ var ] data_type_or_implicit
   //    list_of_tf_variable_identifiers ;

   BEGIN("task or function port declaration");

   v_port_kind_t kind = p_tf_port_direction();

   optional(tVAR);

   vlog_node_t dt = p_data_type_or_implicit();

   p_list_of_tf_variable_identifiers(tf, kind, dt);

   consume(tSEMI);
}

static void p_tf_item_declaration(vlog_node_t tf)
{
   // block_item_declaration | tf_port_declaration

   BEGIN("task or function item declaration");

   switch (peek()) {
   case tINPUT:
   case tOUTPUT:
   case tINOUT:
   case tREF:
   case tCONST:
      p_tf_port_declaration(tf);
      break;
   default:
      p_block_item_declaration(tf);
      break;
   }
}

static void push_class_scopes(vlog_node_t class, int *depth)
{
   // Make the members of a class visible outside of its declaration

   *depth = push_base_scopes(class);
   vlog_symtab_push(symtab, class);

   const int ndecls = vlog_decls(class);
   for (int i = 0; i < ndecls; i++) {
      vlog_node_t d = vlog_decl(class, i);
      switch (vlog_kind(d)) {
      case V_TYPE_DECL:
         if (vlog_ident(d) == ident_new("$implements"))
            break;
         put_enum_names(d);
         // Fall-through
      case V_VAR_DECL:
      case V_TASK_DECL:
      case V_FUNC_DECL:
      case V_CONSTRAINT:
      case V_LOCALPARAM:
         vlog_symtab_put(symtab, d);
         break;
      default:
         break;
      }
   }
}

static vlog_node_t find_extern_proto(ident_t cname, const loc_t *cloc,
                                     ident_t id, const loc_t *mloc,
                                     vlog_kind_t kind, vlog_node_t *classp)
{
   // Find the prototype in a class for a definition outside of the class

   vlog_node_t class = spec_extern_class;
   if (class == NULL)
      class = vlog_symtab_query(symtab, cname);
   if (class != NULL && vlog_kind(class) == V_TYPE_DECL)
      class = vlog_get_type(class);

   *classp = NULL;

   if (class == NULL || vlog_kind(class) != V_CLASS_DECL) {
      error_at(cloc, "no class named %pQ", cname);
      return NULL;
   }
   else if (vlog_flags(class) & VLOG_F_GENERIC) {
      error_at(cloc, "sorry, a method of a parameterised class cannot be "
               "defined outside of the class");
      return NULL;
   }

   *classp = class;

   vlog_node_t proto = NULL;
   const int ndecls = vlog_decls(class);
   for (int i = 0; i < ndecls; i++) {
      vlog_node_t d = vlog_decl(class, i);
      if (vlog_kind(d) == kind && vlog_ident(d) == id) {
         proto = d;
         break;
      }
   }

   if (proto == NULL || !(vlog_flags(proto) & VLOG_F_EXTERN)) {
      diag_t *d = diag_new(DIAG_ERROR, mloc);
      if (proto == NULL)
         diag_printf(d, "class %pQ has no %s named %pQ", cname,
                     kind == V_TASK_DECL ? "task" : "function", id);
      else
         diag_printf(d, "%pQ was not declared extern in class %pQ", id,
                     cname);
      if (proto != NULL)
         diag_hint(d, vlog_loc(proto), "declared here");
      diag_hint(d, mloc, "definition");
      diag_emit(d);
      return NULL;
   }

   return proto;
}

static void p_extern_subr_body(vlog_node_t tmp)
{
   // class_scope task_identifier ... endtask
   //   | class_scope function_identifier ... endfunction
   //
   // The definition of a method which was declared extern in a class is
   // parsed into the prototype

   BEGIN("out-of-class method definition");

   const bool is_func = vlog_kind(tmp) == V_FUNC_DECL;
   const token_t end_token = is_func ? tENDFUNCTION : tENDTASK;

   extern_definition = true;

   ident_t cname = p_identifier();
   const loc_t cloc = state.last_loc;
   consume(tSCOPE);
   ident_t id = p_identifier();
   const loc_t mloc = state.last_loc;

   vlog_node_t class;
   vlog_node_t proto = find_extern_proto(cname, &cloc, id, &mloc,
                                        vlog_kind(tmp), &class);

   if (proto == NULL) {
      // Skip the definition
      drop_tokens_until(&state, end_token);
      if (optional(tCOLON))
         (void)p_identifier();
      return;
   }

   const bool saved_proc = proc_automatic, saved_subr = subr_automatic;
   vlog_node_t saved_func = current_func;
   proc_automatic = subr_automatic = true;
   current_func = is_func ? proto : NULL;

   int depth;
   push_class_scopes(class, &depth);
   vlog_symtab_push(symtab, proto);

   // The ports are the ones of the prototype: check the header against it
   const int nports = vlog_ports(proto);

   if (optional(tLPAREN)) {
      vlog_node_t hdr = vlog_new(vlog_kind(tmp));
      vlog_set_ident(hdr, id);
      vlog_set_loc(hdr, &mloc);

      vlog_symtab_push(symtab, hdr);
      if (peek() != tRPAREN)
         p_tf_port_list(hdr);
      vlog_symtab_pop(symtab);

      consume(tRPAREN);

      bool match = vlog_ports(hdr) == nports;
      for (int i = 0; match && i < nports; i++) {
         vlog_node_t a = vlog_port(proto, i), b = vlog_port(hdr, i);
         match = vlog_has_ident(a) == vlog_has_ident(b)
            && (!vlog_has_ident(a) || vlog_ident(a) == vlog_ident(b));
      }

      if (!match) {
         diag_t *d = diag_new(DIAG_ERROR, &mloc);
         diag_printf(d, "ports of %pQ do not match the prototype in class "
                     "%pQ", id, cname);
         diag_hint(d, vlog_loc(proto), "prototype declared here");
         diag_hint(d, &mloc, "definition");
         diag_emit(d);
      }
   }
   else if (nports > 0)
      error_at(&state.last_loc, "ports of %pQ do not match the prototype in "
               "class %pQ", id, cname);

   for (int i = 0; i < nports; i++) {
      vlog_node_t p = vlog_port(proto, i);
      if (vlog_has_ident(p))
         vlog_symtab_put(symtab, p);
   }

   consume(tSEMI);

   current_func = is_func ? proto : NULL;

   const int saved_depth = class_depth;
   class_depth++;   // The body may use members of classes declared later
   const bool deferred = defer_subr_body(proto, end_token);
   class_depth = saved_depth;

   vlog_clear_flags(proto, VLOG_F_EXTERN);

   vlog_symtab_pop(symtab);
   vlog_symtab_pop(symtab);
   for (int i = 0; i < depth; i++)
      vlog_symtab_pop(symtab);

   proc_automatic = saved_proc;
   subr_automatic = saved_subr;
   current_func = saved_func;

   assert(deferred);
   (void)deferred;

   flush_deferred_bodies(false);
}

static void p_extern_constructor(void)
{
   // class_scope new [ ( [ class_constructor_arg_list ] ) ] ; ... endfunction

   BEGIN("out-of-class constructor definition");

   extern_definition = true;

   ident_t cname = p_identifier();
   const loc_t cloc = state.last_loc;
   consume(tSCOPE);
   consume(tNEW);
   const loc_t mloc = state.last_loc;

   ident_t id = ident_new("new");

   vlog_node_t class;
   vlog_node_t proto = find_extern_proto(cname, &cloc, id, &mloc,
                                        V_CONSTRUCTOR, &class);

   if (proto == NULL) {
      drop_tokens_until(&state, tENDFUNCTION);
      if (optional(tCOLON))
         consume(tNEW);
      return;
   }

   const bool saved_proc = proc_automatic, saved_subr = subr_automatic;
   proc_automatic = subr_automatic = true;

   int depth;
   push_class_scopes(class, &depth);
   vlog_symtab_push(symtab, proto);

   const int nparams = vlog_params(proto);

   if (optional(tLPAREN)) {
      vlog_node_t hdr = vlog_new(V_CONSTRUCTOR);
      vlog_set_ident(hdr, id);
      vlog_set_loc(hdr, &mloc);

      vlog_symtab_push(symtab, hdr);
      if (not_at_token(tRPAREN))
         p_class_constructor_arg_list(hdr);
      vlog_symtab_pop(symtab);

      consume(tRPAREN);

      bool match = vlog_params(hdr) == nparams;
      for (int i = 0; match && i < nparams; i++) {
         vlog_node_t a = vlog_param(proto, i), b = vlog_param(hdr, i);
         match = vlog_has_ident(a) == vlog_has_ident(b)
            && (!vlog_has_ident(a) || vlog_ident(a) == vlog_ident(b));
      }

      if (!match) {
         diag_t *d = diag_new(DIAG_ERROR, &mloc);
         diag_printf(d, "arguments of the constructor do not match the "
                     "prototype in class %pQ", cname);
         diag_hint(d, vlog_loc(proto), "prototype declared here");
         diag_hint(d, &mloc, "definition");
         diag_emit(d);
      }
   }
   else if (nparams > 0)
      error_at(&state.last_loc, "arguments of the constructor do not match "
               "the prototype in class %pQ", cname);

   for (int i = 0; i < nparams; i++) {
      vlog_node_t p = vlog_param(proto, i);
      if (vlog_has_ident(p))
         vlog_symtab_put(symtab, p);
   }

   consume(tSEMI);

   const int saved_depth = class_depth;
   class_depth++;
   const bool deferred = defer_subr_body(proto, tENDFUNCTION);
   class_depth = saved_depth;

   assert(deferred);
   (void)deferred;

   vlog_clear_flags(proto, VLOG_F_EXTERN);

   vlog_symtab_pop(symtab);
   vlog_symtab_pop(symtab);
   for (int i = 0; i < depth; i++)
      vlog_symtab_pop(symtab);

   proc_automatic = saved_proc;
   subr_automatic = saved_subr;

   flush_deferred_bodies(false);
}

static bool want_defer_body(void)
{
   return class_depth > 0 || forward_classes.count > 0 || flushing_bodies;
}

static void p_task_body_declaration(vlog_node_t task)
{
   // [ interface_identifier . | class_scope ] task_identifier ;
   //    { tf_item_declaration } { statement_or_null }
   //    endtask [ : task_identifier ]
   // | [ interface_identifier . | class_scope ] task_identifier
   //    ( [ tf_port_list ] ) ; { block_item_declaration }
   //    { statement_or_null } endtask [ : task_identifier ]

   BEGIN("task body declaration");

   if (peek() == tID && peek_nth(2) == tSCOPE) {
      p_extern_subr_body(task);
      return;
   }

   ident_t id = p_identifier();
   vlog_set_ident(task, id);
   vlog_set_loc(task, &state.last_loc);

   vlog_symtab_put(symtab, task);
   note_block_name(id);

   vlog_symtab_push(symtab, task);

   const vlog_kind_t old_param_kind = param_kind;
   param_kind = V_LOCALPARAM;

   if (optional(tLPAREN)) {
      if (peek() != tRPAREN)
         p_tf_port_list(task);
      consume(tRPAREN);

      consume(tSEMI);

      skip_over_attributes();

      while (!parsing_prototype && !want_defer_body()
             && scan_block_item_declaration()) {
         p_block_item_declaration(task);
         skip_over_attributes();
      }
   }
   else {
      consume(tSEMI);

      skip_over_attributes();

      while (!parsing_prototype && scan_tf_item_declaration()) {
         p_tf_item_declaration(task);
         skip_over_attributes();
      }
   }

   if (!parsing_prototype && !defer_subr_body(task, tENDTASK)) {
      while (not_at_token(tENDTASK)) {
         vlog_node_t s = p_statement_or_null();
         if (s != NULL)
            vlog_add_stmt(task, s);
      }

      consume(tENDTASK);

      if (optional(tCOLON)) {
         ident_t name = p_identifier();
         if (id != name)
            error_at(&state.last_loc, "'%s' does not match task name '%s'",
                     istr(name), istr(id));
      }
   }

   vlog_symtab_pop(symtab);
   param_kind = old_param_kind;
}

static bool p_lifetime(void)
{
   // static | automatic

   BEGIN("lifetime");

   return one_of(tAUTOMATIC, tSTATIC) == tAUTOMATIC;
}

static bool p_method_lifetime(void)
{
   // A method of a class always has automatic lifetime (8.6)

   const bool automatic = p_lifetime();
   if (!automatic && class_depth > 0) {
      diag_t *d = diag_new(relaxed_rules() ? DIAG_WARN : DIAG_ERROR,
                           &state.last_loc);
      diag_printf(d, "a class method cannot have a static lifetime");
      diag_hint(d, &state.last_loc, "a static method is declared with "
                "static before task or function");
      diag_emit(d);
   }

   return automatic;
}

static vlog_node_t p_task_declaration(void)
{
   // task [ dynamic_override_specifiers ] [ lifetime ] task_body_declaration

   BEGIN("task declaration");

   if (capture_generic_extern(tENDTASK))
      return NULL;

   vlog_node_t v = vlog_new(V_TASK_DECL);

   consume(tTASK);

   const bool saved_automatic = proc_automatic;
   vlog_node_t saved_func = current_func;

   proc_automatic = subr_automatic;
   current_func = NULL;
   in_subroutine = true;

   if (scan(tAUTOMATIC, tSTATIC))
      proc_automatic = p_method_lifetime();

   p_task_body_declaration(v);

   proc_automatic = saved_automatic;
   current_func = saved_func;
   in_subroutine = false;

   if (extern_definition) {
      extern_definition = false;
      return NULL;
   }

   vlog_set_loc(v, CURRENT_LOC);
   return v;
}

static vlog_node_t p_function_data_type_or_implicit(void)
{
   // data_type_or_void | implicit_data_type

   BEGIN("function data type or implicit");

   switch (peek()) {
   case tREG:
   case tSTRUCT:
   case tUNION:
   case tENUM:
   case tSVINT:
   case tINTEGER:
   case tSVREAL:
   case tSHORTREAL:
   case tREALTIME:
   case tLOGIC:
   case tBIT:
   case tSHORTINT:
   case tSTRINGK: case tCHANDLE:
   case tEVENT:
   case tVOID:
   case tLONGINT:
   case tBYTE:
   case tTIME:
      return p_data_type_or_void();
   case tVIRTUAL:
      return p_data_type();
   case tID:
      {
         if (is_scoped_type_start() || peek_nth(2) == tHASH)
            return p_data_type();

         vlog_node_t dt = peek_reference();
         if (dt != NULL && vlog_kind(dt) == V_CLASS_DECL
             && (vlog_flags(dt) & VLOG_F_GENERIC))
            return p_data_type();
         else if (dt != NULL && is_data_type(dt)) {
            if (peek_nth(2) == tLSQUARE)
               return p_data_type();

            consume(tID);
            return dt;
         }
         else
            return p_implicit_data_type();
      }
   default:
      return p_implicit_data_type();
   }
}

static void p_function_body_declaration(vlog_node_t func)
{
   // function_data_type_or_implicit [ interface_identifier . | class_scope ]
   //    function_identifier ; { tf_item_declaration }
   //    { function_statement_or_null } endfunction [ : function_identifier ]
   // | function_data_type_or_implicit [ interface_identifier . | class_scope ]
   //    function_identifier ( [ tf_port_list ] ) ; { block_item_declaration }
   //    { function_statement_or_null } endfunction [ : function_identifier ]

   BEGIN("function body declaration");

   vlog_node_t rtype = p_function_data_type_or_implicit();

   if (peek() == tID && peek_nth(2) == tSCOPE) {
      p_extern_subr_body(func);
      return;
   }

   vlog_set_type(func, rtype);

   ident_t id = p_identifier();
   vlog_set_ident(func, id);
   vlog_set_ident2(func, vlog_symtab_qualified(symtab));
   vlog_set_loc(func, &state.last_loc);

   vlog_symtab_put(symtab, func);

   vlog_symtab_push(symtab, func);

   const vlog_kind_t old_param_kind = param_kind;
   param_kind = V_LOCALPARAM;

   if (optional(tLPAREN)) {
      if (peek() != tRPAREN)
         p_tf_port_list(func);
      consume(tRPAREN);

      consume(tSEMI);

      skip_over_attributes();

      while (!parsing_prototype && !want_defer_body()
             && scan_block_item_declaration()) {
         p_block_item_declaration(func);
         skip_over_attributes();
      }
   }
   else {
      consume(tSEMI);

      skip_over_attributes();

      while (!parsing_prototype && scan_tf_item_declaration()) {
         p_tf_item_declaration(func);
         skip_over_attributes();
      }
   }

   if (!parsing_prototype && !defer_subr_body(func, tENDFUNCTION)) {
      while (not_at_token(tENDFUNCTION)) {
         vlog_node_t s = p_statement_or_null();
         if (s != NULL)
            vlog_add_stmt(func, s);
      }

      consume(tENDFUNCTION);

      if (optional(tCOLON)) {
         ident_t name = p_identifier();
         if (id != name)
            error_at(&state.last_loc, "'%s' does not match function name '%s'",
                     istr(name), istr(id));
      }
   }

   vlog_symtab_pop(symtab);
   param_kind = old_param_kind;
}

static vlog_node_t p_function_declaration(void)
{
   // function [ lifetime ] function_body_declaration

   BEGIN("function declaration");

   if (capture_generic_extern(tENDFUNCTION))
      return NULL;

   vlog_node_t v = vlog_new(V_FUNC_DECL);

   consume(tFUNCTION);

   const bool saved_automatic = proc_automatic;
   vlog_node_t saved_func = current_func;

   proc_automatic = subr_automatic;
   current_func = v;
   in_subroutine = true;

   if (scan(tAUTOMATIC, tSTATIC))
      proc_automatic = p_method_lifetime();

   if (peek() == tID && peek_nth(2) == tSCOPE && peek_nth(3) == tNEW)
      p_extern_constructor();
   else
      p_function_body_declaration(v);

   proc_automatic = saved_automatic;
   current_func = saved_func;
   in_subroutine = false;

   if (extern_definition) {
      extern_definition = false;
      return NULL;
   }

   vlog_set_loc(v, CURRENT_LOC);
   return v;
}

static vlog_node_t p_constant_param_expression(void)
{
   // mintypmax_expression | data_type | $

   BEGIN("constant parameter expression");

   return p_mintypmax_expression();
}

static vlog_node_t p_param_assignment(vlog_node_t datatype, vlog_kind_t kind)
{
   // parameter_identifier { unpacked_dimension }
   //   [ = constant_param_expression ]

   BEGIN("parameter assignment");

   vlog_node_t v = vlog_new(kind);
   vlog_set_ident(v, p_identifier());
   vlog_node_t dt = p_variable_dimensions(datatype, v);
   vlog_set_type(v, dt);

   if (optional(tEQ)) {
      if (((peek() == tTICK && peek_nth(2) == tLBRACE)
           || (peek() == tLBRACE && dt != NULL && is_fixed_unpacked(v, dt)))
          && dt != NULL && vlog_get_type(dt) != NULL) {
         pattern_array_elems = is_fixed_unpacked(v, dt);
         vlog_set_value(v, p_assignment_pattern_expression(vlog_get_type(dt)));
         pattern_array_elems = false;
      }
      else
         vlog_set_value(v, p_constant_param_expression());
   }

   vlog_set_loc(v, CURRENT_LOC);
   return v;
}

static void p_list_of_param_assignments(vlog_node_t parent,
                                        vlog_node_t datatype,
                                        vlog_kind_t kind)
{
   // param_assignment { , param_assignment }

   BEGIN("list of parameter assignments");

   do {
      vlog_node_t v = p_param_assignment(datatype, kind);
      vlog_symtab_put(symtab, v);
      vlog_add_decl(parent, v);
   } while (peek_nth(2) == tID && optional(tCOMMA));
}

static void p_type_assignments(vlog_node_t parent, bool is_param)
{
   // type_identifier [ = data_type ] { , type_identifier [ = data_type ] }
   //
   // A type parameter is an alias of its default as it cannot be overridden

   BEGIN("type assignments");

   do {
      vlog_node_t v = vlog_new(V_TYPE_DECL);
      vlog_set_ident(v, p_identifier());

      if (is_param)
         vlog_set_flags(v, VLOG_F_TYPEPARAM);

      if (optional(tEQ))
         vlog_set_type(v, p_data_type());
      else
         vlog_set_type(v, logic_type());

      vlog_set_loc(v, CURRENT_LOC);
      vlog_symtab_put(symtab, v);
      vlog_add_decl(parent, v);

      if (peek() == tCOMMA && peek_nth(2) == tID)
         consume(tCOMMA);
      else
         break;
   } while (true);
}

static void p_parameter_declaration(vlog_node_t mod)
{
   // parameter data_type_or_implicit list_of_param_assignments

   BEGIN("parameter declaration");

   consume(tPARAMETER);

   if (optional(tTYPE)) {
      p_type_assignments(mod, true);
      return;
   }

   vlog_node_t dt = p_data_type_or_implicit();
   p_list_of_param_assignments(mod, dt, param_kind);
}

static void p_local_parameter_declaration(vlog_node_t parent)
{
   // localparam data_type_or_implicit list_of_param_assignments

   BEGIN("local parameter declaration");

   consume(tLOCALPARAM);

   if (optional(tTYPE)) {
      p_type_assignments(parent, false);
      return;
   }

   vlog_node_t dt = p_data_type_or_implicit();
   p_list_of_param_assignments(parent, dt, V_LOCALPARAM);
}

static void p_class_property(vlog_node_t parent)
{
   // { property_qualifier } data_declaration
   //   | const { class_item_qualifier } data_type const_identifier
   //       [ = constant_expression ] ;

   BEGIN("class property");

   const int first = vlog_decls(parent);

   p_data_declaration(parent, V_VAR_DECL);

   const int ndecls = vlog_decls(parent);
   for (int i = first; i < ndecls; i++) {
      vlog_node_t d = vlog_decl(parent, i);
      if (vlog_kind(d) == V_VAR_DECL)
         vlog_set_flags(d, vlog_flags(d) | VLOG_F_PROPERTY);
   }
}

static void p_class_constructor_arg(vlog_node_t parent, v_port_kind_t *kind,
                                    vlog_node_t *dt)
{
   // tf_port_item | default

   BEGIN("class constructor argument");

   vlog_node_t v = p_tf_port_item(kind, dt);
   vlog_symtab_put(symtab, v);
   vlog_add_param(parent, v);
}

static void p_class_constructor_arg_list(vlog_node_t parent)
{
   // class_constructor_arg { , class_constructor_arg }

   BEGIN("class constructor argument list");

   v_port_kind_t kind = V_PORT_INPUT;
   vlog_node_t dt = NULL;
   do {
      p_class_constructor_arg(parent, &kind, &dt);
   } while (optional(tCOMMA));
}

static vlog_node_t p_class_constructor_declaration(bool prototype)
{
   // function [ class_scope ] new [ ( [ class_constructor_arg_list ] ) ] ;
   //   { block_item_declaration }
   //   [ super . new [ ( [ list_of_arguments | default ] ) ] ; ]
   //   { function_statement_or_null }
   //   endfunction [ : new]

   BEGIN("class constructor declaration");

   consume(tFUNCTION);
   consume(tNEW);

   vlog_node_t v = vlog_new(V_CONSTRUCTOR);
   vlog_set_ident(v, ident_new("new"));

   vlog_symtab_push(symtab, v);

   if (optional(tLPAREN)) {
      if (not_at_token(tRPAREN))
         p_class_constructor_arg_list(v);

      consume(tRPAREN);
   }

   consume(tSEMI);

   if (prototype) {
      vlog_symtab_pop(symtab);
      vlog_set_flags(v, VLOG_F_EXTERN);
      vlog_set_loc(v, CURRENT_LOC);
      return v;
   }

   const loc_t loc = *CURRENT_LOC;

   if (!defer_subr_body(v, tENDFUNCTION))
      should_not_reach_here();   // Always in a class

   vlog_symtab_pop(symtab);

   vlog_set_loc(v, &loc);
   return v;
}

static bool scan_nth_qualifier(void)
{
   // Other qualifiers can follow const

   switch (peek_nth(2)) {
   case tSTATIC:
   case tVLOCAL:
   case tVPROTECTED:
      return true;
   default:
      return false;
   }
}

static bool at_covergroup(void)
{
   return peek() == tID && peek_ident_nth(1) == ident_new("covergroup");
}

////////////////////////////////////////////////////////////////////////////////
// Functional coverage

#define COV_PUSH(arr, n, val) do {                                      \
      (arr) = xrealloc_array((arr), (n) + 1, sizeof(*(arr)));           \
      (arr)[(n)++] = (val);                                             \
   } while (0)

typedef struct {
   cov_group_t group;
   bool        failed;
   ident_t    *enum_args;
   int         nenum_args;
   ident_t    *class_args;
   vlog_node_t *class_types;
   int         nclass_args;
} cov_parse_t;

static token_t replay_lex(void);
static void p_class_item(vlog_node_t parent);
static void p_package_item(vlog_node_t parent);
extern int yylex(void);

static bool cov_at_id(const char *name)
{
   return peek() == tID && peek_ident_nth(1) == ident_new(name);
}

static tokenq_t cov_take(void)
{
   (void)peek();
   tokenq_t t = state.tokenq[state.tokenq_tail];
   drop_token(&state);
   return t;
}

static cov_toks_t cov_capture(const token_t *stops, bool rep_stop)
{
   // Take tokens up to one of the stop tokens outside any brackets

   tokenq_t *t = NULL;
   int n = 0, depth = 0;
   for (;;) {
      const token_t tok = peek();
      if (tok == tEOF)
         break;

      if (depth == 0 && !(n == 0 && tok == tLBRACE)) {
         bool stop = false;
         for (const token_t *s = stops; *s != tEOF; s++)
            stop |= (*s == tok);

         if (stop)
            break;

         if (rep_stop && n > 0 && (tok == tTIMESRPT || tok == tGOTORPT
                                   || tok == tEQRPT))
            break;
      }

      if (tok == tLPAREN || tok == tLSQUARE || tok == tLBRACE)
         depth++;
      else if (tok == tRPAREN || tok == tRSQUARE || tok == tRBRACE) {
         if (depth == 0)
            break;
         depth--;
      }

      COV_PUSH(t, n, cov_take());
   }

   return (cov_toks_t){ t, n };
}

static bool cov_sorry_at(cov_parse_t *cp, const loc_t *loc, const char *what)
{
   if (!cp->failed)
      error_at(loc, "sorry, %s is not supported in a covergroup", what);

   cp->failed = true;
   return false;
}

static bool cov_sorry(cov_parse_t *cp, const char *what)
{
   return cov_sorry_at(cp, &(state.tokenq[state.tokenq_tail].loc), what);
}

static bool cov_expect_nonempty(cov_parse_t *cp, cov_toks_t t)
{
   if (t.n > 0)
      return true;

   if (!cp->failed)
      parse_error(&(state.tokenq[state.tokenq_tail].loc), "expected an "
                  "expression in the covergroup");

   cp->failed = true;
   return false;
}

static bool cov_is_dollar(cov_toks_t t)
{
   return t.n == 1 && t.t[0].token == tDOLLAR;
}

static void p_cov_range_list(cov_parse_t *cp, cov_item_t **items, int *n)
{
   // open_range_list without the braces

   static const token_t stops_value[] = { tCOMMA, tRBRACE, tEOF };
   static const token_t stops_lo[] = { tCOLON, tRSQUARE, tEOF };
   static const token_t stops_hi[] = { tRSQUARE, tEOF };

   do {
      cov_item_t it = {};

      if (peek() == tLSQUARE) {
         consume(tLSQUARE);
         it.range = true;
         it.lo = cov_capture(stops_lo, false);
         if (optional(tCOLON))
            it.hi = cov_capture(stops_hi, false);
         else
            it.hi = it.lo;
         consume(tRSQUARE);

         it.hi_dollar = cov_is_dollar(it.hi);
      }
      else
         it.lo = cov_capture(stops_value, false);

      it.lo_dollar = cov_is_dollar(it.lo);

      if (!cov_expect_nonempty(cp, it.lo))
         return;

      COV_PUSH(*items, *n, it);
   } while (optional(tCOMMA));
}

static void p_cov_iff(cov_toks_t *iff, bool *has_iff)
{
   static const token_t stops[] = { tRPAREN, tEOF };

   consume(tIFFKW);
   consume(tLPAREN);
   *iff = cov_capture(stops, false);
   consume(tRPAREN);
   *has_iff = true;
}

static bool p_cov_trans_list(cov_parse_t *cp, cov_bin_t *b)
{
   // trans_list ::= ( trans_set ) { , ( trans_set ) }

   static const token_t stops_step[] = { tCOMMA, tASSOC, tRPAREN, tEOF };
   static const token_t stops_lo[] = { tCOLON, tRSQUARE, tEOF };
   static const token_t stops_hi[] = { tRSQUARE, tEOF };

   do {
      consume(tLPAREN);

      cov_seq_t q = {};
      do {
         cov_step_t st = {};

         do {
            cov_item_t it = {};
            if (peek() == tLSQUARE) {
               consume(tLSQUARE);
               it.range = true;
               it.lo = cov_capture(stops_lo, false);
               if (optional(tCOLON))
                  it.hi = cov_capture(stops_hi, false);
               else
                  it.hi = it.lo;
               consume(tRSQUARE);
               it.hi_dollar = cov_is_dollar(it.hi);
            }
            else
               it.lo = cov_capture(stops_step, true);

            it.lo_dollar = cov_is_dollar(it.lo);

            if (!cov_expect_nonempty(cp, it.lo))
               return false;

            COV_PUSH(st.items, st.nitems, it);
         } while (optional(tCOMMA));

         if (peek() == tTIMESRPT || peek() == tGOTORPT
             || peek() == tEQRPT) {
            st.rkind = peek() == tGOTORPT ? 1 : peek() == tEQRPT ? 2 : 0;
            consume(peek());
            st.rep = true;
            st.rmin = cov_capture(stops_lo, false);
            if (optional(tCOLON))
               st.rmax = cov_capture(stops_hi, false);
            else
               st.rmax = st.rmin;
            consume(tRSQUARE);

            if (!cov_expect_nonempty(cp, st.rmin))
               return false;
         }

         COV_PUSH(q.steps, q.nsteps, st);
      } while (optional(tASSOC));

      consume(tRPAREN);

      COV_PUSH(b->seqs, b->nseqs, q);
   } while (optional(tCOMMA));

   return true;
}

static bool p_cov_bins(cov_parse_t *cp, cov_cp_t *c)
{
   // [ wildcard ] bins_keyword bin_identifier [ [ [ expression ] ] ]
   //    = ... [ iff ( expression ) ] ;

   cov_bin_t b = {};

   if (cov_at_id("wildcard")) {
      consume(tID);
      b.wildcard = true;
   }

   const ident_t kw = p_identifier();
   if (kw == ident_new("bins"))
      b.kind = COV_REGULAR;
   else if (kw == ident_new("ignore_bins"))
      b.kind = COV_IGNORE;
   else if (kw == ident_new("illegal_bins"))
      b.kind = COV_ILLEGAL;
   else {
      parse_error(&state.last_loc, "unexpected %s in a coverpoint", istr(kw));
      cp->failed = true;
      return false;
   }

   b.name = p_identifier();

   if (optional(tLSQUARE)) {
      static const token_t stops[] = { tRSQUARE, tEOF };
      if (peek() == tRSQUARE)
         b.array = 1;
      else {
         b.array = 2;
         b.size = cov_capture(stops, false);
      }
      consume(tRSQUARE);
   }

   consume(tEQ);

   switch (peek()) {
   case tLBRACE:
      consume(tLBRACE);
      p_cov_range_list(cp, &b.items, &b.nitems);
      consume(tRBRACE);
      if (peek() == tWITH) {
         static const token_t stops[] = { tRPAREN, tEOF };
         consume(tWITH);
         consume(tLPAREN);
         b.has_with = true;
         b.with_expr = cov_capture(stops, false);
         consume(tRPAREN);
         if (!cov_expect_nonempty(cp, b.with_expr))
            return false;
         if (b.wildcard)
            return cov_sorry(cp, "a with clause on a wildcard bin");
      }
      break;
   case tLPAREN:
      b.trans = true;
      if (!p_cov_trans_list(cp, &b))
         return false;
      if (b.array == 2)
         return cov_sorry(cp, "a sized array of transition bins");
      if (b.array == 1) {
         for (int i = 0; i < b.nseqs; i++) {
            for (int j = 0; j < b.seqs[i].nsteps; j++) {
               if (b.seqs[i].steps[j].rkind != 0)
                  return cov_sorry(cp, "an array of transition bins with a "
                                   "goto or non-consecutive repetition");
            }
         }
      }
      break;
   case tDEFAULT:
      consume(tDEFAULT);
      if (peek() == tSEQUENCE)
         consume(tSEQUENCE);
      b.kind = COV_DEFAULT;
      break;
   default:
      if (peek() == tID && peek_ident_nth(1) == c->label
          && peek_nth(2) == tWITH) {
         // The values of the coverpoint itself selected by a with clause
         static const token_t stops[] = { tRPAREN, tEOF };
         consume(tID);
         consume(tWITH);
         consume(tLPAREN);

         cov_item_t all = { .range = true, .lo_dollar = true,
                            .hi_dollar = true };
         COV_PUSH(b.items, b.nitems, all);

         b.has_with = true;
         b.with_expr = cov_capture(stops, false);
         consume(tRPAREN);
         if (!cov_expect_nonempty(cp, b.with_expr))
            return false;
         if (b.wildcard)
            return cov_sorry(cp, "a with clause on a wildcard bin");
         break;
      }

      {
         // An array or queue of values evaluated when the covergroup
         // is constructed (19.5.1.2)
         static const token_t stops[] = { tIFFKW, tSEMI, tEOF };
         b.has_set = true;
         b.set_expr = cov_capture(stops, false);
         if (!cov_expect_nonempty(cp, b.set_expr))
            return false;
         if (b.wildcard)
            return cov_sorry(cp, "a wildcard bin defined by a set "
                             "expression");
      }
   }

   if (peek() == tIFFKW)
      p_cov_iff(&b.iff, &b.has_iff);

   consume(tSEMI);

   COV_PUSH(c->bins, c->nbins, b);
   return true;
}

static bool p_cov_option(cov_option_t **opts, int *n)
{
   // option . member_identifier = expression ;

   static const token_t stops[] = { tSEMI, tEOF };

   cov_option_t o = {};
   o.type_option = cov_at_id("type_option");
   consume(tID);
   consume(tDOT);
   o.name = p_identifier();
   consume(tEQ);
   o.value = cov_capture(stops, false);
   consume(tSEMI);

   COV_PUSH(*opts, *n, o);
   return o.value.n > 0;
}

static bool cov_is_enum_type(vlog_node_t type)
{
   return type != NULL && vlog_kind(type) == V_ENUM_DECL;
}

static vlog_node_t cov_member_type(vlog_node_t type, ident_t name)
{
   for (; type != NULL; ) {
      switch (vlog_kind(type)) {
      case V_CLASS_DECL:
      case V_STRUCT_DECL:
      case V_UNION_DECL:
         break;
      default:
         return NULL;
      }

      const int ndecls = vlog_decls(type);
      for (int i = 0; i < ndecls; i++) {
         vlog_node_t d = vlog_decl(type, i);
         if (vlog_kind(d) == V_VAR_DECL && vlog_ident(d) == name)
            return vlog_get_type(d);
      }

      if (vlog_kind(type) == V_CLASS_DECL && vlog_has_ref(type))
         type = vlog_ref(type);
      else
         return NULL;
   }

   return NULL;
}

static bool cov_expr_is_enum(const cov_parse_t *cp, cov_toks_t e,
                             ident_t *cast)
{
   // Only names, members of classes and structures and casts are recognised

   if (e.n < 1 || e.t[0].token != tID)
      return false;

   const ident_t first = e.t[0].lval.ident;

   vlog_node_t type = NULL;
   if (e.n == 1) {
      for (int i = 0; i < cp->nenum_args; i++) {
         if (cp->enum_args[i] == first)
            return true;
      }
   }

   vlog_node_t decl = vlog_symtab_query(symtab, first);
   if (decl == NULL) {
      for (int i = 0; i < cp->nclass_args; i++) {
         if (cp->class_args[i] == first)
            type = cp->class_types[i];
      }
      if (type == NULL)
         return false;
   }
   else switch (vlog_kind(decl)) {
   case V_VAR_DECL:
   case V_NET_DECL:
   case V_PORT_DECL:
   case V_TF_PORT_DECL:
   case V_LOCAL_DECL:
      type = vlog_get_type(decl);
      break;
   case V_TYPE_DECL:
   case V_ENUM_DECL:
      // A cast to an enumeration type
      if (e.n > 3 && e.t[1].token == tTICK && e.t[2].token == tLPAREN
          && e.t[e.n - 1].token == tRPAREN
          && cov_is_enum_type(vlog_get_type(decl))) {
         *cast = first;
         return true;
      }
      return false;
   default:
      return false;
   }

   for (int i = 1; i + 1 < e.n && type != NULL; i += 2) {
      if (e.t[i].token != tDOT || e.t[i + 1].token != tID)
         return false;
      type = cov_member_type(type, e.t[i + 1].lval.ident);
   }

   return cov_is_enum_type(type);
}

static bool p_cov_coverpoint(cov_parse_t *cp, ident_t label)
{
   // [ [ data_type_or_implicit ] cover_point_identifier : ] coverpoint
   //    expression [ iff ( expression ) ] bins_or_empty

   static const token_t stops_expr[] = { tIFFKW, tLBRACE, tSEMI, tEOF };

   consume(tID);   // coverpoint

   cov_cp_t c = {};
   c.expr = cov_capture(stops_expr, false);
   if (!cov_expect_nonempty(cp, c.expr))
      return false;

   if (peek() == tIFFKW)
      p_cov_iff(&c.iff, &c.has_iff);

   if (label != NULL) {
      // The label becomes a variable of the covergroup which would hide
      // the name in the expression
      for (int i = 0; i < c.expr.n; i++) {
         if (c.expr.t[i].token == tID && c.expr.t[i].lval.ident == label)
            return cov_sorry(cp, "a coverpoint label that is also a name in "
                             "its expression");
      }

      c.label = label;
   }
   else if (c.expr.n == 1 && c.expr.t[0].token == tID)
      c.label = c.expr.t[0].lval.ident;
   else {
      c.label = ident_sprintf("cp_%d", cp->group.ncps + 1);
      c.implicit = true;
   }

   c.implicit |= label == NULL;

   if (optional(tLBRACE)) {
      while (not_at_token(tRBRACE, tEOF)) {
         if (cov_at_id("option") || cov_at_id("type_option")) {
            if (!p_cov_option(&c.opts, &c.nopts))
               return false;
         }
         else if (peek() == tSEMI)
            consume(tSEMI);
         else if (!p_cov_bins(cp, &c))
            return false;
      }
      consume(tRBRACE);
      optional(tSEMI);
   }
   else
      consume(tSEMI);

   c.is_enum = cov_expr_is_enum(cp, c.expr, &c.enum_cast);

   COV_PUSH(cp->group.cps, cp->group.ncps, c);
   return true;
}

static cov_sel_t *cov_sel_new(cov_sel_kind_t kind)
{
   cov_sel_t *s = xcalloc(sizeof(cov_sel_t));
   s->kind = kind;
   return s;
}

static cov_sel_t *p_cov_select_or(cov_parse_t *cp);

static cov_sel_t *p_cov_select_with(cov_parse_t *cp, cov_sel_t *s)
{
   static const token_t stops[] = { tRPAREN, tEOF };

   while (peek() == tWITH) {
      consume(tWITH);
      consume(tLPAREN);

      cov_sel_t *w = cov_sel_new(COV_SEL_WITH);
      w->left = s;
      w->with = cov_capture(stops, false);
      consume(tRPAREN);

      if (!cov_expect_nonempty(cp, w->with))
         return NULL;

      s = w;
   }

   return s;
}

static int cov_find_cp(const cov_parse_t *cp, ident_t name);

static cov_sel_t *p_cov_select_base(cov_parse_t *cp)
{
   if (optional(tBANG)) {
      cov_sel_t *s = cov_sel_new(COV_SEL_NOT);
      s->left = p_cov_select_base(cp);
      return s->left == NULL ? NULL : s;
   }
   else if (optional(tLPAREN)) {
      cov_sel_t *s = p_cov_select_or(cp);
      consume(tRPAREN);
      return s;
   }
   else if (cov_at_id("binsof")) {
      consume(tID);
      consume(tLPAREN);

      cov_sel_t *s = cov_sel_new(COV_SEL_BINSOF);
      s->cp = p_identifier();

      // The name of a coverpoint may be hierarchical so the longest prefix
      // that names one is the coverpoint and any name after it is a bin
      ident_t parts[16];
      int nparts = 0;
      while (peek() == tDOT && peek_nth(2) == tID && nparts < 16) {
         consume(tDOT);
         parts[nparts++] = p_identifier();
      }

      int used = nparts;
      ident_t full = s->cp;
      for (int i = 0; i < nparts; i++)
         full = ident_sprintf("%s.%s", istr(full), istr(parts[i]));

      while (used > 0 && cov_find_cp(cp, full) < 0) {
         used--;
         full = s->cp;
         for (int i = 0; i < used; i++)
            full = ident_sprintf("%s.%s", istr(full), istr(parts[i]));
      }

      s->cp = full;
      if (used < nparts) {
         s->bin = parts[used];
         s->has_bin = true;
      }

      if (peek() == tLSQUARE) {
         cov_sorry(cp, "a bin index in binsof");
         return NULL;
      }

      consume(tRPAREN);

      if (optional(tINTERSECT)) {
         consume(tLBRACE);
         s->has_isect = true;
         p_cov_range_list(cp, &s->isect, &s->nisect);
         consume(tRBRACE);
      }

      return s;
   }
   else {
      cov_sorry(cp, "this select expression of a cross bin");
      return NULL;
   }
}

static cov_sel_t *p_cov_select_primary(cov_parse_t *cp)
{
   cov_sel_t *s = p_cov_select_base(cp);
   return s == NULL ? NULL : p_cov_select_with(cp, s);
}

static cov_sel_t *p_cov_select_and(cov_parse_t *cp)
{
   cov_sel_t *s = p_cov_select_primary(cp);
   while (s != NULL && optional(tDBLAMP)) {
      cov_sel_t *a = cov_sel_new(COV_SEL_AND);
      a->left = s;
      a->right = p_cov_select_primary(cp);
      s = a->right == NULL ? NULL : a;
   }

   return s;
}

static cov_sel_t *p_cov_select_or(cov_parse_t *cp)
{
   cov_sel_t *s = p_cov_select_and(cp);
   while (s != NULL && optional(tLOGOR)) {
      cov_sel_t *o = cov_sel_new(COV_SEL_OR);
      o->left = s;
      o->right = p_cov_select_and(cp);
      s = o->right == NULL ? NULL : o;
   }

   return s;
}

static int cov_find_cp(const cov_parse_t *cp, ident_t name)
{
   for (int i = 0; i < cp->group.ncps; i++) {
      if (cp->group.cps[i].label == name)
         return i;
   }

   return -1;
}

static bool p_cov_cross(cov_parse_t *cp, ident_t label)
{
   // [ cross_identifier : ] cross list_of_cross_items [ iff ( expression ) ]
   //    cross_body

   consume(tID);   // cross

   cov_cross_t x = {};
   x.label = label ?: ident_sprintf("cross_%d", cp->group.ncrosses + 1);

   do {
      if (peek() != tID) {
         parse_error(&(state.tokenq[state.tokenq_tail].loc), "expected the "
                     "name of a coverpoint or variable in cross");
         cp->failed = true;
         return false;
      }

      const loc_t loc = state.tokenq[state.tokenq_tail].loc;

      // A variable or a hierarchical name of one
      tokenq_t *t = xmalloc_array(2 * 16, sizeof(tokenq_t));
      int ntoks = 0;
      ident_t name = p_identifier();

      memset(&t[ntoks], 0, sizeof(tokenq_t));
      t[ntoks].token = tID;
      t[ntoks].lval.ident = name;
      t[ntoks++].loc = loc;

      while (peek() == tDOT && peek_nth(2) == tID && ntoks < 2 * 16 - 2) {
         consume(tDOT);

         memset(&t[ntoks], 0, sizeof(tokenq_t));
         t[ntoks].token = tDOT;
         t[ntoks++].loc = state.last_loc;

         const ident_t part = p_identifier();
         name = ident_sprintf("%s.%s", istr(name), istr(part));

         memset(&t[ntoks], 0, sizeof(tokenq_t));
         t[ntoks].token = tID;
         t[ntoks].lval.ident = part;
         t[ntoks++].loc = state.last_loc;
      }

      if (peek() == tLSQUARE)
         return cov_sorry(cp, "an expression in a cross");

      if (ntoks > 1) {
         // A cross item is a coverpoint or a variable identifier
         const loc_t *end = &(state.last_loc);
         const loc_t span = get_loc(loc.first_line, loc.first_column,
                                    end->first_line + end->line_delta,
                                    end->first_column + end->column_delta,
                                    loc.file_ref);
         diag_t *d = pedantic_diag(&span);
         if (d != NULL) {
            diag_printf(d, "cross items must be coverpoints or variables; "
                        "declare a coverpoint");
            diag_hint(d, &span, "%s is a hierarchical name", istr(name));
            diag_emit(d);
         }
      }

      if (cov_find_cp(cp, name) < 0) {
         // The implicit coverpoint of a variable
         cov_cp_t c = {};
         c.label = name;
         c.implicit = true;
         c.expr = (cov_toks_t){ t, ntoks };
         c.is_enum = cov_expr_is_enum(cp, c.expr, &c.enum_cast);
         COV_PUSH(cp->group.cps, cp->group.ncps, c);
      }
      else
         free(t);

      COV_PUSH(x.cps, x.ncps, name);
   } while (optional(tCOMMA));

   if (peek() == tIFFKW)
      p_cov_iff(&x.iff, &x.has_iff);

   if (optional(tLBRACE)) {
      while (not_at_token(tRBRACE, tEOF)) {
         if (cov_at_id("option") || cov_at_id("type_option")) {
            if (!p_cov_option(&x.opts, &x.nopts))
               return false;
         }
         else if (peek() == tSEMI)
            consume(tSEMI);
         else {
            cov_xbin_t b = {};
            const ident_t kw = p_identifier();
            if (kw == ident_new("bins"))
               b.kind = COV_REGULAR;
            else if (kw == ident_new("ignore_bins"))
               b.kind = COV_IGNORE;
            else if (kw == ident_new("illegal_bins"))
               b.kind = COV_ILLEGAL;
            else {
               parse_error(&state.last_loc, "unexpected %s in a cross",
                           istr(kw));
               cp->failed = true;
               return false;
            }

            b.name = p_identifier();

            if (peek() == tLSQUARE)
               return cov_sorry(cp, "an array of cross bins");

            consume(tEQ);

            b.sel = p_cov_select_or(cp);
            if (b.sel == NULL)
               return false;

            if (peek() == tIFFKW) {
               if (b.kind != COV_REGULAR)
                  return cov_sorry(cp, "an iff clause on an ignore or "
                                   "illegal cross bin");
               p_cov_iff(&b.iff, &b.has_iff);
            }

            consume(tSEMI);

            COV_PUSH(x.bins, x.nbins, b);
         }
      }
      consume(tRBRACE);
      optional(tSEMI);
   }
   else
      consume(tSEMI);

   COV_PUSH(cp->group.crosses, cp->group.ncrosses, x);
   return true;
}

static void cov_arg_names(cov_parse_t *cp, cov_toks_t args, ident_t **names,
                          int *n, cov_toks_t **decls, int *ndecls,
                          bool **refs, int *nrefs)
{
   // The name of each argument is the last identifier before any default
   // value and its type is the identifier before that

   int depth = 0, seg = 0;
   for (int i = 0; i <= args.n; i++) {
      token_t tok = tCOMMA;
      if (i < args.n) {
         tok = args.t[i].token;
         if (tok == tLPAREN || tok == tLSQUARE || tok == tLBRACE)
            depth++;
         else if (tok == tRPAREN || tok == tRSQUARE || tok == tRBRACE)
            depth--;
      }

      if (tok != tCOMMA || depth != 0)
         continue;

      int last = -1, prev = -1, d = 0;
      for (int j = seg; j < i; j++) {
         const token_t t = args.t[j].token;
         if (t == tLPAREN || t == tLSQUARE || t == tLBRACE)
            d++;
         else if (t == tRPAREN || t == tRSQUARE || t == tRBRACE)
            d--;
         else if (d == 0 && t == tEQ)
            break;
         else if (d == 0 && t == tID) {
            prev = last;
            last = j;
         }
      }

      if (last >= 0) {
         COV_PUSH(*names, *n, args.t[last].lval.ident);

         if (decls != NULL) {
            // A local of the same type stands in for the argument
            int start = seg, end = seg;
            for (int j = seg; j < i && args.t[j].token != tEQ; j++)
               end = j + 1;
            while (start < end
                   && (args.t[start].token == tINPUT
                       || args.t[start].token == tOUTPUT
                       || args.t[start].token == tINOUT
                       || args.t[start].token == tREF))
               start++;

            cov_toks_t decl = { args.t + start, end - start };
            COV_PUSH(*decls, *ndecls, decl);

            if (refs != NULL) {
               // A reference is a variable which is not copied
               bool is_ref = false;
               for (int j = seg; j < start; j++)
                  is_ref |= args.t[j].token == tREF;
               COV_PUSH(*refs, *nrefs, is_ref);
            }
         }

         if (prev >= 0) {
            vlog_node_t td = vlog_symtab_query(symtab, args.t[prev].lval.ident);
            if (td != NULL && vlog_kind(td) == V_TYPE_DECL
                && cov_is_enum_type(vlog_get_type(td)))
               COV_PUSH(cp->enum_args, cp->nenum_args,
                        args.t[last].lval.ident);
            else if (td != NULL) {
               vlog_node_t ct = vlog_kind(td) == V_TYPE_DECL
                  ? vlog_get_type(td) : td;
               if (vlog_kind(ct) == V_CLASS_DECL) {
                  cp->class_types = xrealloc_array(cp->class_types,
                                                   cp->nclass_args + 1,
                                                   sizeof(vlog_node_t));
                  cp->class_types[cp->nclass_args] = ct;
                  COV_PUSH(cp->class_args, cp->nclass_args,
                           args.t[last].lval.ident);
               }
            }
         }
      }

      seg = i + 1;
   }
}

static bool cov_replaying;

static void cov_replay(vlog_node_t parent, tokenq_t *toks, int ntoks)
{
   generic_class_t tokens = {
      .ntokens = ntoks,
      .tokens  = toks,
   };

   parse_state_t saved = state;
   generic_class_t *saved_class = replay_class;
   const int saved_pos = replay_pos;

   replay_class = &tokens;
   replay_pos = 0;

   state.lex_fn = replay_lex;
   state.tokenq_head = state.tokenq_tail = 0;
   state.start_loc = LOC_INVALID;

   const bool saved_replaying = cov_replaying;
   cov_replaying = true;

   while (peek() != tEOF) {
      switch (vlog_kind(parent)) {
      case V_CLASS_DECL:
         p_class_item(parent);
         break;
      case V_PACKAGE:
         p_package_item(parent);
         break;
      default:
         p_module_or_generate_item(parent);
         break;
      }
   }

   cov_replaying = saved_replaying;
   state = saved;
   replay_class = saved_class;
   replay_pos = saved_pos;
}

typedef struct {
   vlog_node_t  decl;
   vlog_node_t  nettype;
   vlog_node_t *drivers;
   int          ndrivers;
} user_net_t;

static A(user_net_t) user_nets = AINIT;

static void register_user_net(vlog_node_t decl, vlog_node_t nettype)
{
   vlog_set_flags(decl, vlog_flags(decl) | VLOG_F_USERNET);
   user_net_t net = { .decl = decl, .nettype = nettype };
   APUSH(user_nets, net);

   if (vlog_has_value(decl) && vlog_has_ident2(nettype)
       && current_module == module_root) {
      // The assignment in the declaration is a driver of the net
      vlog_node_t target = vlog_new(V_REF);
      vlog_set_ident(target, vlog_ident(decl));
      vlog_set_ref(target, decl);
      vlog_set_loc(target, vlog_loc(decl));

      vlog_node_t assign = vlog_new(V_ASSIGN);
      vlog_set_target(assign, user_net_driver(target));
      vlog_set_ident(assign, default_label("assign"));
      vlog_set_value(assign, vlog_value(decl));
      vlog_set_loc(assign, vlog_loc(decl));
      vlog_add_stmt(current_module, assign);
   }
}

static user_net_t *find_user_net(vlog_node_t decl)
{
   for (int i = 0; i < user_nets.count; i++) {
      if (user_nets.items[i].decl == decl)
         return &(user_nets.items[i]);
   }

   return NULL;
}

static void check_user_net_procedural(vlog_node_t lhs)
{
   vlog_node_t root = lhs;
   while (root != NULL && (vlog_kind(root) == V_BIT_SELECT
                           || vlog_kind(root) == V_PART_SELECT
                           || vlog_kind(root) == V_MEMBER_REF))
      root = vlog_value(root);

   if (root != NULL && vlog_kind(root) == V_REF && vlog_has_ref(root)
       && find_user_net(vlog_ref(root)) != NULL)
      error_at(vlog_loc(lhs), "'%s' cannot be assigned in a procedural block "
               "as it is a net of a user-defined nettype",
               istr(vlog_ident(root)));
}

static bool nettype_is_flat(vlog_node_t type)
{
   // A structure whose members are all integral or all real can be held
   // in a signal

   vlog_node_t st = vlog_get_type(type);
   if (st == NULL || vlog_kind(st) != V_STRUCT_DECL
       || (vlog_flags(st) & VLOG_F_PACKED))
      return false;

   int nreal = 0, nint = 0;
   const int ndecls = vlog_decls(st);
   for (int i = 0; i < ndecls; i++) {
      vlog_node_t d = vlog_decl(st, i);
      if (vlog_kind(d) != V_VAR_DECL || vlog_ranges(d) > 0)
         return false;

      vlog_node_t mt = vlog_get_type(vlog_type(d));
      if (mt == NULL)
         return false;
      else if (vlog_kind(mt) == V_DATA_TYPE
               && (vlog_subkind(mt) == DT_REAL
                   || vlog_subkind(mt) == DT_SHORTREAL
                   || vlog_subkind(mt) == DT_REALTIME))
         nreal++;
      else if (vlog_is_dynamic_type(mt))
         return false;
      else
         nint++;
   }

   return ndecls > 0 && (nreal == 0 || nint == 0);
}

static vlog_node_t user_net_driver(vlog_node_t target)
{
   // Each continuous assignment to a net with a resolution function
   // drives its own variable and the net is resolved from all of them

   if (target == NULL || vlog_kind(target) != V_REF || !vlog_has_ref(target)
       || current_module == NULL)
      return target;

   user_net_t *net = find_user_net(vlog_ref(target));
   if (net == NULL)
      return target;

   if (!vlog_has_ident2(net->nettype)) {
      if (net->ndrivers++ > 0)
         error_at(vlog_loc(target), "net '%s' of nettype %s without a "
                  "resolution function cannot have more than one driver",
                  istr(vlog_ident(net->decl)),
                  istr(vlog_ident(net->nettype)));
      return target;
   }

   // A net of an integral or real type, or of a structure whose members
   // are all integral or all real, is a signal resolved by calling the
   // resolution function with the values of all its drivers, which may be
   // anywhere in the design
   if (!vlog_is_dynamic_type(vlog_type(net->decl))
       || nettype_is_flat(vlog_type(net->decl)))
      return target;

   if (current_module != module_root) {
      error_at(vlog_loc(target), "sorry, a net of a user-defined net type "
               "with a resolution function cannot be driven from a generate "
               "block");
      return target;
   }

   vlog_node_t decl = net->decl;

   vlog_node_t d = vlog_new(V_VAR_DECL);
   vlog_set_ident(d, ident_sprintf("%s$d%d", istr(vlog_ident(decl)),
                                   net->ndrivers));
   vlog_set_type(d, vlog_type(decl));
   vlog_set_loc(d, vlog_loc(target));
   vlog_add_decl(current_module, d);
   vlog_symtab_put(symtab, d);

   net->drivers = xrealloc_array(net->drivers, net->ndrivers + 1,
                                 sizeof(vlog_node_t));
   net->drivers[net->ndrivers++] = d;

   vlog_node_t ref = vlog_new(V_REF);
   vlog_set_ident(ref, vlog_ident(d));
   vlog_set_ref(ref, d);
   vlog_set_loc(ref, vlog_loc(target));
   return ref;
}

static void expand_user_nets(vlog_node_t mod)
{
   // The value of the net is the result of calling the resolution function
   // whenever the value from a driver changes

   LOCAL_TEXT_BUF tb = tb_new();
   int nresolved = 0;

   for (int i = 0; i < user_nets.count; i++) {
      user_net_t *net = &(user_nets.items[i]);
      if (net->ndrivers > 0 && vlog_has_ident2(net->nettype)) {
         tb_printf(tb, "always_comb %s = %s('{", istr(vlog_ident(net->decl)),
                   istr(vlog_ident2(net->nettype)));
         for (int j = 0; j < net->ndrivers; j++)
            tb_printf(tb, "%s%s", j > 0 ? ", " : "",
                      istr(vlog_ident(net->drivers[j])));
         tb_cat(tb, "});\n");
         nresolved++;
      }

      free(net->drivers);
   }

   user_nets.count = 0;

   if (nresolved == 0)
      return;

   tb_cat(tb, "\n__cvend\n");

   const size_t len = tb_len(tb);
   char *copy = xmalloc(len + 1);
   memcpy(copy, tb_get(tb), len + 1);

   tokenq_t *toks = NULL;
   int ntoks = 0;

   scan_text_begin(copy, len);

   extern yylval_t yylval;
   extern loc_t yylloc;

   const loc_t *loc = vlog_loc(mod);
   for (;;) {
      const token_t tok = yylex();
      if (tok == tEOF)
         break;

      if (tok == tID && strcmp(istr(yylval.ident), "__cvend") == 0)
         break;

      yylloc = *loc;
      tokenq_t t = { tok, yylval, *loc };
      COV_PUSH(toks, ntoks, t);
   }

   scan_text_end();

   if (ntoks > 0) {
      tokenq_t eof = toks[ntoks - 1];
      eof.token = tEOF;
      COV_PUSH(toks, ntoks, eof);
      ntoks--;
   }

   cov_replay(mod, toks, ntoks);
}

static vlog_node_t bind_replay(cov_toks_t toks, bool bind)
{
   // Parse the captured tokens of a bind directive or a nested module

   tokenq_t *copy = NULL;
   int ncopy = 0;
   for (int i = 0; i < toks.n; i++)
      COV_PUSH(copy, ncopy, toks.t[i]);

   if (ncopy == 0)
      return NULL;

   tokenq_t semi = copy[ncopy - 1];
   if (bind) {
      semi.token = tSEMI;
      COV_PUSH(copy, ncopy, semi);
   }

   tokenq_t eof = semi;
   eof.token = tEOF;
   COV_PUSH(copy, ncopy, eof);
   ncopy--;

   generic_class_t tokens = {
      .ntokens = ncopy,
      .tokens  = copy,
   };

   parse_state_t saved = state;
   generic_class_t *saved_class = replay_class;
   const int saved_pos = replay_pos;

   replay_class = &tokens;
   replay_pos = 0;

   state.lex_fn = replay_lex;
   state.tokenq_head = state.tokenq_tail = 0;
   state.start_loc = LOC_INVALID;

   vlog_node_t v = p_description();

   state = saved;
   replay_class = saved_class;
   replay_pos = saved_pos;
   return v;
}

static void cov_expand(vlog_node_t parent, cov_group_t *g, const loc_t *loc)
{
   cov_text_t text = {
      .text = tb_new(),
   };

   const char *err = cov_generate(g, &text);
   if (err != NULL) {
      error_at(loc, "sorry, %s", err);
      g->ncps = g->ncrosses = 0;
      text.nslots = 0;
      tb_rewind(text.text);
      err = cov_generate(g, &text);
      assert(err == NULL);
   }

   tb_cat(text.text, "\n__cvend\n");

   // Lex the generated text and substitute the expressions of the
   // covergroup for the identifiers that stand for them

   const size_t len = tb_len(text.text);
   char *copy = xmalloc(len + 1);
   memcpy(copy, tb_get(text.text), len + 1);

   tokenq_t *toks = NULL;
   int ntoks = 0;

   scan_text_begin(copy, len);

   extern yylval_t yylval;
   extern loc_t yylloc;

   const char *prefix = cov_slot_prefix();
   const size_t plen = strlen(prefix);

   for (;;) {
      const token_t tok = yylex();
      if (tok == tEOF)
         break;

      if (tok == tID && strcmp(istr(yylval.ident), "__cvend") == 0)
         break;

      if (tok == tID && strncmp(istr(yylval.ident), prefix, plen) == 0) {
         const int slot = atoi(istr(yylval.ident) + plen);
         const cov_toks_t span = text.slots[slot];
         for (int i = 0; i < span.n; i++)
            COV_PUSH(toks, ntoks, span.t[i]);
         continue;
      }

      yylloc = *loc;
      tokenq_t t = { tok, yylval, *loc };
      COV_PUSH(toks, ntoks, t);
   }

   scan_text_end();

   if (ntoks > 0) {
      tokenq_t eof = toks[ntoks - 1];
      eof.token = tEOF;
      COV_PUSH(toks, ntoks, eof);
      ntoks--;
   }

   cov_replay(parent, toks, ntoks);

   tb_free(text.text);
}

static void p_covergroup_declaration(vlog_node_t parent)
{
   // covergroup covergroup_identifier [ ( [ tf_port_list ] ) ]
   //    [ coverage_event ] ; { coverage_spec_or_option } endgroup
   //    [ : covergroup_identifier ]

   BEGIN("covergroup declaration");

   consume(tID);

   cov_parse_t *cp = xcalloc(sizeof(cov_parse_t));
   cov_group_t *g = &(cp->group);

   g->name = p_identifier();
   const loc_t loc = state.last_loc;

   if (vlog_kind(parent) == V_CLASS_DECL) {
      // Inside the body of a specialisation the name of the class without
      // the parameters refers to the specialisation
      g->owner = ident_until(vlog_ident(parent), '@');
   }

   static const token_t stops_rparen[] = { tRPAREN, tEOF };

   if (optional(tLPAREN)) {
      g->ctor_args = cov_capture(stops_rparen, false);
      consume(tRPAREN);
      cov_arg_names(cp, g->ctor_args, &g->ctor_names, &g->nctor,
                    &g->ctor_decls, &g->nctor_decls, &g->ctor_ref,
                    &g->nctor_ref);

      // The covergroup only reads a reference
      const cov_toks_t args = g->ctor_args;
      tokenq_t *conv = NULL;
      int nconv = 0;
      for (int i = 0; i < args.n; i++) {
         if (args.t[i].token == tREF) {
            tokenq_t tok = args.t[i];
            tok.token = tCONST;
            COV_PUSH(conv, nconv, tok);
         }
         COV_PUSH(conv, nconv, args.t[i]);
      }
      g->ctor_args = (cov_toks_t){ conv, nconv };
   }

   if (peek() == tWITH) {
      consume(tWITH);
      consume(tFUNCTION);
      if (!cov_at_id("sample")) {
         parse_error(&(state.tokenq[state.tokenq_tail].loc), "expected "
                     "sample after with function");
         cp->failed = true;
      }
      else {
         consume(tID);
         consume(tLPAREN);
         g->sample_args = cov_capture(stops_rparen, false);
         consume(tRPAREN);
         cov_arg_names(cp, g->sample_args, &g->sample_names, &g->nsample,
                       &g->sample_decls, &g->nsample_decls, NULL, NULL);
      }
   }
   else if (peek() == tAT) {
      consume(tAT);
      g->has_event = true;
      if (optional(tLPAREN)) {
         g->event = cov_capture(stops_rparen, false);
         consume(tRPAREN);
      }
      else
         g->event = cov_capture((const token_t[]){ tSEMI, tEOF }, false);

      if (vlog_kind(parent) == V_PACKAGE)
         cov_sorry(cp, "a clocking event on a covergroup in a package");
   }

   consume(tSEMI);

   while (!cp->failed && !cov_at_id("endgroup") && peek() != tEOF) {
      if (cov_at_id("option") || cov_at_id("type_option")) {
         if (!p_cov_option(&g->opts, &g->nopts))
            cp->failed = true;
      }
      else if (cov_at_id("coverpoint"))
         p_cov_coverpoint(cp, NULL);
      else if (cov_at_id("cross"))
         p_cov_cross(cp, NULL);
      else if (peek() == tID && peek_nth(2) == tCOLON && peek_nth(3) == tID
               && peek_ident_nth(3) == ident_new("coverpoint")) {
         const ident_t label = p_identifier();
         consume(tCOLON);
         p_cov_coverpoint(cp, label);
      }
      else if (peek() == tID && peek_nth(2) == tCOLON && peek_nth(3) == tID
               && peek_ident_nth(3) == ident_new("cross")) {
         const ident_t label = p_identifier();
         consume(tCOLON);
         p_cov_cross(cp, label);
      }
      else if (peek() == tSEMI)
         consume(tSEMI);
      else {
         parse_error(&(state.tokenq[state.tokenq_tail].loc), "unexpected "
                     "%s in covergroup", token_str(peek()));
         cp->failed = true;
      }
   }

   if (cp->failed)
      g->ncps = g->ncrosses = 0;

   // Skip to the end after an error
   while (!cov_at_id("endgroup") && peek() != tEOF)
      drop_token(&state);

   if (peek() == tEOF)
      parse_error(&loc, "missing endgroup");
   else
      consume(tID);

   if (optional(tCOLON))
      (void)p_identifier();

   cov_expand(parent, g, &loc);
}

static vlog_flags_t p_class_item_qualifiers(void)
{
   // { static | protected | local | virtual | rand | randc | const | pure }

   vlog_flags_t flags = 0;
   for (;;) {
      switch (peek()) {
      case tCONST:
         if (!scan_nth_qualifier())
            return flags;
         consume(tCONST);
         break;
      case tSTATIC:
         consume(tSTATIC);
         flags |= VLOG_F_STATIC;
         break;
      case tVPROTECTED:
         consume(tVPROTECTED);
         break;
      case tVLOCAL:
         consume(tVLOCAL);
         break;
      case tVIRTUAL:
         if (peek_nth(2) == tCLASS || peek_nth(2) == tINTERFACE
             || peek_nth(2) == tID)
            return flags;
         consume(tVIRTUAL);
         flags |= VLOG_F_VIRTUAL;
         break;
      case tVPURE:
         consume(tVPURE);
         flags |= VLOG_F_PURE;
         break;
      case tEXTERN:
         consume(tEXTERN);
         flags |= VLOG_F_EXTERN;
         break;
      case tRAND:
         consume(tRAND);
         flags |= VLOG_F_RAND;
         break;
      case tRANDC:
         consume(tRANDC);
         flags |= VLOG_F_RANDC;
         break;
      case tREF:
         if (!cov_replaying)
            return flags;
         consume(tREF);
         flags |= VLOG_F_REFVAR;
         break;
      default:
         return flags;
      }
   }
}

static bool defer_constraint_body(vlog_node_t decl)
{
   if (forward_classes.count == 0 || class_depth == 0 || peek() != tLBRACE)
      return false;

   deferred_body_t d = {
      .subr           = decl,
      .scope          = vlog_symtab_current(symtab),
      .proc_automatic = proc_automatic,
      .subr_automatic = subr_automatic,
      .is_constraint  = true,
      .container      = spec_container,
   };

   int depth = 0;
   for (bool done = false; !done; ) {
      const token_t tok = peek();
      if (tok == tEOF)
         break;

      tokenq_t saved = state.tokenq[state.tokenq_tail];
      drop_token(&state);

      d.body.tokens = xrealloc_array(d.body.tokens, d.body.ntokens + 1,
                                     sizeof(tokenq_t));
      d.body.tokens[d.body.ntokens++] = saved;

      if (tok == tLBRACE)
         depth++;
      else if (tok == tRBRACE && --depth == 0)
         done = true;
   }

   APUSH(deferred_bodies, d);
   return true;
}

static vlog_node_t p_constraint_declaration(vlog_node_t class,
                                            vlog_flags_t flags)
{
   // [ static ] constraint constraint_identifier { { constraint_block_item } }

   BEGIN("constraint declaration");

   consume(tCONSTRAINT);

   vlog_node_t v = vlog_new(V_CONSTRAINT);
   vlog_set_ident(v, p_identifier());
   vlog_set_ref(v, class);

   if (flags & VLOG_F_STATIC)
      vlog_set_flags(v, VLOG_F_STATIC);

   if (peek() == tSEMI) {
      // Constraint prototype: the block is defined outside of the class
      // unless it is pure and is defined by a derived class
      consume(tSEMI);
      if (flags & VLOG_F_PURE)
         vlog_set_flags(v, VLOG_F_PURE);
      else if (flags & VLOG_F_EXTERN)
         vlog_set_flags(v, VLOG_F_EXTERN);
      else
         vlog_set_flags(v, VLOG_F_EXTERN | VLOG_F_IMPLICIT);
   }
   else if (flags & (VLOG_F_EXTERN | VLOG_F_PURE)) {
      error_at(&(state.tokenq[state.tokenq_tail].loc), "an extern or pure "
               "constraint must not have a block");
      consume(tLBRACE);
      while (not_at_token(tRBRACE))
         vlog_add_stmt(v, p_constraint_expression());
      consume(tRBRACE);
   }
   else if (defer_constraint_body(v)) {
      // The block may use a class which is declared later
   }
   else {
      consume(tLBRACE);

      while (not_at_token(tRBRACE)) {
         if (!optional(tSEMI))   // An empty item
            vlog_add_stmt(v, p_constraint_expression());
      }

      consume(tRBRACE);
   }

   vlog_set_loc(v, CURRENT_LOC);
   vlog_symtab_put(symtab, v);
   return v;
}

static void p_class_item(vlog_node_t parent)
{
   // { attribute_instance } class_property
   //   | { attribute_instance } class_method
   //   | { attribute_instance } class_constraint
   //   | { attribute_instance } class_declaration
   //   | { attribute_instance } covergroup_declaration
   //   | local_parameter_declaration ;
   //   | parameter_declaration | ;

   BEGIN("class item");

   optional_attributes();

   const vlog_flags_t flags = p_class_item_qualifiers();

   if (at_covergroup()) {
      p_covergroup_declaration(parent);
      return;
   }

   switch (peek()) {
   case tSEMI:
      consume(tSEMI);
      break;
   case tTASK:
   case tFUNCTION:
      {
         if (peek() == tFUNCTION && peek_nth(2) == tNEW) {
            vlog_node_t c =
               p_class_constructor_declaration(!!(flags & VLOG_F_EXTERN));
            vlog_set_ref(c, parent);
            vlog_add_decl(parent, c);
            break;
         }

         if (flags & (VLOG_F_RAND | VLOG_F_RANDC))
            error_at(&state.last_loc, "only class properties can be declared "
                     "rand or randc");

         if ((vlog_flags(parent) & VLOG_F_INTERFACE)
             && !(flags & VLOG_F_PURE))
            error_at(&(state.tokenq[state.tokenq_tail].loc), "methods of an "
                     "interface class must be pure virtual");

         parsing_prototype = !!(flags & (VLOG_F_PURE | VLOG_F_EXTERN));

         vlog_node_t m = peek() == tTASK
            ? p_task_declaration() : p_function_declaration();

         parsing_prototype = false;

         if (m == NULL)
            break;   // Out-of-class definition inside a class

         vlog_set_flags(m, vlog_flags(m) | flags);
         vlog_set_ref(m, parent);
         vlog_add_decl(parent, m);
      }
      break;
   case tCLASS:
      vlog_add_decl(parent, p_class_declaration());
      break;
   case tPARAMETER:
      p_parameter_declaration(parent);
      consume(tSEMI);
      break;
   case tLOCALPARAM:
      p_local_parameter_declaration(parent);
      consume(tSEMI);
      break;
   case tCONSTRAINT:
      if (vlog_flags(parent) & VLOG_F_INTERFACE)
         error_at(&state.last_loc, "an interface class cannot have "
                  "constraints");
      vlog_add_decl(parent, p_constraint_declaration(parent, flags));
      break;
   default:
      {
         const int first = vlog_decls(parent);
         p_class_property(parent);

         for (int i = first; i < vlog_decls(parent); i++) {
            vlog_node_t d = vlog_decl(parent, i);
            if (vlog_kind(d) == V_VAR_DECL
                && (vlog_flags(parent) & VLOG_F_INTERFACE))
               error_at(vlog_loc(d), "an interface class cannot have "
                        "properties");

            if (vlog_kind(d) == V_VAR_DECL) {
               vlog_set_flags(d, flags);
               if (flags & VLOG_F_STATIC)
                  vlog_set_ref(d, parent);
            }
         }
      }
      break;
   }
}

static void p_extern_constraint(void)
{
   // [ static ] constraint class_scope constraint_identifier
   //    constraint_block

   BEGIN("out-of-class constraint definition");

   if (capture_generic_constraint())
      return;

   const bool is_static = optional(tSTATIC);

   consume(tCONSTRAINT);

   ident_t cname = p_identifier();
   const loc_t cloc = state.last_loc;
   consume(tSCOPE);
   ident_t id = p_identifier();
   const loc_t mloc = state.last_loc;

   vlog_node_t class = spec_extern_class;
   if (class == NULL)
      class = vlog_symtab_query(symtab, cname);
   if (class != NULL && vlog_kind(class) == V_TYPE_DECL)
      class = vlog_get_type(class);

   vlog_node_t proto = NULL;
   if (class == NULL || vlog_kind(class) != V_CLASS_DECL) {
      error_at(&cloc, "no class named %pQ", cname);
      class = NULL;
   }
   else if (vlog_flags(class) & VLOG_F_GENERIC) {
      error_at(&cloc, "sorry, a constraint of a parameterised class cannot "
               "be defined outside of the class");
      class = NULL;
   }
   else {
      const int ndecls = vlog_decls(class);
      for (int i = 0; i < ndecls; i++) {
         vlog_node_t d = vlog_decl(class, i);
         if (vlog_kind(d) == V_CONSTRAINT && vlog_ident(d) == id) {
            proto = d;
            break;
         }
      }

      if (proto == NULL || !(vlog_flags(proto) & VLOG_F_EXTERN)) {
         diag_t *d = diag_new(DIAG_ERROR, &mloc);
         if (proto == NULL)
            diag_printf(d, "class %pQ has no constraint named %pQ", cname, id);
         else
            diag_printf(d, "constraint %pQ was not declared extern in class "
                        "%pQ", id, cname);
         if (proto != NULL)
            diag_hint(d, vlog_loc(proto), "declared here");
         diag_hint(d, &mloc, "definition");
         diag_emit(d);
         proto = NULL;
      }
      else if (is_static != !!(vlog_flags(proto) & VLOG_F_STATIC))
         error_at(&mloc, "the static qualifier of constraint %pQ does not "
                  "match the prototype", id);
   }

   if (class == NULL) {
      // The names cannot be resolved without the class so skip the block
      consume(tLBRACE);
      for (int nest = 1; nest > 0 && peek() != tEOF; ) {
         const token_t tok = peek();
         nest += tok == tLBRACE ? 1 : tok == tRBRACE ? -1 : 0;
         drop_token(&state);
      }
      return;
   }

   int depth = 0;
   push_class_scopes(class, &depth);

   consume(tLBRACE);

   while (not_at_token(tRBRACE)) {
      vlog_node_t item = p_constraint_expression();
      if (proto != NULL)
         vlog_add_stmt(proto, item);
   }

   consume(tRBRACE);

   if (proto != NULL)
      vlog_clear_flags(proto, VLOG_F_EXTERN);

   for (int i = 0; i < depth; i++)
      vlog_symtab_pop(symtab);
   vlog_symtab_pop(symtab);
}

static vlog_node_t p_class_type(void)
{
   // ps_class_identifier [ parameter_value_assignment ]
   //   { :: class_identifier [ parameter_value_assignment ] }

   BEGIN("class type");

   return p_data_type();
}

static void put_enum_names(vlog_node_t type_decl)
{
   // The names of an enumeration are visible with the type

   vlog_node_t t = vlog_type(type_decl);
   if (vlog_kind(t) != V_ENUM_DECL)
      return;

   const int ndecls = vlog_decls(t);
   for (int i = 0; i < ndecls; i++)
      vlog_symtab_put(symtab, vlog_decl(t, i));
}

static int push_base_scopes(vlog_node_t class)
{
   // Inherited members are visible in a derived class, outermost base first

   int depth = 0;

   if (vlog_flags(class) & VLOG_F_INTERFACE) {
      // An interface class can use the types of those it extends
      const unsigned nifaces = vlog_class_ifaces(class);
      for (unsigned i = 0; i < nifaces; i++) {
         vlog_node_t iface = vlog_class_iface(class, i);
         depth += push_base_scopes(iface);

         vlog_symtab_push(symtab, iface);
         depth++;

         const int ndecls = vlog_decls(iface);
         for (int j = 0; j < ndecls; j++) {
            vlog_node_t d = vlog_decl(iface, j);
            if ((vlog_kind(d) == V_TYPE_DECL
                 && vlog_ident(d) != ident_new("$implements"))
                || vlog_kind(d) == V_LOCALPARAM)
               vlog_symtab_put(symtab, d);
         }
      }
   }

   if (vlog_has_ref(class)) {
      vlog_node_t base = vlog_ref(class);
      depth += push_base_scopes(base);

      vlog_symtab_push(symtab, base);
      depth++;

      const int ndecls = vlog_decls(base);
      for (int i = 0; i < ndecls; i++) {
         vlog_node_t d = vlog_decl(base, i);
         switch (vlog_kind(d)) {
         case V_TYPE_DECL:
            if (vlog_ident(d) == ident_new("$implements"))
               break;
            put_enum_names(d);
            // Fall-through
         case V_VAR_DECL:
         case V_TASK_DECL:
         case V_FUNC_DECL:
         case V_CONSTRAINT:
         case V_PARAM_DECL:
         case V_LOCALPARAM:
            vlog_symtab_put(symtab, d);
            break;
         default:
            break;
         }
      }
   }

   return depth;
}

static bool class_has_decl(vlog_node_t class, ident_t id)
{
   const int ndecls = vlog_decls(class);
   for (int i = 0; i < ndecls; i++) {
      vlog_node_t d = vlog_decl(class, i);
      if (vlog_kind(d) == V_VAR_DECL && vlog_ident(d) == id)
         return true;
   }

   return false;
}

static void add_hidden_property(vlog_node_t class, ident_t id)
{
   vlog_node_t d = vlog_new(V_VAR_DECL);
   vlog_set_ident(d, id);
   vlog_set_loc(d, vlog_loc(class));

   vlog_node_t dt = make_integer_atom_type(DT_LONGINT);
   vlog_set_flags(dt, VLOG_F_SIGNED);
   vlog_set_type(d, dt);

   vlog_node_t zero = vlog_new(V_NUMBER);
   vlog_set_number(zero, number_from_int(0));
   vlog_set_loc(zero, vlog_loc(class));
   vlog_set_value(d, zero);

   vlog_add_decl(class, d);
}

static void add_rand_state(vlog_node_t class)
{
   // Hidden properties hold the rand_mode and constraint_mode bits and the
   // state of each randc variable

   bool own = false;
   int ncons = 0;
   for (vlog_node_t c = class; ; c = vlog_ref(c)) {
      const int ndecls = vlog_decls(c);
      for (int i = 0; i < ndecls; i++) {
         vlog_node_t d = vlog_decl(c, i);
         switch (vlog_kind(d)) {
         case V_VAR_DECL:
            if (vlog_flags(d) & (VLOG_F_RAND | VLOG_F_RANDC))
               own |= (c == class);
            break;
         case V_CONSTRAINT:
            // An override in a derived class uses the same bit
            if (vlog_class_member(class, vlog_ident(d)) == d)
               ncons++;
            own |= (c == class);
            break;
         default:
            break;
         }
      }

      if (!vlog_has_ref(c))
         break;
   }

   if (!own)
      return;

   // Only the first 64 rand properties have a rand_mode bit
   if (ncons > 64)
      error_at(vlog_loc(class), "sorry, more than 64 constraints in a class "
               "are not supported");

   ident_t rand_off = ident_new("$rand_off");

   bool inherited = false;
   for (vlog_node_t c = class; vlog_has_ref(c) && !inherited; ) {
      c = vlog_ref(c);
      inherited = class_has_decl(c, rand_off);
   }

   const int ndecls = vlog_decls(class);
   if (!inherited) {
      add_hidden_property(class, rand_off);
      add_hidden_property(class, ident_new("$cons_off"));
      add_hidden_property(class, ident_new("$seed"));
   }

   for (int i = 0; i < ndecls; i++) {
      vlog_node_t d = vlog_decl(class, i);
      if (vlog_kind(d) == V_VAR_DECL && (vlog_flags(d) & VLOG_F_RANDC))
         add_hidden_property(class, ident_prefix(vlog_ident(d),
                                                 ident_new("randc"), '$'));
   }
}

static void p_interface_class_type(vlog_node_t class)
{
   // interface_class_type

   BEGIN("interface class type");

   vlog_node_t ct = p_class_type();
   vlog_node_t t = vlog_get_type(ct);
   if (t == NULL || vlog_kind(t) != V_CLASS_DECL)
      error_at(&state.last_loc, "expecting an interface class type");
   else if (!(vlog_flags(t) & VLOG_F_INTERFACE))
      error_at(&state.last_loc, "class %pQ is not an interface class",
               vlog_ident(t));
   else {
      vlog_node_t d = vlog_new(V_TYPE_DECL);
      vlog_set_ident(d, ident_new("$implements"));
      vlog_set_type(d, t);
      vlog_set_loc(d, &state.last_loc);
      vlog_add_decl(class, d);
   }
}

static vlog_node_t generic_param_type(const generic_param_t *p)
{
   // The type of a value parameter is kept when it names a user type

   if (p->tname != NULL) {
      vlog_node_t d = vlog_symtab_query(symtab, p->tname);
      if (d != NULL && vlog_kind(d) == V_TYPE_DECL)
         return d;
   }

   return implicit_type();
}

static bool pfmt_class_member(vlog_node_t d)
{
   // Whether a property is shown by %p of an object

   if (vlog_kind(d) != V_VAR_DECL || vlog_ident(d) == NULL
       || strchr(istr(vlog_ident(d)), '$') != NULL)
      return false;

   vlog_node_t t = vlog_has_type(d) ? vlog_get_type(vlog_type(d)) : NULL;
   if (t == NULL)
      return false;

   switch (vlog_kind(t)) {
   case V_DATA_TYPE:
      switch (vlog_subkind(t)) {
      case DT_MAILBOX:
      case DT_SEMAPHORE:
      case DT_VIRTUAL:
      case DT_PROCESS:
         return false;
      default:
         return true;
      }
   case V_CLASS_DECL:
      return !(vlog_flags(t) & VLOG_F_INTERFACE);
   default:
      return true;
   }
}

static void add_pfmt_methods(vlog_node_t class)
{
   // Hidden methods give the text of %p of an object from its
   // properties.  A handle inside an array or an object is shown as
   // the class name and the object number, Name#N, rather than by its
   // properties so that a cycle of references cannot recurse

   if (vlog_flags(class) & VLOG_F_INTERFACE)
      return;

   // A specialisation is known by the name of its generic class
   char *cname LOCAL = xstrdup(istr(vlog_ident(class)));
   char *at = strchr(cname, '@');
   if (at != NULL)
      *at = '\0';

   if (strchr(cname, '"') != NULL || strchr(cname, '\\') != NULL)
      return;

   LOCAL_TEXT_BUF tb = tb_new();

   tb_printf(tb, "virtual function string __cname();\nreturn \"%s\";\n"
             "endfunction\n", cname);
   tb_cat(tb, "function int __oidn();\nreturn $__clsoid;\nendfunction\n");
   tb_printf(tb, "virtual function %s __cl();\n%s __r = new this;\n"
             "return __r;\nendfunction\n", cname, cname);
   tb_printf(tb, "static function string __pn(%s h);\n"
             "if (h == null) return \"null\";\n"
             "return $sformatf(\"%%s#%%0d\", h.__cname(), h.__oidn());\n"
             "endfunction\n", cname);

   for (int plain = 0; plain < 2; plain++) {
      tb_printf(tb, "virtual function string __p%d();\n", plain);
      tb_cat(tb, "string __r, __s;\n");
      if (vlog_has_ref(class))
         tb_printf(tb, "__r = super.__p%d();\n", plain);
      else
         tb_cat(tb, "__r = \"\";\n");

      for (int pass = 0; pass < 2; pass++) {
         const int ndecls = vlog_decls(class);
         for (int i = 0; i < ndecls; i++) {
            vlog_node_t d = vlog_decl(class, i);
            if (!pfmt_class_member(d))
               continue;
            else if (!!(vlog_flags(d) & VLOG_F_STATIC) != (pass == 1))
               continue;

            const char *name = istr(vlog_ident(d));
            tb_printf(tb, "if (__r != \"\") __r = {__r, \"%s\"};\n",
                      plain ? "," : ", ");

            vlog_node_t t = vlog_get_type(vlog_type(d));
            if (vlog_kind(t) == V_DATA_TYPE && vlog_subkind(t) == DT_EVENT)
               tb_printf(tb, "__r = {__r, \"%s:<no-events>\"};\n", name);
            else if (vlog_kind(t) == V_CLASS_DECL && vlog_ranges(d) == 0) {
               tb_printf(tb, "__s = $__pn(%s);\n", name);
               tb_printf(tb, "__r = {__r, \"%s:\", __s};\n", name);
            }
            else
               tb_printf(tb, "__r = {__r, \"%s:\", $sformatf(\"%s\", %s)};\n",
                         name, plain ? "%0p" : "%p", name);
         }
      }

      tb_cat(tb, "return __r;\nendfunction\n");

      tb_printf(tb, "static function string __ps%d(%s h);\n", plain, cname);
      tb_cat(tb, "if (h == null) return \"null\";\n");
      tb_printf(tb, "return {\"'{\", h.__p%d(), \"}\"};\n", plain);
      tb_cat(tb, "endfunction\n");
   }

   if (getenv("NVC_DBG_PFMT"))
      fprintf(stderr, "%s\n", tb_get(tb));

   const size_t len = tb_len(tb);
   char *copy = xmalloc(len + 1);
   memcpy(copy, tb_get(tb), len + 1);

   tokenq_t *toks = NULL;
   int ntoks = 0;

   scan_text_begin(copy, len);

   extern yylval_t yylval;
   extern loc_t yylloc;

   const loc_t *loc = vlog_loc(class);
   for (;;) {
      const token_t tok = yylex();
      if (tok == tEOF)
         break;

      yylloc = *loc;
      tokenq_t t = { tok, yylval, *loc };
      COV_PUSH(toks, ntoks, t);
   }

   scan_text_end();

   if (ntoks > 0) {
      tokenq_t eof = toks[ntoks - 1];
      eof.token = tEOF;
      COV_PUSH(toks, ntoks, eof);
      ntoks--;
   }

   cov_replay(class, toks, ntoks);
}

static void p_class_body(vlog_node_t v, ident_t name, generic_class_t *g,
                         vlog_node_t *actuals)
{
   // [ extends class_type [ ( list_of_arguments ) ] ]
   //   [ implements interface_class_type { , interface_class_type } ] ;
   //   { class_item } endclass [ : class_identifier ]

   BEGIN("class body");

   open_classes = xrealloc_array(open_classes, nopen_classes + 1,
                                 sizeof(vlog_node_t));
   open_classes[nopen_classes++] = v;

   // Methods are automatic unless declared otherwise
   const bool saved_proc = proc_automatic, saved_subr = subr_automatic;
   proc_automatic = subr_automatic = true;

   if (g != NULL) {
      // Bind the parameters and make the bare name of the class mean this
      // specialisation inside its own body
      vlog_symtab_push(symtab, v);

      for (int i = 0; i < g->nparams; i++) {
         vlog_node_t d = vlog_new(g->params[i].is_type ? V_TYPE_DECL
                                  : V_LOCALPARAM);
         vlog_set_ident(d, g->params[i].name);
         vlog_set_loc(d, &(g->tokens[0].loc));

         if (g->params[i].is_type)
            vlog_set_type(d, actuals[i]);
         else {
            if (vlog_kind(actuals[i]) == V_STRING) {
               vlog_node_t str = vlog_new(V_DATA_TYPE);
               vlog_set_subkind(str, DT_STRING);
               vlog_set_loc(str, vlog_loc(actuals[i]));
               vlog_set_type(d, str);
            }
            else
               vlog_set_type(d, generic_param_type(&(g->params[i])));
            vlog_set_value(d, actuals[i]);
         }

         vlog_symtab_put(symtab, d);
         vlog_add_decl(v, d);
      }

      vlog_node_t self = vlog_new(V_TYPE_DECL);
      vlog_set_ident(self, name);
      vlog_set_type(self, v);
      vlog_set_loc(self, &(g->tokens[0].loc));
      vlog_symtab_put(symtab, self);
      hash_put(generic_classes, self, g);

      g->specs = xrealloc_array(g->specs, g->nspecs + 1, sizeof(vlog_node_t));
      g->spec_scopes = xrealloc_array(g->spec_scopes, g->nspecs + 1,
                                      sizeof(vlog_scope_t *));
      g->specs[g->nspecs] = v;
      g->spec_scopes[g->nspecs++] = vlog_symtab_current(symtab);
   }

   const bool is_iface = !!(vlog_flags(v) & VLOG_F_INTERFACE);

   // Visible to its own base class and interface classes
   if (g == NULL) {
      vlog_set_loc(v, &state.last_loc);
      vlog_symtab_put(symtab, v);
   }

   if (optional(tEXTENDS)) {
      if (is_iface) {
         // An interface class can extend any number of interface classes
         do {
            p_interface_class_type(v);
         } while (optional(tCOMMA));
      }
      else {
         vlog_node_t base = p_class_type();
         vlog_node_t bt = vlog_get_type(base);
         if (bt != NULL && vlog_kind(bt) == V_CLASS_DECL
             && (vlog_flags(bt) & VLOG_F_INTERFACE))
            error_at(&state.last_loc, "class %pQ cannot extend the interface "
                     "class %pQ: use implements", name, vlog_ident(bt));
         else if (bt != NULL && vlog_kind(bt) == V_CLASS_DECL) {
            // The chain of base classes must not lead back to this class
            vlog_node_t it = bt;
            for (int n = 0; it != v && it != NULL && n < 1000; n++)
               it = vlog_has_ref(it) ? vlog_ref(it) : NULL;

            if (it == v)
               error_at(&state.last_loc, "class %pQ cannot be derived from "
                        "itself", name);
            else
               vlog_set_ref(v, bt);
         }
         else
            error_at(&state.last_loc, "base of class must be a class type");

         // Arguments of the call to the constructor of the base class if
         // the constructor does not call it explicitly
         if (optional(tLPAREN)) {
            p_list_of_arguments(v);
            consume(tRPAREN);
         }
      }
   }

   if (peek() == tIMPLEMENTS) {
      consume(tIMPLEMENTS);

      if (is_iface)
         error_at(&state.last_loc, "an interface class cannot implement "
                  "another: use extends");

      do {
         p_interface_class_type(v);
      } while (optional(tCOMMA));
   }

   consume(tSEMI);

   vlog_set_loc(v, CURRENT_LOC);

   const int depth = push_base_scopes(v);
   vlog_symtab_push(symtab, v);

   class_depth++;

   // A specialisation can be created while parsing the body of a subroutine
   const bool saved_in_subroutine = in_subroutine;
   in_subroutine = false;

   while (not_at_token(tENDCLASS))
      p_class_item(v);

   in_subroutine = saved_in_subroutine;

   class_depth--;

   add_pfmt_methods(v);
   add_rand_state(v);

   vlog_symtab_pop(symtab);

   for (int i = 0; i < depth; i++)
      vlog_symtab_pop(symtab);

   if (g != NULL)
      vlog_symtab_pop(symtab);

   consume(tENDCLASS);

   if (optional(tCOLON)) {
      ident_t end_name = p_identifier();
      if (name != end_name)
         error_at(&state.last_loc, "'%s' does not match class name '%s'",
                  istr(end_name), istr(name));
   }

   proc_automatic = saved_proc;
   subr_automatic = saved_subr;

   resolve_pending_lookups(v);
   nopen_classes--;
}

static vlog_node_t resolve_forward_class(ident_t name)
{
   // The class named by an earlier forward declaration is this one

   vlog_node_t d = vlog_symtab_query(symtab, name);
   if (d == NULL || vlog_kind(d) != V_CLASS_DECL
       || !(vlog_flags(d) & VLOG_F_FORWARD))
      return NULL;

   vlog_clear_flags(d, VLOG_F_FORWARD);

   for (int i = 0; i < forward_classes.count; i++) {
      if (forward_classes.items[i] == d) {
         for (int j = i; j < forward_classes.count - 1; j++)
            forward_classes.items[j] = forward_classes.items[j + 1];
         ATRIM(forward_classes, forward_classes.count - 1);
         break;
      }
   }

   return d;
}

static vlog_node_t p_generic_class(ident_t name, vlog_flags_t flags)
{
   // Parameters are bound by parsing the body again for each specialisation
   // so that member lookups can see the actual types

   EXTEND("parameterised class");

   generic_class_t *g = xcalloc(sizeof(generic_class_t));
   g->name  = name;
   g->flags = flags;
   g->scope = vlog_symtab_current(symtab);

   vlog_node_t du = vlog_symtab_design_unit(symtab);
   if (du != NULL && vlog_kind(du) == V_MODULE && class_depth == 0)
      g->module = du;

   consume(tHASH);
   consume(tLPAREN);

   bool is_type = false;
   while (not_at_token(tRPAREN)) {
      if (scan(tPARAMETER, tLOCALPARAM))
         (void)one_of(tPARAMETER, tLOCALPARAM);

      ident_t tname = NULL;
      if (optional(tTYPE))
         is_type = true;
      else if (peek() != tID || (peek_nth(2) != tEQ && peek_nth(2) != tCOMMA
                                 && peek_nth(2) != tRPAREN)) {
         if (peek() == tID && peek_nth(2) == tID)
            tname = peek_ident_nth(1);
         (void)p_data_type_or_implicit();
         is_type = false;
      }

      g->params = xrealloc_array(g->params, g->nparams + 1,
                                 sizeof(generic_param_t));
      generic_param_t *p = &(g->params[g->nparams++]);
      p->name = p_identifier();
      p->is_type = is_type;
      p->tname = tname;
      p->has_def = false;
      p->ndef = 0;
      p->def = NULL;

      if (optional(tEQ)) {
         // May refer to the parameters before it so this is parsed for
         // each specialisation
         p->has_def = true;

         for (int depth = 0;;) {
            const token_t tok = peek();
            if (tok == tEOF)
               break;
            else if (depth == 0 && (tok == tCOMMA || tok == tRPAREN))
               break;
            else if (tok == tLPAREN || tok == tLSQUARE || tok == tLBRACE)
               depth++;
            else if (tok == tRPAREN || tok == tRSQUARE || tok == tRBRACE)
               depth--;

            tokenq_t saved = state.tokenq[state.tokenq_tail];
            drop_token(&state);

            p->def = xrealloc_array(p->def, p->ndef + 1, sizeof(tokenq_t));
            p->def[p->ndef++] = saved;
         }

         if (p->ndef == 0) {
            error_at(&state.last_loc, "missing default value for parameter "
                     "%pi", p->name);
            p->has_def = false;
         }
      }

      if (!optional(tCOMMA))
         break;
   }

   consume(tRPAREN);

   // Record everything up to the end of the class

   int depth = 0;
   for (;;) {
      const token_t tok = peek();
      if (tok == tEOF) {
         expect(tENDCLASS);
         break;
      }

      if (tok == tCLASS)
         depth++;

      const int pos = state.tokenq_tail;
      tokenq_t saved = state.tokenq[pos];
      drop_token(&state);

      g->tokens = xrealloc_array(g->tokens, g->ntokens + 1, sizeof(tokenq_t));
      g->tokens[g->ntokens++] = saved;

      if (tok == tENDCLASS && depth-- == 0) {
         // Keep the optional label with the class
         if (peek() == tCOLON) {
            for (int i = 0; i < 2; i++) {
               saved = state.tokenq[state.tokenq_tail];
               drop_token(&state);
               g->tokens = xrealloc_array(g->tokens, g->ntokens + 1,
                                          sizeof(tokenq_t));
               g->tokens[g->ntokens++] = saved;
               if (i == 0)
                  (void)peek();
            }
         }
         break;
      }
   }

   vlog_node_t v = resolve_forward_class(name);
   if (v == NULL)
      v = vlog_new(V_CLASS_DECL);

   vlog_set_ident(v, name);
   vlog_set_flags(v, flags | VLOG_F_GENERIC);
   vlog_set_loc(v, CURRENT_LOC);

   if (generic_classes == NULL)
      generic_classes = hash_new(16);
   hash_put(generic_classes, v, g);

   APUSH(unit_generics, v);

   vlog_symtab_put(symtab, v);

   if (flags & VLOG_F_INTERFACE) {
      // Specialise with the default values so that errors in the
      // declaration are found even if it is never used
      bool all_default = true;
      for (int i = 0; i < g->nparams; i++)
         all_default &= g->params[i].has_def;

      if (all_default)
         (void)specialise_class(v);
   }

   resolve_forward_specs(v);

   return v;
}

static token_t replay_lex(void)
{
   // Tokens are consumed by the parser so give it its own copy of any
   // text they own

   extern yylval_t yylval;
   extern loc_t yylloc;

   if (replay_pos == replay_class->ntokens) {
      yylloc = replay_class->tokens[replay_pos - 1].loc;
      return tEOF;
   }

   const tokenq_t *t = &(replay_class->tokens[replay_pos++]);
   yylval = t->lval;
   yylloc = t->loc;

   switch (t->token) {
   case tUNSNUM:
   case tNUMBER:
   case tTIMELIT:
   case tBITSTRING:
      yylval.str = xstrdup(t->lval.str);
      break;
   case tSTRING:
      yylval.text = tb_new();
      tb_catn(yylval.text, tb_get(t->lval.text), tb_len(t->lval.text));
      break;
   default:
      break;
   }

   return t->token;
}

typedef struct {
   ident_t   name;
   ident_t  *formals;
   tokenq_t **defaults;
   int       *ndefaults;
   int        nformals;
   tokenq_t  *body;
   int        nbody;
} checker_def_t;

static A(checker_def_t) checker_defs = AINIT;

static void register_checker(const tokenq_t *t, int n)
{
   // Keep the text of a checker so that an instance in procedural code
   // can be expanded in place (17.3)

   if (n < 4 || t[0].token != tID || t[1].token != tID)
      return;

   checker_def_t def = { .name = t[1].lval.ident };
   int pos = 2;

   if (t[pos].token == tLPAREN) {
      int depth = 0, start = pos + 1;
      for (; pos < n; pos++) {
         if (t[pos].token == tLPAREN || t[pos].token == tLSQUARE
             || t[pos].token == tLBRACE)
            depth++;
         else if (t[pos].token == tRPAREN || t[pos].token == tRSQUARE
                  || t[pos].token == tRBRACE)
            depth--;

         const bool last = depth == 0;
         if ((depth == 1 && t[pos].token == tCOMMA) || last) {
            int end = pos, eq = -1, name = -1;
            for (int i = start, d = 0; i < end; i++) {
               if (t[i].token == tLPAREN || t[i].token == tLSQUARE
                   || t[i].token == tLBRACE)
                  d++;
               else if (t[i].token == tRPAREN || t[i].token == tRSQUARE
                        || t[i].token == tRBRACE)
                  d--;
               else if (d == 0 && t[i].token == tEQ && eq < 0)
                  eq = i;
               else if (d == 0 && t[i].token == tID && eq < 0)
                  name = i;
            }

            if (name >= 0) {
               const int k = def.nformals++;
               def.formals = xrealloc_array(def.formals, def.nformals,
                                            sizeof(ident_t));
               def.defaults = xrealloc_array(def.defaults, def.nformals,
                                             sizeof(tokenq_t *));
               def.ndefaults = xrealloc_array(def.ndefaults, def.nformals,
                                              sizeof(int));
               def.formals[k] = t[name].lval.ident;
               def.defaults[k] = NULL;
               def.ndefaults[k] = 0;
               if (eq >= 0) {
                  for (int i = eq + 1; i < end; i++)
                     COV_PUSH(def.defaults[k], def.ndefaults[k], t[i]);
               }
            }

            start = pos + 1;
         }

         if (last)
            break;
      }

      pos++;
   }

   if (pos >= n || t[pos].token != tSEMI)
      return;

   pos++;

   // The body ends at endchecker
   int end = n;
   while (end > pos && !(t[end - 1].token == tID
                         && t[end - 1].lval.ident == ident_new("endchecker")))
      end--;

   if (end > pos)
      end--;

   for (int i = pos; i < end; i++)
      COV_PUSH(def.body, def.nbody, t[i]);

   APUSH(checker_defs, def);
}

static checker_def_t *find_checker(ident_t name)
{
   for (int i = 0; i < checker_defs.count; i++) {
      if (checker_defs.items[i].name == name)
         return &(checker_defs.items[i]);
   }

   return NULL;
}

static bool at_checker_call(void)
{
   return checker_defs.count > 0 && peek() == tID && peek_nth(2) == tID
      && peek_nth(3) == tLPAREN && find_checker(peek_ident_nth(1)) != NULL;
}

static bool is_inferred_clock(const tokenq_t *t, int n)
{
   return n == 1 && t[0].token == tSYSTASK
      && t[0].lval.ident == ident_new("$inferred_clock");
}

static vlog_node_t p_checker_call(void)
{
   // checker_identifier name ( [ list_of_checker_port_connections ] ) ;
   // in a procedure: the assertions of the checker are those of the
   // enclosing procedure with the formals replaced by the actuals (17.3)

   BEGIN("checker instantiation");

   checker_def_t *def = find_checker(peek_ident_nth(1));
   const tokenq_t cname = cov_take();
   const tokenq_t iname = cov_take();
   (void)cname;

   tokenq_t *actual[def->nformals + 1];
   int nactual[def->nformals + 1];
   for (int i = 0; i < def->nformals; i++) {
      actual[i] = NULL;
      nactual[i] = 0;
   }

   (void)cov_take();   // (

   int index = 0, depth = 1, named = -1;
   bool first = true;
   for (;;) {
      if (peek() == tEOF)
         break;

      const token_t tok = peek();
      if (tok == tLPAREN || tok == tLSQUARE || tok == tLBRACE)
         depth++;
      else if (tok == tRPAREN || tok == tRSQUARE || tok == tRBRACE) {
         if (--depth == 0) {
            consume(tRPAREN);
            break;
         }
      }

      if (depth == 1 && tok == tCOMMA) {
         (void)cov_take();
         index++;
         named = -1;
         first = true;
         continue;
      }

      if (first && depth == 1 && tok == tDOT && peek_nth(2) == tID) {
         (void)cov_take();
         ident_t n = cov_take().lval.ident;
         (void)cov_take();   // (
         depth++;
         named = -1;
         for (int i = 0; i < def->nformals; i++) {
            if (def->formals[i] == n)
               named = index = i;
         }
         first = false;
         continue;
      }

      first = false;

      tokenq_t t = cov_take();
      if (depth == 1 && named >= 0 && tok == tRPAREN)
         continue;

      if (index < def->nformals)
         COV_PUSH(actual[index], nactual[index], t);
   }

   consume(tSEMI);

   tokenq_t *out = NULL;
   int nout = 0;

   tokenq_t tmpl = iname;
   memset(&tmpl.lval, 0, sizeof(tmpl.lval));

   tmpl.token = tBEGIN;
   COV_PUSH(out, nout, tmpl);
   tmpl.token = tCOLON;
   COV_PUSH(out, nout, tmpl);
   COV_PUSH(out, nout, iname);

   for (int i = 0; i < def->nbody; i++) {
      const tokenq_t *t = &(def->body[i]);
      int f = -1;
      if (t->token == tID && (i == 0 || def->body[i - 1].token != tDOT)) {
         for (int j = 0; j < def->nformals; j++) {
            if (def->formals[j] == t->lval.ident)
               f = j;
         }
      }

      if (f < 0) {
         COV_PUSH(out, nout, *t);
         continue;
      }

      const tokenq_t *toks = actual[f];
      int ntoks = nactual[f];
      if (ntoks == 0) {
         toks = def->defaults[f];
         ntoks = def->ndefaults[f];
      }

      if (is_inferred_clock(toks, ntoks) && nout > 0
          && out[nout - 1].token == tAT) {
         nout--;   // The clock of the enclosing procedure
         continue;
      }

      tmpl.token = tLPAREN;
      COV_PUSH(out, nout, tmpl);
      for (int k = 0; k < ntoks; k++)
         COV_PUSH(out, nout, toks[k]);
      tmpl.token = tRPAREN;
      COV_PUSH(out, nout, tmpl);
   }

   tmpl.token = tEND;
   COV_PUSH(out, nout, tmpl);

   tokenq_t eof = tmpl;
   eof.token = tEOF;
   COV_PUSH(out, nout, eof);
   nout--;

   generic_class_t tokens = {
      .ntokens = nout,
      .tokens  = out,
   };

   parse_state_t saved = state;
   generic_class_t *saved_class = replay_class;
   const int saved_pos = replay_pos;

   replay_class = &tokens;
   replay_pos = 0;

   state.lex_fn = replay_lex;
   state.tokenq_head = state.tokenq_tail = 0;
   state.start_loc = LOC_INVALID;

   vlog_node_t v = p_statement_or_null();

   state = saved;
   replay_class = saved_class;
   replay_pos = saved_pos;

   free(out);
   return v;
}

static bool is_param_decl(vlog_node_t d);

typedef struct {
   vlog_node_t decl;
   int64_t     value;
   bool        known;
} spec_env_t;

static bool spec_eval(vlog_node_t v, const spec_env_t *env, int nenv,
                      int64_t *result)
{
   switch (vlog_kind(v)) {
   case V_REF:
      if (vlog_has_ref(v)) {
         for (int i = 0; i < nenv; i++) {
            if (env[i].decl == vlog_ref(v)) {
               *result = env[i].value;
               return env[i].known;
            }
         }

         // A parameter with an expression which has not been folded yet
         vlog_node_t d = vlog_ref(v);
         if ((vlog_kind(d) == V_PARAM_DECL || vlog_kind(d) == V_LOCALPARAM)
             && vlog_has_value(d) && vlog_kind(vlog_value(d)) == V_BINARY)
            return spec_eval(vlog_value(d), env, nenv, result);
      }
      break;
   case V_BINARY:
      {
         int64_t l, r;
         if (!spec_eval(vlog_left(v), env, nenv, &l)
             || !spec_eval(vlog_right(v), env, nenv, &r))
            return false;

         switch (vlog_subkind(v)) {
         case V_BINARY_PLUS:  *result = l + r; return true;
         case V_BINARY_MINUS: *result = l - r; return true;
         case V_BINARY_TIMES: *result = l * r; return true;
         case V_BINARY_DIVIDE:
            if (r == 0)
               return false;
            *result = l / r;
            return true;
         default:
            return false;
         }
      }
   case V_SYS_FCALL:
      if (vlog_ident(v) == ident_new("$bits") && vlog_params(v) == 1) {
         // The width of a type or of an object with a fixed size
         vlog_node_t arg = vlog_param(v, 0);
         vlog_node_t t = NULL;
         switch (vlog_kind(arg)) {
         case V_DATA_TYPE:
            t = arg;
            break;
         case V_REF:
            if (vlog_has_ref(arg)) {
               vlog_node_t d = vlog_ref(arg);
               switch (vlog_kind(d)) {
               case V_TYPE_DECL:
               case V_ENUM_DECL:
               case V_STRUCT_DECL:
               case V_UNION_DECL:
                  t = d;
                  break;
               case V_VAR_DECL:
               case V_NET_DECL:
               case V_PORT_DECL:
                  if (!vlog_is_dynamic_type(vlog_type(d))) {
                     *result = vlog_size(d) * vlog_size(vlog_type(d));
                     return *result > 0;
                  }
                  break;
               default:
                  break;
               }
            }
            break;
         default:
            break;
         }

         if (t != NULL) {
            while ((vlog_kind(t) == V_TYPE_DECL || vlog_kind(t) == V_ENUM_DECL)
                   && vlog_has_type(t))
               t = vlog_type(t);

            if (vlog_kind(t) == V_DATA_TYPE && !vlog_is_dynamic_type(t)) {
               *result = vlog_size(t);
               return *result > 0;
            }
         }
      }
      break;
   default:
      break;
   }

   return vlog_get_const(v, result);
}

static void spec_key_for(text_buf_t *tb, vlog_node_t type);

static void spec_key_params(text_buf_t *tb, vlog_node_t t, bool actuals)
{
   vlog_node_t iface = vlog_ref(t);

   spec_env_t env[16];
   int nenv = 0, index = 0;

   const int ndecls = vlog_decls(iface);
   const int nparams = actuals ? vlog_params(t) : 0;
   for (int i = 0; i < ndecls && nenv < 16; i++) {
      vlog_node_t d = vlog_decl(iface, i);
      const bool is_type = vlog_kind(d) == V_TYPE_DECL;
      if (!is_param_decl(d))
         continue;

      vlog_node_t value = NULL;
      if (index < nparams && !vlog_has_ident(vlog_param(t, index))
          && vlog_has_value(vlog_param(t, index)))
         value = vlog_value(vlog_param(t, index));
      else {
         for (int j = 0; j < nparams; j++) {
            vlog_node_t p = vlog_param(t, j);
            if (vlog_has_ident(p) && vlog_ident(p) == vlog_ident(d)
                && vlog_has_value(p))
               value = vlog_value(p);
         }
      }
      index++;

      tb_append(tb, '$');
      tb_istr(tb, vlog_ident(d));
      tb_append(tb, '=');

      env[nenv].decl = d;
      env[nenv].known = false;
      env[nenv].value = 0;

      if (is_type) {
         vlog_node_t dt = value != NULL ? value : vlog_type(d);
         while (vlog_kind(dt) == V_TYPE_DECL && vlog_has_type(dt)
                && !(vlog_flags(dt) & VLOG_F_TYPEPARAM))
            dt = vlog_type(dt);

         int64_t size = 0;
         if (vlog_kind(dt) == V_DATA_TYPE && vlog_subkind(dt) != DT_VIRTUAL
             && vlog_ranges(dt) == 1) {
            vlog_node_t dim = vlog_range(dt, 0);
            int64_t l, r;
            if (spec_eval(vlog_left(dim), env, nenv, &l)
                && spec_eval(vlog_right(dim), env, nenv, &r)) {
               size = (l > r ? l - r : r - l) + 1;
               tb_printf(tb, "t%d_%"PRIi64"_%d", vlog_subkind(dt), size,
                         !!(vlog_flags(dt) & VLOG_F_SIGNED));
               nenv++;
               continue;
            }
         }

         spec_key_for(tb, dt);
      }
      else {
         vlog_node_t expr = value != NULL ? value
            : (vlog_has_value(d) ? vlog_value(d) : NULL);
         int64_t ival;
         if (expr != NULL && spec_eval(expr, env, nenv, &ival)) {
            tb_printf(tb, "%"PRIi64, ival);
            env[nenv].value = ival;
            env[nenv].known = true;
         }
         else if (expr != NULL) {
            const loc_t *loc = vlog_loc(expr);
            tb_printf(tb, "u%u_%u", loc->first_line, loc->first_column);
         }
      }

      nenv++;
   }
}

static void spec_key_effective(text_buf_t *tb, vlog_node_t t)
{
   // The parameters of a virtual interface type with the defaults of the
   // interface filled in so different spellings of the same type agree.
   // A type which uses only the defaults has the same key as one that was
   // used before the interface was declared and so has no parameters.

   LOCAL_TEXT_BUF given = tb_new();
   LOCAL_TEXT_BUF dflt = tb_new();
   spec_key_params(given, t, true);
   spec_key_params(dflt, t, false);

   if (strcmp(tb_get(given), tb_get(dflt)) != 0)
      tb_cat(tb, tb_get(given));
}

static void spec_key_for(text_buf_t *tb, vlog_node_t type)
{
   vlog_node_t t = vlog_get_type(type);
   if (t == NULL) {
      tb_cat(tb, "?");
      return;
   }

   switch (vlog_kind(t)) {
   case V_CLASS_DECL:
      tb_istr(tb, vlog_ident(t));
      break;
   case V_DATA_TYPE:
      switch (vlog_subkind(t)) {
      case DT_DYNARRAY:
      case DT_QUEUE:
         tb_cat(tb, vlog_subkind(t) == DT_QUEUE ? "q$" : "d$");
         spec_key_for(tb, vlog_type(t));
         break;
      case DT_VIRTUAL:
         tb_cat(tb, "v$");
         tb_istr(tb, vlog_ident(t));
         if (vlog_has_ref(t) && vlog_kind(vlog_ref(t)) == V_MODULE)
            spec_key_effective(tb, t);
         break;
      case DT_STRING:
      case DT_EVENT:
      case DT_CHANDLE:
      case DT_PROCESS:
      case DT_SEMAPHORE:
      case DT_REAL:
      case DT_SHORTREAL:
      case DT_REALTIME:
         tb_printf(tb, "t%d", vlog_subkind(t));
         break;
      case DT_MAILBOX:
         tb_cat(tb, "m$");
         spec_key_for(tb, vlog_type(t));
         break;
      case DT_ASSOC:
         tb_cat(tb, "a$");
         spec_key_for(tb, vlog_type(t));
         if (vlog_assoc_key(t) != NULL) {
            tb_append(tb, '$');
            spec_key_for(tb, vlog_assoc_key(t));
         }
         break;
      default:
         tb_printf(tb, "t%d_%u_%d", vlog_subkind(t), vlog_size(t),
                   !!(vlog_flags(t) & VLOG_F_SIGNED));
         break;
      }
      break;
   default:
      {
         const loc_t *loc = vlog_loc(t);
         tb_printf(tb, "u%u_%u", loc->first_line, loc->first_column);
      }
      break;
   }
}

static void set_decl_scopes(vlog_node_t container, ident_t prefix);
static void add_scope_init(vlog_node_t mod, ident_t name);
static generic_class_t *load_generic_class(vlog_node_t generic);
static void capture_tokens_until(generic_class_t *c, token_t end_token);
static void replay_generic_extern(generic_class_t *ext, vlog_node_t spec,
                                  vlog_scope_t *scope);

static vlog_node_t replay_default(vlog_node_t generic, generic_class_t *g,
                                  int pos, vlog_node_t *actuals,
                                  vlog_node_t container)
{
   // Parse the default value with the parameters before it visible

   generic_class_t tokens = {
      .ntokens = g->params[pos].ndef,
      .tokens  = g->params[pos].def,
   };

   parse_state_t saved = state;
   generic_class_t *saved_class = replay_class;
   const int saved_pos = replay_pos;
   vlog_node_t saved_container = spec_container;

   replay_class = &tokens;
   replay_pos = 0;
   spec_container = container;

   state.lex_fn = replay_lex;
   state.tokenq_head = state.tokenq_tail = 0;
   state.start_loc = LOC_INVALID;

   vlog_scope_t *saved_scope = vlog_symtab_enter(symtab, g->scope);
   vlog_symtab_push(symtab, generic);

   for (int i = 0; i < pos; i++) {
      vlog_node_t d = vlog_new(g->params[i].is_type ? V_TYPE_DECL
                               : V_LOCALPARAM);
      vlog_set_ident(d, g->params[i].name);
      vlog_set_loc(d, &(tokens.tokens[0].loc));

      if (g->params[i].is_type)
         vlog_set_type(d, actuals[i]);
      else {
         vlog_set_type(d, generic_param_type(&(g->params[i])));
         vlog_set_value(d, actuals[i]);
      }

      vlog_symtab_put(symtab, d);
   }

   vlog_node_t result =
      g->params[pos].is_type ? p_data_type() : p_expression();

   vlog_symtab_pop(symtab);
   vlog_symtab_enter(symtab, saved_scope);

   state = saved;
   replay_class = saved_class;
   replay_pos = saved_pos;
   spec_container = saved_container;

   return result;
}

static vlog_node_t specialise_class(vlog_node_t generic)
{
   // [ # ( list_of_parameter_assignments ) ]

   EXTEND("class specialisation");

   generic_class_t *g = NULL;
   if (generic_classes != NULL)
      g = hash_get(generic_classes, generic);
   if (g == NULL)
      g = load_generic_class(generic);

   vlog_node_t *actuals LOCAL =
      xcalloc_array(MAX(g->nparams, 1), sizeof(vlog_node_t));

   if (optional(tHASH)) {
      consume(tLPAREN);

      for (int i = 0; not_at_token(tRPAREN); i++) {
         int pos = i;
         bool named = false;
         if (optional(tDOT)) {
            ident_t id = p_identifier();
            consume(tLPAREN);
            named = true;

            for (pos = 0; pos < g->nparams; pos++) {
               if (g->params[pos].name == id)
                  break;
            }

            if (pos == g->nparams) {
               error_at(&state.last_loc, "class %pQ has no parameter named %pQ",
                        g->name, id);
               return logic_type();
            }
         }
         else if (pos >= g->nparams) {
            error_at(&state.last_loc, "too many parameters for class %pQ",
                     g->name);
            return logic_type();
         }

         actuals[pos] = g->params[pos].is_type ? p_data_type() : p_expression();

         if (named)
            consume(tRPAREN);

         if (!optional(tCOMMA))
            break;
      }

      consume(tRPAREN);
   }

   // A class goes in the module or package using it, or else in the
   // current compilation unit scope, as its methods need a parent context

   vlog_node_t container = spec_container;
   if (container == NULL)
      container = vlog_symtab_design_unit(symtab);
   if (container == NULL)
      container = current_namespace;

   if (container == NULL) {
      error_at(&state.last_loc, "class %pQ cannot be used here", g->name);
      return logic_type();
   }

   // The line number keeps classes of the same name in other packages apart
   LOCAL_TEXT_BUF key = tb_new();
   tb_istr(key, g->name);
   tb_printf(key, "@%u", g->ntokens > 0 ? g->tokens[0].loc.first_line : 0);

   for (int i = 0; i < g->nparams; i++) {
      if (actuals[i] == NULL && g->params[i].has_def)
         actuals[i] = replay_default(generic, g, i, actuals, container);

      if (actuals[i] == NULL) {
         error_at(&state.last_loc, "parameter %pQ of class %pQ has no default "
                  "value", g->params[i].name, g->name);
         return logic_type();
      }

      tb_append(key, '$');

      // Parameter of an enclosing specialisation
      while (!g->params[i].is_type && vlog_kind(actuals[i]) == V_REF
             && vlog_has_ref(actuals[i])
             && vlog_kind(vlog_ref(actuals[i])) == V_LOCALPARAM
             && vlog_has_value(vlog_ref(actuals[i])))
         actuals[i] = vlog_value(vlog_ref(actuals[i]));

      int64_t ival;
      if (g->params[i].is_type)
         spec_key_for(key, actuals[i]);
      else if (vlog_kind(actuals[i]) == V_STRING)
         tb_printf(key, "s%x", number_hash(vlog_number(actuals[i])));
      else if (spec_eval(actuals[i], NULL, 0, &ival))
         tb_printf(key, "%"PRIi64, ival);
      else {
         const loc_t *loc = vlog_loc(actuals[i]);
         tb_printf(key, "e%u_%u", loc->first_line, loc->first_column);
      }
   }

   ident_t spec_name = ident_new(tb_get(key));

   vlog_node_t into = spec_into;
   spec_into = NULL;

   if (into == NULL) {
      const int ndecls = vlog_decls(container);
      for (int i = 0; i < ndecls; i++) {
         vlog_node_t d = vlog_decl(container, i);
         if (vlog_kind(d) == V_CLASS_DECL && vlog_ident(d) == spec_name)
            return d;
      }

      // The package with a class from the library may already have this
      vlog_node_t home = g->home;
      if (home != NULL && home != container) {
         const int nhome = vlog_decls(home);
         for (int i = 0; i < nhome; i++) {
            vlog_node_t d = vlog_decl(home, i);
            if (vlog_kind(d) == V_CLASS_DECL && vlog_ident(d) == spec_name)
               return d;
         }
      }
   }

   if (spec_depth >= MAX_SPEC_DEPTH) {
      error_at(&state.last_loc, "specialisations of class %pQ are nested too "
               "deeply: the parameters may be circular", g->name);
      return logic_type();
   }

   vlog_node_t v = into ?: vlog_new(V_CLASS_DECL);
   vlog_set_ident(v, spec_name);
   vlog_clear_flags(v, VLOG_F_FORWARD);
   vlog_set_flags(v, g->flags | VLOG_F_SPEC);
   APUSH(unit_specs, v);

   if (g->module != NULL) {
      if (spec_modules == NULL)
         spec_modules = hash_new(16);
      hash_put(spec_modules, v, g->module);
   }

   // Added first so that a class can refer to itself
   vlog_add_decl(container, v);

   parse_state_t saved = state;
   generic_class_t *saved_class = replay_class;
   const int saved_pos = replay_pos;
   const bool saved_proto = parsing_prototype;
   const vlog_kind_t saved_kind = param_kind;

   vlog_node_t saved_container = spec_container;
   spec_container = container;

   vlog_scope_t *saved_scope = vlog_symtab_enter(symtab, g->scope);

   replay_class = g;
   replay_pos = 0;
   parsing_prototype = false;

   state.lex_fn = replay_lex;
   state.tokenq_head = state.tokenq_tail = 0;
   state.start_loc = LOC_INVALID;

   spec_depth++;
   p_class_body(v, g->name, g, actuals);
   spec_depth--;

   state = saved;
   replay_class = saved_class;
   replay_pos = saved_pos;
   parsing_prototype = saved_proto;
   param_kind = saved_kind;
   spec_container = saved_container;

   vlog_symtab_enter(symtab, saved_scope);

   // Methods defined outside of the class that have been seen so far
   vlog_node_t saved_extern_container = spec_container;
   spec_container = container;

   for (int i = 0; i < g->nexterns; i++)
      replay_generic_extern(g->externs[i], v, g->spec_scopes[g->nspecs - 1]);

   spec_container = saved_extern_container;

   if (class_depth == 0)
      flush_deferred_bodies(false);

   return v;
}

static void replay_generic_extern(generic_class_t *ext, vlog_node_t spec,
                                  vlog_scope_t *scope)
{
   // Parse a method defined outside of the class for one specialisation

   parse_state_t saved = state;
   generic_class_t *saved_class = replay_class;
   const int saved_pos = replay_pos;
   vlog_node_t saved_extern = spec_extern_class;
   vlog_node_t saved_container = spec_container;

   replay_class = ext;
   replay_pos = 0;
   spec_extern_class = spec;

   state.lex_fn = replay_lex;
   state.tokenq_head = state.tokenq_tail = 0;
   state.start_loc = LOC_INVALID;

   vlog_scope_t *saved_scope = vlog_symtab_enter(symtab, scope);

   const int saved_depth = class_depth;
   class_depth++;

   if (peek() == tTASK)
      (void)p_task_declaration();
   else if (peek() == tCONSTRAINT || peek() == tSTATIC)
      p_extern_constraint();
   else
      (void)p_function_declaration();

   class_depth = saved_depth;

   vlog_symtab_enter(symtab, saved_scope);

   state = saved;
   replay_class = saved_class;
   replay_pos = saved_pos;
   spec_extern_class = saved_extern;
   spec_container = saved_container;
}

static bool capture_generic_constraint(void)
{
   // A constraint defined outside of a parameterised class is kept for each
   // specialisation like a method

   if (spec_extern_class != NULL || class_depth > 0)
      return false;

   const int n = peek() == tSTATIC ? 3 : 2;
   if (peek_nth(n) != tID || peek_nth(n + 1) != tSCOPE)
      return false;

   const int pos = (state.tokenq_tail + n - 1) & (TOKENQ_SIZE - 1);
   vlog_node_t d = vlog_symtab_query(symtab, state.tokenq[pos].lval.ident);
   generic_class_t *g = NULL;
   if (d != NULL && generic_classes != NULL)
      g = hash_get(generic_classes, d);

   if (g == NULL)
      return false;

   generic_class_t *ext = xcalloc(sizeof(generic_class_t));

   int depth = 0;
   for (bool done = false; !done; ) {
      const token_t tok = peek();
      if (tok == tEOF)
         break;

      tokenq_t saved = state.tokenq[state.tokenq_tail];
      drop_token(&state);

      ext->tokens = xrealloc_array(ext->tokens, ext->ntokens + 1,
                                   sizeof(tokenq_t));
      ext->tokens[ext->ntokens++] = saved;

      if (tok == tLBRACE)
         depth++;
      else if (tok == tRBRACE && --depth == 0)
         done = true;
   }

   g->externs = xrealloc_array(g->externs, g->nexterns + 1,
                               sizeof(generic_class_t *));
   g->externs[g->nexterns++] = ext;

   for (int i = 0; i < g->nspecs; i++)
      replay_generic_extern(ext, g->specs[i], g->spec_scopes[i]);

   return true;
}

static bool capture_generic_extern(token_t end_token)
{
   // With the next token being function or task check if this defines a
   // method of a parameterised class and if so keep it for each specialisation

   if (spec_extern_class != NULL || class_depth > 0)
      return false;

   int n = 2;
   generic_class_t *g = NULL;
   for (; n < TOKENQ_SIZE - 8; n++) {
      const token_t tok = peek_nth(n);
      if (tok == tEOF || tok == tLPAREN || tok == tSEMI)
         return false;
      else if (tok == tID && peek_nth(n + 1) == tSCOPE
               && peek_nth(n + 3) != tID && peek_nth(n + 3) != tSCOPE) {
         const int pos = (state.tokenq_tail + n - 1) & (TOKENQ_SIZE - 1);
         vlog_node_t d = vlog_symtab_query(symtab,
                                           state.tokenq[pos].lval.ident);
         if (d != NULL && generic_classes != NULL)
            g = hash_get(generic_classes, d);

         if (g != NULL)
            break;

         return false;
      }
   }

   if (g == NULL)
      return false;

   generic_class_t *ext = xcalloc(sizeof(generic_class_t));
   capture_tokens_until(ext, end_token);

   g->externs = xrealloc_array(g->externs, g->nexterns + 1,
                               sizeof(generic_class_t *));
   g->externs[g->nexterns++] = ext;

   for (int i = 0; i < g->nspecs; i++)
      replay_generic_extern(ext, g->specs[i], g->spec_scopes[i]);

   return true;
}

static void capture_tokens_until(generic_class_t *c, token_t end_token)
{
   // Keep the tokens up to the end token and its optional label

   for (;;) {
      const token_t tok = peek();
      if (tok == tEOF) {
         expect(end_token);
         return;
      }

      tokenq_t saved = state.tokenq[state.tokenq_tail];
      drop_token(&state);

      c->tokens = xrealloc_array(c->tokens, c->ntokens + 1, sizeof(tokenq_t));
      c->tokens[c->ntokens++] = saved;

      if (tok == end_token)
         break;
   }

   if (peek() == tCOLON) {
      for (int i = 0; i < 2; i++) {
         tokenq_t saved = state.tokenq[state.tokenq_tail];
         drop_token(&state);

         c->tokens = xrealloc_array(c->tokens, c->ntokens + 1,
                                    sizeof(tokenq_t));
         c->tokens[c->ntokens++] = saved;

         if (i == 0)
            (void)peek();
      }
   }
}

static bool defer_property_init(vlog_node_t decl)
{
   // Only an initial value which calls something depends on what is declared
   // later in the class and others may be needed as constants

   if (peek() == tNEW || (peek() == tTICK && class_depth > 0))
      return false;

   bool has_call = false;
   int n = 1, depth = 0;
   for (;; n++) {
      if (n >= TOKENQ_SIZE - 8)
         return false;

      const token_t tok = peek_nth(n);
      if (tok == tEOF || tok == tSEMI)
         break;
      else if (depth == 0 && tok == tCOMMA)
         break;
      else if (tok == tLPAREN || tok == tLBRACE || tok == tLSQUARE) {
         has_call |= tok == tLPAREN && n > 1 && peek_nth(n - 1) == tID;
         depth++;
      }
      else if (tok == tRPAREN || tok == tRBRACE || tok == tRSQUARE)
         depth--;
   }

   if (!has_call)
      return false;

   deferred_body_t d = {
      .subr           = decl,
      .scope          = vlog_symtab_current(symtab),
      .proc_automatic = proc_automatic,
      .subr_automatic = subr_automatic,
      .is_init        = true,
      .container      = spec_container,
   };

   for (int i = 1; i < n; i++) {
      tokenq_t saved = state.tokenq[state.tokenq_tail];
      drop_token(&state);

      d.body.tokens = xrealloc_array(d.body.tokens, d.body.ntokens + 1,
                                     sizeof(tokenq_t));
      d.body.tokens[d.body.ntokens++] = saved;

      (void)peek();
   }

   APUSH(deferred_bodies, d);
   return true;
}

static bool defer_subr_body(vlog_node_t subr, token_t end_token)
{
   // Called with the header parsed and the scope of the subroutine on top

   if (!want_defer_body())
      return false;

   deferred_body_t d = {
      .subr           = subr,
      .scope          = vlog_symtab_current(symtab),
      .end_token      = end_token,
      .proc_automatic = proc_automatic,
      .subr_automatic = subr_automatic,
      .in_subroutine  = in_subroutine,
      .func           = current_func,
      .container      = spec_container,
   };

   capture_tokens_until(&d.body, end_token);

   APUSH(deferred_bodies, d);
   return true;
}

static void p_deferred_body(const deferred_body_t *d)
{
   BEGIN("subroutine body");

   vlog_node_t subr = d->subr;
   const vlog_kind_t kind = vlog_kind(subr);

   if (d->is_constraint) {
      consume(tLBRACE);

      while (not_at_token(tRBRACE)) {
         if (!optional(tSEMI))
            vlog_add_stmt(subr, p_constraint_expression());
      }

      consume(tRBRACE);
      return;
   }

   if (d->is_init) {
      if (peek() == tTICK && vlog_has_type(subr))
         vlog_set_value(subr, p_expression_of_type(
                           vlog_get_type(vlog_type(subr))));
      else
         vlog_set_value(subr, p_expression());
      return;
   }

   skip_over_attributes();

   while (scan_block_item_declaration()) {
      p_block_item_declaration(subr);
      skip_over_attributes();
   }

   if (kind == V_CONSTRUCTOR && optional(tSUPER)) {
      consume(tDOT);
      consume(tNEW);

      vlog_node_t call = vlog_new(V_SUPER_CALL);
      vlog_set_loc(call, &state.last_loc);

      if (optional(tLPAREN)) {
         p_list_of_arguments(call);
         consume(tRPAREN);
      }

      vlog_add_stmt(subr, call);
   }

   while (not_at_token(d->end_token)) {
      vlog_node_t s = p_statement_or_null();
      if (s != NULL)
         vlog_add_stmt(subr, s);
   }

   consume(d->end_token);

   if (optional(tCOLON)) {
      if (kind == V_CONSTRUCTOR)
         consume(tNEW);
      else {
         ident_t name = p_identifier(), id = vlog_ident(subr);
         if (id != name)
            error_at(&state.last_loc, "'%s' does not match %s name '%s'",
                     istr(name), kind == V_FUNC_DECL ? "function" : "task",
                     istr(id));
      }
   }
}

static void put_generic_tokens(text_buf_t *tb, const tokenq_t *tokens, int n)
{
   tb_printf(tb, "%d\n", n);
   for (int i = 0; i < n; i++) {
      const tokenq_t *t = &(tokens[i]);
      tb_printf(tb, "%d %u ", t->token, t->loc.first_line);
      switch (t->token) {
      case tID:
      case tSYSTASK:
         {
            const char *str = istr(t->lval.ident);
            tb_printf(tb, "%zu %s", strlen(str), str);
         }
         break;
      case tUNSNUM:
      case tNUMBER:
      case tTIMELIT:
      case tBITSTRING:
         tb_printf(tb, "%zu %s", strlen(t->lval.str), t->lval.str);
         break;
      case tSTRING:
         tb_printf(tb, "%zu ", tb_len(t->lval.text));
         tb_catn(tb, tb_get(t->lval.text), tb_len(t->lval.text));
         break;
      case tREAL:
         tb_printf(tb, "%a", t->lval.real);
         break;
      default:
         break;
      }
      tb_append(tb, '\n');
   }
}

static const char *get_generic_tokens(const char *p, const loc_t *base,
                                      tokenq_t **tokens, int *n)
{
   char *eptr;
   *n = strtol(p, &eptr, 10);
   p = eptr + 1;

   *tokens = xcalloc_array(MAX(*n, 1), sizeof(tokenq_t));

   for (int i = 0; i < *n; i++) {
      tokenq_t *t = &((*tokens)[i]);
      t->token = strtol(p, &eptr, 10);
      p = eptr + 1;
      t->loc = *base;
      t->loc.first_line = strtoul(p, &eptr, 10);
      p = eptr + 1;

      switch (t->token) {
      case tID:
      case tSYSTASK:
         {
            const size_t len = strtoul(p, &eptr, 10);
            t->lval.ident = ident_new_n(eptr + 1, len);
            p = eptr + 1 + len;
         }
         break;
      case tUNSNUM:
      case tNUMBER:
      case tTIMELIT:
      case tBITSTRING:
         {
            const size_t len = strtoul(p, &eptr, 10);
            t->lval.str = xstrndup(eptr + 1, len);
            p = eptr + 1 + len;
         }
         break;
      case tSTRING:
         {
            const size_t len = strtoul(p, &eptr, 10);
            t->lval.text = tb_new();
            tb_catn(t->lval.text, eptr + 1, len);
            p = eptr + 1 + len;
         }
         break;
      case tREAL:
         t->lval.real = strtod(p, &eptr);
         p = eptr;
         break;
      default:
         break;
      }

      assert(*p == '\n');
      p++;
   }

   return p;
}

static void save_generic_classes(void)
{
   // Parsing a specialisation in another design unit needs the tokens

   for (int i = 0; i < unit_generics.count; i++) {
      vlog_node_t v = unit_generics.items[i];
      generic_class_t *g = hash_get(generic_classes, v);

      LOCAL_TEXT_BUF tb = tb_new();
      tb_printf(tb, "%d %d\n", g->nparams, g->nexterns);

      for (int j = 0; j < g->nparams; j++) {
         const generic_param_t *p = &(g->params[j]);
         const char *name = istr(p->name);
         tb_printf(tb, "%zu %s %d %d\n", strlen(name), name, p->is_type,
                   p->has_def);
         if (p->tname != NULL)
            tb_printf(tb, "T %zu %s\n", strlen(istr(p->tname)),
                      istr(p->tname));
         put_generic_tokens(tb, p->def, p->ndef);
      }

      put_generic_tokens(tb, g->tokens, g->ntokens);

      for (int j = 0; j < g->nexterns; j++)
         put_generic_tokens(tb, g->externs[j]->tokens, g->externs[j]->ntokens);

      // Only classes outside modules can be used by other design units
      if (vlog_has_ident2(v) || current_namespace != NULL
          || vlog_symtab_design_unit(symtab) != NULL) {
         vlog_node_t du = vlog_symtab_design_unit(symtab);
         ident_t owner = du != NULL ? vlog_ident(du)
            : vlog_ident(current_namespace);

         LOCAL_TEXT_BUF path = tb_new();
         tb_printf(path, "%s.%s.generic", istr(owner), istr(vlog_ident(v)));

         FILE *f = lib_fopen(lib_work(), tb_get(path), "w");
         if (f != NULL) {
            fwrite(tb_get(tb), 1, tb_len(tb), f);
            fclose(f);
         }
      }
   }

   ARESIZE(unit_generics, 0);
}

static vlog_scope_t *generic_home_scope(vlog_node_t pack)
{
   // Everything visible in a package from the library

   if (generic_homes == NULL)
      generic_homes = hash_new(16);

   vlog_scope_t *scope = hash_get(generic_homes, pack);
   if (scope != NULL)
      return scope;

   vlog_scope_t *saved = vlog_symtab_enter(symtab, NULL);
   vlog_symtab_push(symtab, pack);

   const int ndecls = vlog_decls(pack);
   for (int i = 0; i < ndecls; i++) {
      vlog_node_t d = vlog_decl(pack, i);
      switch (vlog_kind(d)) {
      case V_IMPORT_DECL:
         vlog_symtab_import(symtab, d);
         break;
      case V_CLASS_DECL:
         if (!(vlog_flags(d) & VLOG_F_SPEC))
            vlog_symtab_put(symtab, d);
         break;
      case V_TYPE_DECL:
         put_enum_names(d);
         vlog_symtab_put(symtab, d);
         break;
      case V_VAR_DECL:
      case V_FUNC_DECL:
      case V_TASK_DECL:
      case V_LOCALPARAM:
      case V_PARAM_DECL:
         vlog_symtab_put(symtab, d);
         break;
      default:
         break;
      }
   }

   scope = vlog_symtab_current(symtab);
   vlog_symtab_enter(symtab, saved);

   hash_put(generic_homes, pack, scope);
   return scope;
}

static generic_class_t *load_generic_class(vlog_node_t generic)
{
   if (!vlog_has_ident2(generic))
      fatal_at(vlog_loc(generic), "parameterised class %pQ cannot be used here",
               vlog_ident(generic));

   lib_t lib = lib_require(ident_until(vlog_ident2(generic), '.'));
   object_t *obj = lib_get_generic(lib, vlog_ident2(generic), NULL);
   vlog_node_t pack = obj != NULL ? vlog_from_object(obj) : NULL;
   if (pack == NULL)
      fatal_at(vlog_loc(generic), "cannot find the package containing "
               "parameterised class %pQ", vlog_ident(generic));

   LOCAL_TEXT_BUF path = tb_new();
   tb_printf(path, "%s.%s.generic", istr(vlog_ident2(generic)),
             istr(vlog_ident(generic)));

   FILE *f = lib_fopen(lib, tb_get(path), "r");
   if (f == NULL)
      fatal_at(vlog_loc(generic), "cannot find the definition of parameterised "
               "class %pQ: analyse its package again", vlog_ident(generic));

   LOCAL_TEXT_BUF contents = tb_new();
   char chunk[4096];
   size_t nread;
   while ((nread = fread(chunk, 1, sizeof(chunk), f)) > 0)
      tb_catn(contents, chunk, nread);
   fclose(f);

   generic_class_t *g = xcalloc(sizeof(generic_class_t));
   g->name  = vlog_ident(generic);
   g->flags = vlog_flags(generic) & ~VLOG_F_GENERIC;
   g->scope = generic_home_scope(pack);
   g->home  = pack;

   const loc_t *base = vlog_loc(generic);
   const char *p = tb_get(contents);
   char *eptr;

   g->nparams = strtol(p, &eptr, 10);
   p = eptr + 1;
   g->nexterns = strtol(p, &eptr, 10);
   p = eptr + 1;

   g->params = xcalloc_array(MAX(g->nparams, 1), sizeof(generic_param_t));
   for (int i = 0; i < g->nparams; i++) {
      generic_param_t *gp = &(g->params[i]);

      const size_t len = strtoul(p, &eptr, 10);
      gp->name = ident_new_n(eptr + 1, len);
      p = eptr + 1 + len + 1;
      gp->is_type = strtol(p, &eptr, 10);
      p = eptr + 1;
      gp->has_def = strtol(p, &eptr, 10);
      p = eptr + 1;

      if (p[0] == 'T' && p[1] == ' ') {
         const size_t tlen = strtoul(p + 2, &eptr, 10);
         gp->tname = ident_new_n(eptr + 1, tlen);
         p = eptr + 1 + tlen + 1;
      }

      p = get_generic_tokens(p, base, &gp->def, &gp->ndef);
   }

   p = get_generic_tokens(p, base, &g->tokens, &g->ntokens);

   g->externs = xcalloc_array(MAX(g->nexterns, 1), sizeof(generic_class_t *));
   for (int i = 0; i < g->nexterns; i++) {
      g->externs[i] = xcalloc(sizeof(generic_class_t));
      p = get_generic_tokens(p, base, &g->externs[i]->tokens,
                             &g->externs[i]->ntokens);
   }

   if (generic_classes == NULL)
      generic_classes = hash_new(16);
   hash_put(generic_classes, generic, g);

   return g;
}

typedef struct {
   hset_t *decls;
   bool    found;
} class_refs_t;

static void class_refs_module_cb(vlog_node_t v, void *context)
{
   class_refs_t *ctx = context;

   switch (vlog_kind(v)) {
   case V_REF:
   case V_USER_FCALL:
   case V_USER_TCALL:
      if (vlog_has_ref(v) && hset_contains(ctx->decls, vlog_ref(v)))
         ctx->found = true;
      break;
   case V_HIER_REF:
   case V_MOD_REF:
      ctx->found = true;
      break;
   default:
      break;
   }
}

static void make_module_classes_global(vlog_node_t mod)
{
   // A class in a module is the same class in every instance if it does not
   // use anything from the module: a specialisation may refer to it and
   // that cannot depend on the instance

   hset_t *decls = hset_new(32);

   const int ndecls = vlog_decls(mod);
   for (int i = 0; i < ndecls; i++) {
      vlog_node_t d = vlog_decl(mod, i);
      switch (vlog_kind(d)) {
      case V_VAR_DECL:
      case V_NET_DECL:
      case V_PORT_DECL:
      case V_PARAM_DECL:
      case V_LOCALPARAM:
      case V_FUNC_DECL:
      case V_TASK_DECL:
         hset_insert(decls, d);
         break;
      default:
         break;
      }
   }

   for (int i = 0; i < ndecls; i++) {
      vlog_node_t d = vlog_decl(mod, i);
      if (vlog_kind(d) != V_CLASS_DECL)
         continue;
      else if (vlog_flags(d) & (VLOG_F_SPEC | VLOG_F_GENERIC))
         continue;
      else if (vlog_has_ref(d)
               && !(vlog_flags(vlog_ref(d)) & VLOG_F_GLOBAL)
               && !(vlog_flags(vlog_ref(d)) & VLOG_F_SPEC))
         continue;   // Base class depends on the instance

      class_refs_t ctx = { .decls = decls };
      vlog_visit(d, class_refs_module_cb, &ctx);

      if (!ctx.found) {
         ident_t unit = ident_prefix(
            lib_name(lib_work()),
            ident_sprintf("$spec$%s$%s", istr(vlog_ident2(mod)),
                          istr(vlog_ident(d))), '.');

         vlog_set_flags(d, vlog_flags(d) | VLOG_F_SPEC | VLOG_F_GLOBAL);
         vlog_set_ident2(d, unit);
         set_decl_scopes(d, ident_prefix(unit, vlog_ident(d), '.'));
         add_scope_init(mod, unit);
      }
   }

   hset_free(decls);
}

static void fix_spec_scopes(void)
{
   vlog_node_t mod = vlog_symtab_design_unit(symtab);
   if (mod != NULL && vlog_kind(mod) == V_MODULE)
      make_module_classes_global(mod);

   // A specialisation may be declared in every design unit that uses it
   // but all of them are the same class with one copy of the static members
   // so they share a scope which does not depend on the design unit

   for (int i = 0; i < unit_specs.count; i++) {
      vlog_node_t c = unit_specs.items[i];
      ident_t spec = ident_new("$spec$");
      vlog_node_t owner = spec_modules != NULL ? hash_get(spec_modules, c) : NULL;
      if (owner != NULL && vlog_has_ident2(owner))
         spec = ident_sprintf("$spec$%s$", istr(vlog_ident2(owner)));

      ident_t unit = ident_prefix(lib_name(lib_work()),
                                  ident_prefix(spec, vlog_ident(c), '\0'),
                                  '.');
      vlog_set_ident2(c, unit);
      vlog_set_flags(c, vlog_flags(c) | VLOG_F_GLOBAL);
      set_decl_scopes(c, ident_prefix(unit, vlog_ident(c), '.'));
   }

   save_generic_classes();

   // Initialised when the simulation starts if used by a module
   vlog_node_t du = vlog_symtab_design_unit(symtab);
   if (du != NULL && vlog_kind(du) == V_MODULE) {
      for (int i = 0; i < unit_specs.count; i++)
         add_scope_init(du, vlog_ident2(unit_specs.items[i]));
   }

   ARESIZE(unit_specs, 0);

   if (spec_modules != NULL) {
      hash_free(spec_modules);
      spec_modules = NULL;
   }
}

static void flush_deferred_bodies(bool end_of_unit)
{
   if (flushing_bodies || class_depth > 0)
      return;

   // Methods in a package can use anything declared anywhere in the package
   vlog_node_t du = vlog_symtab_design_unit(symtab);
   if (!end_of_unit && du != NULL && vlog_kind(du) == V_PACKAGE)
      return;

   if (end_of_unit) {
      for (int i = 0; i < forward_classes.count; i++) {
         vlog_node_t c = forward_classes.items[i];
         error_at(vlog_loc(c), "class %pQ has a forward declaration but is "
                  "not declared", vlog_ident(c));
         vlog_clear_flags(c, VLOG_F_FORWARD);
      }
      ARESIZE(forward_classes, 0);
   }
   else if (forward_classes.count > 0)
      return;

   if (deferred_bodies.count == 0) {
      if (end_of_unit)
         fix_spec_scopes();
      return;
   }

   flushing_bodies = true;

   parse_state_t saved = state;
   generic_class_t *saved_class = replay_class;
   const int saved_pos = replay_pos;
   const bool saved_proc = proc_automatic, saved_subr = subr_automatic;
   const bool saved_in_subr = in_subroutine, saved_proto = parsing_prototype;
   const vlog_kind_t saved_kind = param_kind;
   vlog_node_t saved_func = current_func, saved_container = spec_container;
   vlog_scope_t *saved_scope = vlog_symtab_current(symtab);

   for (int i = 0; i < deferred_bodies.count; i++) {
      // More may be added if the body causes a class to be specialised
      deferred_body_t d = deferred_bodies.items[i];

      replay_class = &d.body;
      replay_pos = 0;
      parsing_prototype = false;
      param_kind = V_LOCALPARAM;
      proc_automatic = d.proc_automatic;
      subr_automatic = d.subr_automatic;
      in_subroutine = d.in_subroutine;
      current_func = d.func;
      spec_container = d.container;

      state.lex_fn = replay_lex;
      state.tokenq_head = state.tokenq_tail = 0;
      state.start_loc = LOC_INVALID;

      vlog_symtab_enter(symtab, d.scope);

      p_deferred_body(&d);

      // Names that were not found are resolved in the enclosing scopes
      while (vlog_symtab_current(symtab) != saved_scope
             && vlog_symtab_current(symtab) != NULL)
         vlog_symtab_pop(symtab);

      free(d.body.tokens);
   }

   ARESIZE(deferred_bodies, 0);

   vlog_symtab_enter(symtab, saved_scope);

   if (end_of_unit)
      fix_spec_scopes();

   state = saved;
   replay_class = saved_class;
   replay_pos = saved_pos;
   proc_automatic = saved_proc;
   subr_automatic = saved_subr;
   in_subroutine = saved_in_subr;
   parsing_prototype = saved_proto;
   param_kind = saved_kind;
   current_func = saved_func;
   spec_container = saved_container;

   vlog_symtab_enter(symtab, saved_scope);

   flushing_bodies = false;
}

static vlog_node_t specialise_forward(vlog_node_t forward)
{
   // [ # ( list_of_parameter_assignments ) ] of a class not yet declared

   EXTEND("forward class specialisation");

   forward_spec_t f = {
      .forward = forward,
      .scope   = vlog_symtab_current(symtab),
      .key     = tb_new(),
   };

   int depth = 0;
   bool started = false;
   do {
      const token_t tok = peek();
      if (tok == tEOF) {
         expect(tRPAREN);
         break;
      }
      else if (tok == tHASH)
         started = false;
      else
         started = true;

      if (tok == tEOF)
         break;
      else if (tok == tLPAREN)
         depth++;
      else if (tok == tRPAREN)
         depth--;

      tokenq_t saved = state.tokenq[state.tokenq_tail];
      drop_token(&state);

      f.actuals.tokens = xrealloc_array(f.actuals.tokens,
                                        f.actuals.ntokens + 1,
                                        sizeof(tokenq_t));
      f.actuals.tokens[f.actuals.ntokens++] = saved;

      tb_printf(f.key, "%d:", tok);
      if (tok == tID)
         tb_istr(f.key, saved.lval.ident);
      else if (tok == tUNSNUM || tok == tNUMBER)
         tb_cat(f.key, saved.lval.str);
      else if (tok == tSTRING)
         tb_catn(f.key, tb_get(saved.lval.text), tb_len(saved.lval.text));
      tb_append(f.key, ',');
   } while (depth > 0 || !started);

   for (int i = 0; i < forward_specs.count; i++) {
      const forward_spec_t *o = &(forward_specs.items[i]);
      if (o->forward == forward
          && strcmp(tb_get(o->key), tb_get(f.key)) == 0) {
         tb_free(f.key);
         free(f.actuals.tokens);
         return o->placeholder;
      }
   }

   f.container = spec_container;
   if (f.container == NULL)
      f.container = vlog_symtab_design_unit(symtab);
   if (f.container == NULL)
      f.container = current_namespace;

   f.placeholder = vlog_new(V_CLASS_DECL);
   vlog_set_ident(f.placeholder, ident_uniq("forward"));
   vlog_set_flags(f.placeholder, VLOG_F_FORWARD);
   vlog_set_loc(f.placeholder, &state.last_loc);

   APUSH(forward_specs, f);

   return f.placeholder;
}

static void resolve_forward_specs(vlog_node_t class)
{
   for (int i = 0; i < forward_specs.count; i++) {
      forward_spec_t f = forward_specs.items[i];
      if (f.forward != class)
         continue;

      parse_state_t saved = state;
      generic_class_t *saved_class = replay_class;
      const int saved_pos = replay_pos;
      vlog_node_t saved_container = spec_container;

      replay_class = &f.actuals;
      replay_pos = 0;
      spec_container = f.container;

      state.lex_fn = replay_lex;
      state.tokenq_head = state.tokenq_tail = 0;
      state.start_loc = LOC_INVALID;

      vlog_scope_t *saved_scope = vlog_symtab_enter(symtab, f.scope);

      spec_into = f.placeholder;
      (void)specialise_class(class);
      spec_into = NULL;

      vlog_symtab_enter(symtab, saved_scope);

      state = saved;
      replay_class = saved_class;
      replay_pos = saved_pos;
      spec_container = saved_container;
   }
}

static vlog_node_t p_class_declaration(void)
{
   // [ virtual ] class [ lifetime ] class_identifier [ parameter_port_list ]
   //   [ extends class_type [ ( list_of_arguments ) ] ]
   //   [ implements interface_class_type { , interface_class_type } ] ;
   //   { class_item } endclass [ : class_identifier ]

   BEGIN("class declaration");

   vlog_flags_t flags = 0;
   if (optional(tVIRTUAL))
      flags = VLOG_F_VIRTUAL;
   else if (optional(tINTERFACE))
      flags = VLOG_F_VIRTUAL | VLOG_F_INTERFACE;

   consume(tCLASS);

   ident_t name = p_identifier();

   if (peek() == tHASH)
      return p_generic_class(name, flags);

   vlog_node_t v = resolve_forward_class(name);
   if (v == NULL)
      v = vlog_new(V_CLASS_DECL);

   vlog_set_ident(v, name);
   vlog_set_flags(v, flags);

   p_class_body(v, name, NULL, NULL);

   vlog_set_loc(v, CURRENT_LOC);

   flush_deferred_bodies(false);

   return v;
}

static void p_block_item_declaration(vlog_node_t parent)
{
   // { attribute_instance } data_declaration
   //   | { attribute_instance } local_parameter_declaration ;
   //   | { attribute_instance } parameter_declaration ;
   //   | { attribute_instance } overload_declaration
   //   | { attribute_instance } let_declaration

   BEGIN("block item declaration");

   optional_attributes();

   switch (peek()) {
   case tREG:
   case tSTRUCT:
   case tUNION:
   case tTYPEDEF:
   case tENUM:
   case tSVINT:
   case tINTEGER:
   case tSVREAL:
   case tSHORTREAL:
   case tREALTIME:
   case tBIT:
   case tLOGIC:
   case tSHORTINT:
   case tTIME:
   case tTYPE:
   case tID:
   case tAUTOMATIC:
   case tSTATIC:
   case tCONST:
   case tLONGINT:
   case tBYTE:
   case tSTRINGK: case tCHANDLE:
   case tEVENT:
   case tVIRTUAL:
   case tVAR:
   case tIMPORT:
      p_data_declaration(parent, V_LOCAL_DECL);
      break;
   case tLOCALPARAM:
      p_local_parameter_declaration(parent);
      consume(tSEMI);
      break;
   case tPARAMETER:
      p_parameter_declaration(parent);
      consume(tSEMI);
      break;
   default:
      should_not_reach_here();
   }
}

static FILE *dpi_stub_file = NULL;
static shash_t *dpi_stub_names = NULL;

void vlog_set_dpi_stubs(const char *path)
{
   if ((dpi_stub_file = fopen(path, "w")) == NULL)
      fatal_errno("%s", path);

   dpi_stub_names = shash_new(16);

   fprintf(dpi_stub_file,
           "// Generated by nvc --dpi-stubs: link this file into the DLL\n"
           "// with the C code that calls the DPI-exported subroutines of the\n"
           "// design.  Each call jumps through a slot nvc fills in when the\n"
           "// DLL is loaded.\n\n");
}

static void dpi_stub_emit(const char *name)
{
   if (dpi_stub_file == NULL || shash_get(dpi_stub_names, name) != NULL)
      return;

   shash_put(dpi_stub_names, name, (void *)name);

   fprintf(dpi_stub_file,
           "#ifdef _WIN32\n"
           "__declspec(dllexport) void *__nvc_dpi_slot_%s;\n"
           "__asm__(\".text\\n\"\n"
           "        \".globl %s\\n\"\n"
           "        \".def %s; .scl 2; .type 32; .endef\\n\"\n"
           "        \"%s:\\n\"\n"
           "        \"jmp *__nvc_dpi_slot_%s(%%rip)\\n\");\n"
           "#endif\n\n", name, name, name, name, name);
   fflush(dpi_stub_file);
}

static void p_dpi_export(vlog_node_t parent)
{
   // export "DPI-C" [ c_identifier = ] function name ;
   //   | export "DPI-C" [ c_identifier = ] task name ;

   BEGIN("DPI export declaration");

   consume(tEXPORT);
   consume(tSTRING);

   ident_t cname = NULL;
   if (peek() == tID && peek_nth(2) == tEQ) {
      cname = p_identifier();
      consume(tEQ);
   }

   if (!optional(tFUNCTION))
      consume(tTASK);

   vlog_node_t v = vlog_new(V_DPI_EXPORT);
   vlog_set_ident(v, p_identifier());
   vlog_set_ident3(v, cname ?: vlog_ident(v));

   dpi_stub_emit(istr(vlog_ident3(v)));

   consume(tSEMI);

   vlog_set_loc(v, CURRENT_LOC);
   vlog_add_decl(parent, v);
}

static void p_package_export_declaration(vlog_node_t parent)
{
   // export package_import_item { , package_import_item } ;
   //   | export *::* ;

   BEGIN("package export declaration");

   consume(tEXPORT);

   do {
      vlog_node_t v = vlog_new(V_IMPORT_DECL);
      vlog_set_flags(v, VLOG_F_EXPORT);

      if (optional(tTIMES)) {
         consume(tSCOPE);
         consume(tTIMES);
      }
      else {
         vlog_set_ref(v, p_package_identifier());

         consume(tSCOPE);

         if (peek() == tID)
            vlog_set_ident(v, p_identifier());
         else
            one_of(tTIMES, tID);
      }

      vlog_set_loc(v, CURRENT_LOC);
      vlog_add_decl(parent, v);
   } while (optional(tCOMMA));

   consume(tSEMI);
}

static void p_package_or_generate_item_declaration(vlog_node_t parent)
{
   // net_declaration | data_declaration | task_declaration
   //   | function_declaration | checker_declaration | dpi_import_export
   //   | extern_constraint_declaration | class_declaration
   //   | class_constructor_declaration | local_parameter_declaration ;
   //   | parameter_declaration ; | covergroup_declaration
   //   | overload_declaration | assertion_item_declaration | ;

   BEGIN("package or generate item declaration");

   if (peek() == tSTATIC && peek_nth(2) == tCONSTRAINT) {
      p_extern_constraint();
      return;
   }

   if (peek() == tEXPORT && peek_nth(2) == tSTRING) {
      p_dpi_export(parent);
      return;
   }

   if (peek() == tEXPORT) {
      p_package_export_declaration(parent);
      return;
   }

   switch (peek()) {
   case tWIRE:
   case tUWIRE:
   case tSUPPLY0:
   case tSUPPLY1:
   case tTRI:
   case tTRI0:
   case tTRI1:
   case tTRIAND:
   case tTRIOR:
   case tTRIREG:
   case tWAND:
   case tWOR:
   case tINTERCONNECT:
      p_net_declaration(parent);
      break;
   case tREG:
   case tSTRUCT:
   case tUNION:
   case tTYPEDEF:
   case tNETTYPE:
   case tENUM:
   case tSVINT:
   case tINTEGER:
   case tSVREAL:
   case tSHORTREAL:
   case tREALTIME:
   case tTIME:
   case tEVENT:
   case tTYPE:
   case tID:
   case tVAR:
   case tLOGIC:
   case tBIT:
   case tSHORTINT:
   case tLONGINT:
   case tBYTE:
   case tSTRINGK: case tCHANDLE:
   case tIMPORT:
      p_data_declaration(parent, V_VAR_DECL);
      break;
   case tTASK:
      {
         vlog_node_t task = p_task_declaration();
         if (task != NULL)
            vlog_add_decl(parent, task);
      }
      break;
   case tFUNCTION:
      {
         vlog_node_t func = p_function_declaration();
         if (func != NULL)
            vlog_add_decl(parent, func);
      }
      break;
   case tLOCALPARAM:
      p_local_parameter_declaration(parent);
      consume(tSEMI);
      break;
   case tPARAMETER:
      p_parameter_declaration(parent);
      consume(tSEMI);
      break;
   case tSEMI:
      consume(tSEMI);
      break;
   case tSEQUENCE:
   case tPROPERTY:
      p_assertion_item_declaration(parent);
      break;
   case tCLOCKING:
      p_clocking_declaration(parent, false);
      break;
   case tDEFAULT:
      p_default_declaration(parent);
      break;
   case tCLASS:
      vlog_add_decl(parent, p_class_declaration());
      break;
   case tINTERFACE:
      vlog_add_decl(parent, p_class_declaration());
      break;
   case tCONSTRAINT:
      p_extern_constraint();
      break;
   case tVIRTUAL:
      if (peek_nth(2) == tCLASS)
         vlog_add_decl(parent, p_class_declaration());
      else
         p_data_declaration(parent, V_VAR_DECL);
      break;
   case tSTATIC:
   case tAUTOMATIC:
   case tCONST:
      (void)one_of(tSTATIC, tAUTOMATIC, tCONST);
      p_package_or_generate_item_declaration(parent);
      break;
   default:
      one_of(tWIRE, tUWIRE, tSUPPLY0, tSUPPLY1, tTRI, tTRI0, tTRI1, tTRIAND,
             tTRIOR, tTRIREG, tWAND, tWOR, tINTERCONNECT, tREG, tSTRUCT, tUNION,
             tTYPEDEF, tENUM, tSVINT, tINTEGER, tSVREAL, tSHORTREAL, tREALTIME,
             tTIME, tEVENT, tID, tVAR, tLOGIC, tBIT, tSHORTINT, tLONGINT, tBYTE,
             tSTRINGK, tIMPORT, tTASK, tFUNCTION, tLOCALPARAM, tPARAMETER,
             tCLASS, tVIRTUAL);
      drop_tokens_until(&state, tSEMI);
      break;
   }
}

static void p_list_of_genvar_identifiers(vlog_node_t mod, vlog_node_t dt)
{
   // genvar_identifier { , genvar_identifier }

   BEGIN("list of genvar identifiers");

   do {
      vlog_node_t v = vlog_new(V_GENVAR_DECL);
      vlog_set_ident(v, p_identifier());
      vlog_set_type(v, dt);
      vlog_set_loc(v, CURRENT_LOC);

      vlog_add_decl(mod, v);

      vlog_symtab_put(symtab, v);
   } while (optional(tCOMMA));
}

static void p_genvar_declaration(vlog_node_t mod)
{
   // genvar list_of_genvar_identifiers ;

   BEGIN("genvar declaration");

   consume(tGENVAR);

   vlog_node_t dt = make_integer_atom_type(DT_INTEGER);
   vlog_set_flags(dt, VLOG_F_SIGNED);

   p_list_of_genvar_identifiers(mod, dt);

   consume(tSEMI);
}

static void p_module_or_generate_item_declaration(vlog_node_t mod)
{
   // package_or_generate_item_declaration | genvar_declaration
   //   | clocking_declaration | default clocking clocking_identifier ;
   //   | default disable iff expression_or_dist ;

   BEGIN("module or generate item declaration");

   switch (peek()) {
   case tGENVAR:
      p_genvar_declaration(mod);
      break;
   default:
      p_package_or_generate_item_declaration(mod);
      break;
   }
}

static void p_generate_item(vlog_node_t parent)
{
   // module_or_generate_item | interface_or_generate_item
   //   | checker_or_generate_item

   BEGIN("generate item");

   p_module_or_generate_item(parent);
}

static ident_t generate_block_name(void)
{
   // An unnamed generate block is named genblk<n> where n is the number
   // of its generate construct in the scope, with leading zeros added
   // when that is the name of a declaration (27.6)

   if (gen_construct == 0)
      return default_label("genblk");

   LOCAL_TEXT_BUF tb = tb_new();
   tb_printf(tb, "genblk%u", gen_construct);

   ident_t id = ident_new(tb_get(tb));
   for (int zeros = 1; vlog_symtab_query(symtab, id) != NULL; zeros++) {
      tb_rewind(tb);
      tb_cat(tb, "genblk");
      for (int i = 0; i < zeros; i++)
         tb_append(tb, '0');
      tb_printf(tb, "%u", gen_construct);
      id = ident_new(tb_get(tb));
   }

   return id;
}

static vlog_node_t p_generate_block(void)
{
   // generate_item
   //   | [ generate_block_identifier : ] begin [ : generate_block_identifier ]
   //         { generate_item } end [ : generate_block_identifier ]

   BEGIN("generate block");

   vlog_node_t b = vlog_new(V_GEN_BLOCK);

   if (peek() == tBEGIN || (peek() == tID && peek_nth(2) == tCOLON)) {
      ident_t label = NULL;
      if (peek() == tID) {
         vlog_set_ident(b, (label = p_identifier()));
         consume(tCOLON);
      }

      consume(tBEGIN);

      if (optional(tCOLON)) {
         ident_t name = p_identifier();
         if (label != NULL)    // 1800-2023 section 9.3.5
            parse_error(&state.last_loc, "cannot specify both a label and a "
                        "name for the same block");
         else
            vlog_set_ident(b, (label = name));
      }

      if (!vlog_has_ident(b))
         vlog_set_ident(b, generate_block_name());

      vlog_symtab_push(symtab, b);

      vlog_node_t saved_scope = current_module;
      current_module = b;

      const unsigned first_tick = shared_ticks.count;
      const unsigned saved_constructs = gen_constructs;
      gen_constructs = 0;

      while (not_at_token(tEND))
         p_generate_item(b);

      gen_constructs = saved_constructs;
      shared_ticks.count = first_tick;
      current_module = saved_scope;

      vlog_symtab_pop(symtab);

      consume(tEND);

      if (optional(tCOLON)) {
         ident_t name = p_identifier();
         if (label == NULL)
            parse_error(&state.last_loc, "block does not have a label");
         else if (name != label)
            parse_error(&state.last_loc, "%pQ does not match label %pQ",
                        name, label);
      }
   }
   else {
      vlog_set_ident(b, generate_block_name());

      const unsigned saved_constructs = gen_constructs;
      gen_constructs = 0;

      p_generate_item(b);

      gen_constructs = saved_constructs;
   }

   vlog_set_loc(b, CURRENT_LOC);
   return b;
}

static bool gen_literal(vlog_node_t v, int64_t *value)
{
   // A number which is not the value of a parameter that can be overridden

   return vlog_kind(v) == V_NUMBER && vlog_get_const(v, value);
}

static vlog_node_t p_if_generate_construct(void)
{
   // if ( constant_expression ) generate_block
   //   { else if ( constant_expression ) generate_block }
   //   [ else generate_block ]

   BEGIN("if generate construct");

   vlog_node_t v = vlog_new(V_IF_GENERATE);

   consume(tIF);
   consume(tLPAREN);

   vlog_node_t c0 = vlog_new(V_COND);
   vlog_set_value(c0, p_constant_expression());

   consume(tRPAREN);

   // The branch is never taken if the condition is a constant zero and the
   // later ones are not if it is a constant true
   int64_t cv;
   const int dead = gen_literal(vlog_value(c0), &cv) && cv == 0;
   bool taken = gen_literal(vlog_value(c0), &cv) && cv != 0;

   vlog_set_loc(c0, CURRENT_LOC);
   gen_dead += dead;
   vlog_add_stmt(c0, p_generate_block());
   gen_dead -= dead;

   vlog_add_cond(v, c0);

   while (optional(tELSE)) {
      if (peek() == tIF) {
         // else if ( constant_expression ) generate_block
         consume(tIF);
         consume(tLPAREN);

         vlog_node_t cn = vlog_new(V_COND);
         vlog_set_value(cn, p_constant_expression());

         consume(tRPAREN);

         const bool zero = gen_literal(vlog_value(cn), &cv) && cv == 0;
         const int dead_n = taken || zero;
         if (gen_literal(vlog_value(cn), &cv) && cv != 0)
            taken = true;

         vlog_set_loc(cn, CURRENT_LOC);
         gen_dead += dead_n;
         vlog_add_stmt(cn, p_generate_block());
         gen_dead -= dead_n;

         vlog_add_cond(v, cn);
      }
      else {
         // else generate_block (no condition = default branch)
         vlog_node_t ce = vlog_new(V_COND);
         vlog_set_loc(ce, &state.last_loc);
         gen_dead += taken;
         vlog_add_stmt(ce, p_generate_block());
         gen_dead -= taken;

         vlog_add_cond(v, ce);
         break;
      }
   }

   vlog_set_loc(v, CURRENT_LOC);
   return v;
}

static vlog_node_t p_case_generate_construct(void)
{
   // case ( constant_expression )
   //   case_generate_item { case_generate_item } endcase
   //
   // Lowered to V_IF_GENERATE with equality comparisons so the existing
   // simplifier and elaborator handle it without a new node kind.

   BEGIN("case generate construct");

   consume(tCASE);
   consume(tLPAREN);

   vlog_node_t selector = p_constant_expression();

   consume(tRPAREN);

   vlog_node_t v = vlog_new(V_IF_GENERATE);

   while (not_at_token(tENDCASE)) {
      vlog_node_t cn = vlog_new(V_COND);

      if (optional(tDEFAULT)) {
         // default branch: no condition (like else)
         optional(tCOLON);
      }
      else {
         // case_generate_item: constant_expression { , constant_expression }
         //   : generate_block
         // Build equality comparison: selector == label
         // For multiple labels, OR them: (sel == l1) || (sel == l2)
         vlog_node_t cond = NULL;
         do {
            vlog_node_t eq = vlog_new(V_BINARY);
            vlog_set_subkind(eq, V_BINARY_LOG_EQ);
            vlog_set_left(eq, selector);
            vlog_set_right(eq, p_constant_expression());
            vlog_set_loc(eq, CURRENT_LOC);

            if (cond == NULL) {
               cond = eq;
            }
            else {
               vlog_node_t lor = vlog_new(V_BINARY);
               vlog_set_subkind(lor, V_BINARY_LOG_OR);
               vlog_set_left(lor, cond);
               vlog_set_right(lor, eq);
               vlog_set_loc(lor, CURRENT_LOC);
               cond = lor;
            }
         } while (optional(tCOMMA));

         consume(tCOLON);
         vlog_set_value(cn, cond);
      }

      vlog_set_loc(cn, CURRENT_LOC);
      vlog_add_stmt(cn, p_generate_block());
      vlog_add_cond(v, cn);
   }

   consume(tENDCASE);

   vlog_set_loc(v, CURRENT_LOC);
   return v;
}

static vlog_node_t p_conditional_generate_construct(void)
{
   // if_generate_construct | case_generate_construct

   BEGIN("conditional generate construct");

   const unsigned saved_construct = gen_construct;
   gen_construct = ++gen_constructs;

   vlog_node_t v;
   switch (peek()) {
   case tIF:
      v = p_if_generate_construct();
      break;
   case tCASE:
      v = p_case_generate_construct();
      break;
   default:
      should_not_reach_here();
   }

   gen_construct = saved_construct;
   return v;
}

static vlog_node_t p_genvar_initialization(vlog_node_t *var)
{
   // [ genvar ] genvar_identifier = constant_expression

   BEGIN("genvar initialization");

   ident_t id;
   if (optional(tGENVAR)) {
      vlog_node_t dt = make_integer_atom_type(DT_INTEGER);
      vlog_set_flags(dt, VLOG_F_SIGNED);

      *var = vlog_new(V_GENVAR_DECL);
      vlog_set_ident(*var, (id = p_identifier()));
      vlog_set_type(*var, dt);
      vlog_set_loc(*var, &state.last_loc);

      vlog_symtab_put(symtab, *var);
   }
   else
      id = p_identifier();

   vlog_node_t v = vlog_new(V_FOR_INIT);

   vlog_node_t ref = vlog_new(V_REF);
   vlog_set_ident(ref, id);
   vlog_set_loc(ref, &state.last_loc);

   vlog_symtab_lookup(symtab, ref);

   consume(tEQ);

   vlog_node_t a = vlog_new(V_BASSIGN);
   vlog_set_target(a, ref);
   vlog_set_value(a, p_constant_expression());
   vlog_set_loc(a, CURRENT_LOC);

   vlog_add_stmt(v, a);

   vlog_set_loc(v, CURRENT_LOC);
   return v;
}

static vlog_node_t p_genvar_iteration(void)
{
   // genvar_identifier assignment_operator genvar_expression
   //   | inc_or_dec_operator genvar_identifier
   //   | genvar_identifier inc_or_dec_operator

   BEGIN("genvar iteration");

   vlog_node_t v = vlog_new(V_FOR_STEP);

   vlog_node_t prefix = NULL;
   if (scan(tPLUSPLUS, tMINUSMINUS)) {
      prefix = vlog_new(V_PREFIX);
      vlog_set_subkind(prefix, p_inc_or_dec_operator());
   }

   vlog_node_t ref = vlog_new(V_REF);
   vlog_set_ident(ref, p_identifier());
   vlog_set_loc(ref, &state.last_loc);

   vlog_symtab_lookup(symtab, ref);

   if (prefix != NULL) {
      vlog_set_target(prefix, ref);

      vlog_add_stmt(v, prefix);
   }
   else if (optional(tEQ)) {
      vlog_node_t a = vlog_new(V_BASSIGN);
      vlog_set_target(a, ref);
      vlog_set_value(a, p_constant_expression());

      vlog_add_stmt(v, a);
   }
   else {
      vlog_node_t a = vlog_new(V_POSTFIX);
      vlog_set_subkind(a, p_inc_or_dec_operator());
      vlog_set_target(a, ref);

      vlog_add_stmt(v, a);
   }

   vlog_set_loc(v, CURRENT_LOC);
   return v;
}

static void p_loop_generate_construct(vlog_node_t parent)
{
   // for ( genvar_initialization ; genvar_expression ; genvar_iteration )
   //   generate_block

   BEGIN("loop generate construct");

   consume(tFOR);
   consume(tLPAREN);

   vlog_node_t v = vlog_new(V_FOR_GENERATE);

   vlog_symtab_push(symtab, NULL);

   vlog_node_t var = NULL;
   vlog_set_left(v, p_genvar_initialization(&var));

   consume(tSEMI);

   vlog_set_value(v, p_constant_expression());

   consume(tSEMI);

   vlog_set_right(v, p_genvar_iteration());

   consume(tRPAREN);

   const unsigned saved_construct = gen_construct;
   gen_construct = ++gen_constructs;

   vlog_node_t b = p_generate_block();
   vlog_add_stmt(v, b);

   gen_construct = saved_construct;

   vlog_symtab_pop(symtab);

   vlog_set_loc(v, CURRENT_LOC);
   vlog_add_stmt(parent, v);

   if (var != NULL) {
      // Genvar must be allocated in parent context
      ident_t prefixed = ident_prefix(vlog_ident(b), vlog_ident(var), '.');
      vlog_set_ident(var, prefixed);
      vlog_add_decl(parent, var);
   }
}

static vlog_node_t p_property_spec(void)
{
   // [ clocking_event ] [ disable iff ( expression_or_dist ) ] property_expr

   BEGIN("property spec");

   vlog_node_t v = vlog_new(V_PROP_SPEC);

   if (peek() == tAT)
      vlog_set_target(v, p_clocking_event());

   if (optional(tDISABLE)) {
      consume(tIFFKW);
      consume(tLPAREN);
      vlog_set_left(v, p_expression_or_dist());
      consume(tRPAREN);
   }

   vlog_set_value(v, p_property_expr());

   vlog_set_loc(v, CURRENT_LOC);
   return v;
}

static void sva_apply_defaults(vlog_node_t spec)
{
   // Move a clocking event or disable condition from the root of the
   // property into the specification
   for (;;) {
      vlog_node_t body = vlog_value(spec);
      if (vlog_kind(body) != V_SVA_OP)
         break;
      else if (vlog_subkind(body) == V_SVA_CLOCKED && !vlog_has_target(spec)) {
         vlog_set_target(spec, vlog_value(body));
         vlog_set_value(spec, vlog_param(body, 0));
      }
      else if (vlog_subkind(body) == V_SVA_DISABLE && !vlog_has_left(spec)) {
         vlog_set_left(spec, vlog_value(body));
         vlog_set_value(spec, vlog_param(body, 0));
      }
      else
         break;
   }

   if (!vlog_has_target(spec) && default_clock != NULL)
      vlog_set_target(spec, default_clock);

   if (!vlog_has_left(spec) && default_disable != NULL)
      vlog_set_left(spec, default_disable);
}

static void p_action_block(vlog_node_t parent)
{
   // statement_or_null | [ statement ] else statement_or_null

   BEGIN("action block");

   vlog_node_t then =  NULL;
   if (peek() != tELSE) {
      then = p_statement_or_null();
      if (then != NULL) {
         vlog_node_t c0 = vlog_new(V_COND);
         vlog_add_stmt(c0, then);

         vlog_add_cond(parent, c0);
      }
   }

   if (optional(tELSE)) {
      if (then == NULL) {
         vlog_node_t c0 = vlog_new(V_COND);
         vlog_add_cond(parent, c0);
      }

      vlog_node_t c1 = vlog_new(V_COND);
      vlog_add_cond(parent, c1);

      vlog_node_t stmt = p_statement_or_null();
      if (stmt != NULL)
         vlog_add_stmt(c1, stmt);
   }
}

static vlog_node_t p_concurrent_assertion_statement(ident_t label)
{
   // assert property ( property_spec ) action_block
   //   | assume property ( property_spec ) action_block
   //   | cover property ( property_spec ) statement_or_null
   //   | cover sequence ( [ clocking_event ] [ disable iff ( expression_or_dist ) ]
   //       sequence_expr ) statement_or_null
   //   | restrict property ( property_spec ) ;

   BEGIN("concurrent assertion statement");

   vlog_kind_t kind;
   bool is_restrict = false;
   switch (one_of(tASSERT, tASSUME, tCOVER, tRESTRICT)) {
   case tASSERT: kind = V_ASSERT; break;
   case tASSUME: kind = V_ASSUME; break;
   case tCOVER:  kind = V_COVER; break;
   default:      kind = V_ASSERT; is_restrict = true; break;
   }
   const bool cover_seq = kind == V_COVER && peek() == tSEQUENCE;

   if (cover_seq)
      consume(tSEQUENCE);
   else
      consume(tPROPERTY);

   consume(tLPAREN);

   vlog_node_t v = vlog_new(kind);
   vlog_set_ident(v, label);
   vlog_set_subkind(v, cover_seq ? V_ASSERT_COVER_SEQ : V_ASSERT_CONCURRENT);

   vlog_node_t spec = p_property_spec();
   sva_apply_defaults(spec);
   vlog_set_value(v, spec);

   consume(tRPAREN);

   if (is_restrict) {
      consume(tSEMI);
      return NULL;
   }

   if (kind == V_COVER) {
      vlog_node_t stmt = p_statement_or_null();
      if (stmt != NULL) {
         vlog_node_t c0 = vlog_new(V_COND);
         vlog_add_stmt(c0, stmt);
         vlog_add_cond(v, c0);
      }
   }
   else
      p_action_block(v);

   vlog_set_loc(v, CURRENT_LOC);
   return v;
}

typedef A(vlog_node_t) node_list_t;

static void collect_hier_refs_cb(vlog_node_t v, void *ctx)
{
   node_list_t *names = ctx;
   APUSH(*names, v);
}

static void sva_call_cb(vlog_node_t v, void *ctx)
{
   switch (vlog_kind(v)) {
   case V_USER_FCALL:
   case V_METHOD_CALL:
   case V_SYS_FCALL:
      *(bool *)ctx = true;
      break;
   default:
      break;
   }
}

static bool expr_has_call(vlog_node_t v)
{
   bool found = false;
   vlog_visit(v, sva_call_cb, &found);
   return found;
}

static void assertion_async_disable(vlog_node_t spec, vlog_node_t timing,
                                    unsigned count)
{
   // The condition of disable iff is not sampled: it aborts the attempts in
   // progress whenever it is true, not only when the clock ticks.  A
   // process records that it was true since the last tick.

   vlog_node_t cond = vlog_left(spec);
   if (!monitor_arg_sensitive(cond) || expr_has_call(cond))
      return;

   const loc_t *loc = vlog_loc(cond);

   vlog_node_t hit = vlog_new(V_VAR_DECL);
   vlog_set_ident(hit, ident_sprintf("$disable%u", count));
   vlog_set_type(hit, make_integer_atom_type(DT_INT));
   vlog_set_loc(hit, loc);
   vlog_add_decl(current_module, hit);

   vlog_node_t ev = vlog_new(V_EVENT);
   vlog_set_subkind(ev, V_EVENT_LEVEL);
   vlog_set_value(ev, cond);
   vlog_set_loc(ev, loc);

   vlog_node_t ctrl = vlog_new(V_EVENT_CONTROL);
   vlog_set_loc(ctrl, loc);
   vlog_add_param(ctrl, ev);

   vlog_node_t wait = vlog_new(V_TIMING);
   vlog_set_value(wait, ctrl);
   vlog_set_loc(wait, loc);
   vlog_add_stmt(wait, rs_if(cond, pca_set(hit, 1, loc), NULL, loc));

   vlog_node_t watch = vlog_new(V_ALWAYS);
   vlog_set_subkind(watch, V_ALWAYS_PLAIN);
   vlog_set_ident(watch, ident_sprintf("$disable_watch%u", count));
   vlog_set_loc(watch, loc);
   vlog_add_stmt(watch, wait);
   vlog_add_stmt(current_module, watch);

   vlog_node_t any = vlog_new(V_BINARY);
   vlog_set_subkind(any, V_BINARY_LOG_OR);
   vlog_set_left(any, cond);
   vlog_set_right(any, pca_ref(hit, loc));
   vlog_set_loc(any, loc);
   vlog_set_left(spec, any);

   vlog_add_stmt(timing, pca_set(hit, 0, loc));
}

static vlog_node_t p_assertion_process(vlog_node_t v)
{
   // A concurrent assertion in a module runs as its own process

   static unsigned count = 0;

   vlog_node_t spec = vlog_value(v);
   if (!vlog_has_target(spec)) {
      error_at(vlog_loc(v), "no clocking event specified for concurrent "
               "assertion and no default clocking in this scope");
      return NULL;
   }

   APUSH(module_asserts, v);

   vlog_node_t event = vlog_target(spec);

   if (current_module != NULL) {
      // The condition of disable iff has the values after the non-blocking
      // assignments of the time step in which the clock ticked.  Assertions
      // on the same clock share the process that detects the tick.
      vlog_node_t tick = NULL;
      shared_tick_t key = {};
      if (vlog_params(event) == 1) {
         vlog_node_t e = vlog_param(event, 0);
         if (vlog_kind(e) == V_EVENT && !vlog_has_left(e)
             && vlog_kind(vlog_value(e)) == V_REF
             && vlog_has_ident(vlog_value(e))) {
            key.scope = current_module;
            key.clock = vlog_ident(vlog_value(e));
            key.kind = vlog_subkind(e);

            for (unsigned i = 0; i < shared_ticks.count; i++) {
               const shared_tick_t *it = &(shared_ticks.items[i]);
               if (it->scope == key.scope && it->clock == key.clock
                   && it->kind == key.kind)
                  tick = it->tick;
            }
         }
      }

      if (tick == NULL) {
         tick = vlog_new(V_VAR_DECL);
         vlog_set_ident(tick, ident_sprintf("$tick%u", count));
         vlog_set_type(tick, logic_type());
         vlog_set_loc(tick, vlog_loc(v));

         vlog_node_t zero = vlog_new(V_NUMBER);
         vlog_set_number(zero, number_from_int(0));
         vlog_set_loc(zero, vlog_loc(v));
         vlog_set_value(tick, zero);
         vlog_add_decl(current_module, tick);

         vlog_node_t flip = vlog_new(V_UNARY);
         vlog_set_subkind(flip, V_UNARY_BITNEG);
         vlog_set_value(flip, defer_ref(tick, vlog_loc(v)));
         vlog_set_loc(flip, vlog_loc(v));

         vlog_node_t nba = vlog_new(V_NBASSIGN);
         vlog_set_target(nba, defer_ref(tick, vlog_loc(v)));
         vlog_set_value(nba, flip);
         vlog_set_loc(nba, vlog_loc(v));

         vlog_node_t sync_timing = vlog_new(V_TIMING);
         vlog_set_value(sync_timing, event);
         vlog_set_loc(sync_timing, vlog_loc(v));
         vlog_add_stmt(sync_timing, nba);

         vlog_node_t sync = vlog_new(V_ALWAYS);
         vlog_set_subkind(sync, V_ALWAYS_PLAIN);
         vlog_set_ident(sync, ident_sprintf("$tick_sync%u", count));
         vlog_set_loc(sync, vlog_loc(v));
         vlog_add_stmt(sync, sync_timing);
         vlog_add_stmt(current_module, sync);

         if (key.scope != NULL) {
            key.tick = tick;
            APUSH(shared_ticks, key);
         }
      }

      vlog_node_t ev = vlog_new(V_EVENT);
      vlog_set_subkind(ev, V_EVENT_LEVEL);
      vlog_set_value(ev, defer_ref(tick, vlog_loc(v)));
      vlog_set_loc(ev, vlog_loc(v));

      event = vlog_new(V_EVENT_CONTROL);
      vlog_set_loc(event, vlog_loc(v));
      vlog_add_param(event, ev);
   }

   vlog_node_t timing = vlog_new(V_TIMING);
   vlog_set_value(timing, event);
   vlog_set_loc(timing, vlog_loc(v));
   vlog_add_stmt(timing, v);

   if (current_module != NULL && vlog_has_left(spec))
      assertion_async_disable(spec, timing, count);

   vlog_node_t p = vlog_new(V_ALWAYS);
   vlog_set_subkind(p, V_ALWAYS_PLAIN);
   vlog_set_ident(p, vlog_has_ident(v) && vlog_ident(v) != NULL
                  ? vlog_ident(v) : ident_sprintf("$assert%u", count));
   count++;
   vlog_set_loc(p, vlog_loc(v));

   if (current_module != NULL) {
      // A hierarchical name cannot be copied by a variable of the module
      // so its value is captured when the clock ticks, before the
      // non-blocking assignments of the time step update it
      node_list_t names = AINIT;
      vlog_visit_only(vlog_value(spec), collect_hier_refs_cb, &names,
                      V_HIER_REF);

      if (names.count > 0) {
         vlog_node_t sample = vlog_new(V_SYS_TCALL);
         vlog_set_ident(sample, ident_new("$__sample"));
         vlog_set_subkind(sample, V_SYSTF_NONE);
         vlog_set_ival(sample, 0);
         vlog_set_loc(sample, vlog_loc(v));

         for (unsigned i = 0; i < names.count; i++)
            vlog_add_param(sample, names.items[i]);

         vlog_node_t capture = vlog_new(V_TIMING);
         vlog_set_value(capture, vlog_target(spec));
         vlog_set_loc(capture, vlog_loc(v));
         vlog_add_stmt(capture, sample);
         vlog_add_stmt(p, capture);
      }

      ACLEAR(names);
   }

   vlog_add_stmt(p, timing);
   return p;
}

static vlog_node_t p_assertion_item(void)
{
   // concurrent_assertion_item | deferred_immediate_assertion_item

   BEGIN("assertion item");

   ident_t label = NULL;
   if (optional(tID)) {
      label = state.last_lval.ident;
      consume(tCOLON);
   }

   if (peek_nth(2) == tPROPERTY || peek_nth(2) == tSEQUENCE) {
      vlog_node_t v = p_concurrent_assertion_statement(label);
      return v != NULL ? p_assertion_process(v) : NULL;
   }

   if (label != NULL)
      note_block_name(label);
   deferred_label = label;
   vlog_node_t v = p_deferred_immediate_assertion_statement();
   if (label != NULL)
      vlog_set_ident(deferred_inner != NULL ? deferred_inner : v, label);

   static unsigned count = 0;
   vlog_node_t p = vlog_new(V_ALWAYS);
   vlog_set_subkind(p, V_ALWAYS_COMB);
   vlog_set_ident(p, label != NULL ? label : ident_sprintf("$dassert%u", count++));
   vlog_set_loc(p, vlog_loc(v));
   vlog_add_stmt(p, v);
   return p;
}

static void p_sva_port(vlog_node_t decl)
{
   // [ local [ direction ] ] [ sequence | property | untyped | data_type ]
   //    formal_port_identifier [ = actual_arg ]

   BEGIN("sequence or property formal");

   vlog_node_t v = vlog_new(V_TF_PORT_DECL);
   vlog_set_subkind(v, V_PORT_INPUT);

   skip_over_attributes();

   if (optional(tVLOCAL)) {
      vlog_set_flags(v, VLOG_F_SVA_LOCAL);
      if (scan(tINPUT, tOUTPUT, tINOUT))
         vlog_set_subkind(v, p_tf_port_direction());
   }

   vlog_node_t dt;
   const int nports = vlog_ports(decl);
   if (nports > 0 && peek() == tID
       && (peek_nth(2) == tCOMMA || peek_nth(2) == tRPAREN
           || peek_nth(2) == tEQ)) {
      // The type, direction and locality are those of the previous formal
      vlog_node_t prev = vlog_port(decl, nports - 1);
      vlog_set_subkind(v, vlog_subkind(prev));
      vlog_set_flags(v, vlog_flags(prev));
      dt = vlog_type(prev);
   }
   else if (scan(tSEQUENCE, tPROPERTY, tUNTYPED)) {
      consume(peek());
      dt = p_implicit_data_type();
   }
   else if (peek() != tID || peek_nth(2) == tID || peek_nth(2) == tHASH
            || is_scoped_type_start())
      dt = p_data_type_or_implicit();
   else
      dt = p_implicit_data_type();

   vlog_set_ident(v, p_identifier());
   vlog_set_type(v, p_variable_dimensions(dt, v));

   if (optional(tEQ))
      vlog_set_value(v, p_sva_expr(1));

   vlog_set_loc(v, CURRENT_LOC);
   vlog_add_port(decl, v);
   vlog_symtab_put(symtab, v);
}

static void p_assertion_item_declaration(vlog_node_t parent)
{
   // property_declaration | sequence_declaration | let_declaration

   BEGIN("assertion item declaration");

   const bool is_prop = peek() == tPROPERTY;
   consume(is_prop ? tPROPERTY : tSEQUENCE);

   vlog_node_t v = vlog_new(V_SVA_DECL);
   vlog_set_subkind(v, is_prop ? V_SVA_PROPERTY : V_SVA_SEQUENCE);
   vlog_set_ident(v, p_identifier());
   vlog_set_loc(v, &state.last_loc);

   vlog_symtab_put(symtab, v);
   vlog_symtab_push(symtab, v);

   if (optional(tLPAREN)) {
      if (peek() != tRPAREN) {
         do {
            p_sva_port(v);
         } while (optional(tCOMMA));
      }
      consume(tRPAREN);
   }

   consume(tSEMI);

   if (!is_prop || peek() != tAT) {
      while (scan_block_item_declaration()) {
         const int first = vlog_decls(v);
         if (scan(tLOCALPARAM, tPARAMETER, tIMPORT, tTYPEDEF))
            p_block_item_declaration(v);
         else {
            vlog_node_t dt = p_data_type_or_implicit();
            p_list_of_variable_decl_assignments(v, dt, V_VAR_DECL);
            consume(tSEMI);
         }

         const int ndecls = vlog_decls(v);
         for (int i = first; i < ndecls; i++) {
            vlog_node_t d = vlog_decl(v, i);
            if (vlog_kind(d) == V_VAR_DECL)
               vlog_set_flags(d, vlog_flags(d) | VLOG_F_SVA_LOCAL);
         }
      }
   }

   vlog_node_t spec = p_property_spec();
   vlog_node_t body = vlog_value(spec);

   if (vlog_has_left(spec)) {
      vlog_node_t d = sva_new(V_SVA_DISABLE);
      vlog_set_value(d, vlog_left(spec));
      vlog_add_param(d, body);
      vlog_set_loc(d, vlog_loc(spec));
      body = d;
   }

   if (vlog_has_target(spec)) {
      vlog_node_t c = sva_unary(V_SVA_CLOCKED, body, vlog_loc(spec));
      vlog_set_value(c, vlog_target(spec));
      body = c;
   }

   vlog_set_value(v, body);

   optional(tSEMI);

   consume(is_prop ? tENDPROPERTY : tENDSEQUENCE);

   if (optional(tCOLON)) {
      ident_t name = p_identifier();
      if (name != vlog_ident(v))
         error_at(&state.last_loc, "'%s' does not match %s name '%s'",
                  istr(name), is_prop ? "property" : "sequence",
                  istr(vlog_ident(v)));
   }

   vlog_symtab_pop(symtab);

   vlog_set_loc(v, CURRENT_LOC);
   vlog_add_decl(parent, v);
}

typedef struct {
   ident_t      sig;
   bool         input, output;
   bool         in_step;
   vlog_node_t  in_skew, out_skew;
   int          in_edge, out_edge;   // Event kind or -1
   vlog_node_t  expr;
} clock_var_t;

static unsigned clocking_init_count = 0;

static int clocking_edge(void)
{
   // edge_identifier

   switch (peek()) {
   case tPOSEDGE:
      consume(tPOSEDGE);
      return V_EVENT_POSEDGE;
   case tNEGEDGE:
      consume(tNEGEDGE);
      return V_EVENT_NEGEDGE;
   case tEDGE:
      consume(tEDGE);
      return V_EVENT_LEVEL;
   default:
      return -1;
   }
}

static vlog_node_t clocking_skew(bool *step)
{
   // # delay_value | # 1step | edge identifier

   *step = false;

   if (peek() != tHASH)
      return NULL;

   if (peek_nth(2) == tUNSNUM && peek_nth(3) == tID
       && peek_ident_nth(3) == ident_new("step")) {
      consume(tHASH);
      consume(tUNSNUM);
      consume(tID);
      *step = true;
      return NULL;
   }

   return p_delay_control();
}

static vlog_node_t clocking_ref(vlog_node_t decl, const loc_t *loc)
{
   vlog_node_t r = vlog_new(V_REF);
   vlog_set_ident(r, vlog_ident(decl));
   vlog_set_ref(r, decl);
   vlog_set_loc(r, loc);
   return r;
}

static vlog_node_t clocking_var(vlog_node_t parent, ident_t name,
                                vlog_node_t type, vlog_node_t like,
                                const loc_t *loc)
{
   vlog_node_t v = vlog_new(V_VAR_DECL);
   vlog_set_ident(v, name);
   vlog_set_type(v, type);
   vlog_set_loc(v, loc);

   if (like != NULL && vlog_kind(like) != V_PORT_DECL) {
      const int nranges = vlog_ranges(like);
      for (int i = 0; i < nranges; i++)
         vlog_add_range(v, vlog_range(like, i));
   }

   vlog_add_decl(parent, v);
   vlog_symtab_put(symtab, v);
   return v;
}

static vlog_node_t clocking_always(vlog_node_t parent, const char *prefix,
                                   vlog_node_t body, vlog_node_t ctrl,
                                   const loc_t *loc)
{
   static unsigned count = 0;

   vlog_node_t p = vlog_new(V_ALWAYS);
   vlog_set_subkind(p, ctrl == NULL ? V_ALWAYS_COMB : V_ALWAYS_PLAIN);
   vlog_set_ident(p, ident_sprintf("%s%u", prefix, count++));
   vlog_set_loc(p, loc);

   if (ctrl == NULL)
      vlog_add_stmt(p, body);
   else {
      vlog_node_t t = vlog_new(V_TIMING);
      vlog_set_value(t, ctrl);
      vlog_set_loc(t, loc);
      vlog_add_stmt(t, body);
      vlog_add_stmt(p, t);
   }

   vlog_add_stmt(parent, p);
   return p;
}

static vlog_node_t clocking_assign(vlog_node_t target, vlog_node_t value,
                                   vlog_node_t delay, bool nba,
                                   const loc_t *loc)
{
   vlog_node_t a = vlog_new(nba ? V_NBASSIGN : V_BASSIGN);
   vlog_set_target(a, target);
   vlog_set_value(a, value);
   if (delay != NULL)
      vlog_set_delay(a, delay);
   vlog_set_loc(a, loc);
   return a;
}

static bool clocking_expr_cb(vlog_node_t v, void *ctx)
{
   return v == ctx;
}

static vlog_node_t clocking_sigref(const clock_var_t *cv, vlog_node_t sig,
                                   const loc_t *loc)
{
   if (cv->expr != NULL && vlog_kind(cv->expr) == V_REF)
      return clocking_ref(vlog_ref(cv->expr), loc);
   else if (cv->expr != NULL)
      return vlog_copy(cv->expr, clocking_expr_cb, cv->expr);

   return clocking_ref(sig, loc);
}

static vlog_node_t clocking_time_plus_one(const loc_t *loc)
{
   vlog_node_t now = vlog_new(V_SYS_FCALL);
   vlog_set_ident(now, ident_new("$time"));
   vlog_set_subkind(now, V_SYSTF_NONE);
   vlog_set_ival(now, time_ival());
   vlog_set_loc(now, loc);

   vlog_node_t one = vlog_new(V_NUMBER);
   vlog_set_number(one, number_from_int(1));
   vlog_set_loc(one, loc);

   vlog_node_t sum = vlog_new(V_BINARY);
   vlog_set_subkind(sum, V_BINARY_PLUS);
   vlog_set_left(sum, now);
   vlog_set_right(sum, one);
   vlog_set_loc(sum, loc);
   return sum;
}

static vlog_node_t clocking_flag(vlog_node_t decl, int value, const loc_t *loc)
{
   vlog_node_t n = vlog_new(V_NUMBER);
   vlog_set_number(n, number_from_int(value));
   vlog_set_loc(n, loc);
   return clocking_assign(clocking_ref(decl, loc), n, NULL, false, loc);
}

static void clocking_initial(vlog_node_t parent, vlog_node_t var,
                             const clock_var_t *cv, vlog_node_t sig,
                             const loc_t *loc)
{
   // Before the first sample the value is the one at the start of time

   vlog_node_t zero = vlog_new(V_NUMBER);
   vlog_set_number(zero, number_from_int(0));
   vlog_set_loc(zero, loc);

   vlog_node_t delay = vlog_new(V_DELAY_CONTROL);
   vlog_set_value(delay, zero);
   vlog_set_loc(delay, loc);

   vlog_node_t t = vlog_new(V_TIMING);
   vlog_set_value(t, delay);
   vlog_set_loc(t, loc);
   vlog_add_stmt(t, clocking_assign(clocking_ref(var, loc),
                                    clocking_sigref(cv, sig, loc), NULL, false,
                                    loc));

   vlog_node_t init = vlog_new(V_INITIAL);
   vlog_set_ident(init, ident_sprintf("$cbi%u", clocking_init_count++));
   vlog_set_loc(init, loc);
   vlog_add_stmt(init, t);
   vlog_add_stmt(parent, init);
}

static vlog_node_t clocking_edge_control(vlog_node_t ctrl, int kind,
                                         const loc_t *loc)
{
   // An event control for an edge of the signal of the clocking event

   if (vlog_params(ctrl) != 1 || vlog_kind(vlog_param(ctrl, 0)) != V_EVENT) {
      error_at(loc, "sorry, an edge skew requires a clocking event with a "
               "single signal");
      return NULL;
   }

   vlog_node_t clk = vlog_param(ctrl, 0);

   vlog_node_t ev = vlog_new(V_EVENT);
   vlog_set_subkind(ev, kind);
   vlog_set_value(ev, vlog_copy(vlog_value(clk), clocking_expr_cb,
                                vlog_value(clk)));
   vlog_set_loc(ev, loc);

   vlog_node_t c = vlog_new(V_EVENT_CONTROL);
   vlog_add_param(c, ev);
   vlog_set_loc(c, loc);
   return c;
}

static void clocking_unbound_cb(vlog_node_t v, void *ctx)
{
   if (!vlog_has_ref(v))
      *(bool *)ctx = true;
}

static bool clocking_unbound(vlog_node_t expr)
{
   // A hierarchical reference which has not been resolved has no width
   bool unbound = false;
   vlog_visit_only(expr, clocking_unbound_cb, &unbound, V_HIER_REF);
   return unbound;
}

static void clocking_synthesise(vlog_node_t parent, ident_t name,
                                vlog_node_t ctrl, const clock_var_t *vars,
                                int nvars, const loc_t *loc)
{
   // A clocking block becomes variables holding the sampled input values
   // and the values to drive, updated by processes sensitive to the clock

   vlog_node_t evtype = vlog_new(V_DATA_TYPE);
   vlog_set_subkind(evtype, DT_EVENT);
   vlog_set_loc(evtype, loc);

   vlog_node_t ev = clocking_var(parent, ident_sprintf("%s$ev", istr(name)),
                                 evtype, NULL, loc);

   vlog_node_t body = vlog_new(V_SEQ_BLOCK);
   vlog_set_loc(body, loc);

   // A drive made between clocking events takes effect at the next one
   vlog_node_t last = clocking_var(
      parent, ident_sprintf("%s$last", istr(name)),
      make_integer_atom_type(DT_TIME), NULL, loc);
   vlog_node_t captures = vlog_new(V_SEQ_BLOCK);
   vlog_set_loc(captures, loc);
   vlog_node_t samples = vlog_new(V_SEQ_BLOCK);
   vlog_set_loc(samples, loc);
   vlog_node_t drives = vlog_new(V_SEQ_BLOCK);
   vlog_set_loc(drives, loc);
   vlog_node_t publish = vlog_new(V_SEQ_BLOCK);
   vlog_set_loc(publish, loc);

   vlog_add_stmt(body, clocking_assign(clocking_ref(last, loc),
                                       clocking_time_plus_one(loc), NULL,
                                       false, loc));

   for (int i = 0; i < nvars; i++) {
      const clock_var_t *cv = &(vars[i]);

      vlog_node_t sig = cv->expr;
      vlog_node_t sigtype = sig != NULL ? vlog_get_type(sig) : NULL;
      if (sig != NULL && sigtype == NULL && clocking_unbound(sig)) {
         error_at(loc, "cannot determine the width of the expression for "
                  "clocking signal %pi", cv->sig);
         continue;
      }
      if (sig != NULL && sigtype == NULL) {
         // A bit or part select or concatenation is a vector of its width
         const int width = vlog_width(sig);
         sigtype = logic_type();
         if (width > 1) {
            vlog_node_t left = foreach_number(width - 1, loc);
            vlog_node_t right = foreach_number(0, loc);
            vlog_node_t dim = vlog_new(V_DIMENSION);
            vlog_set_subkind(dim, V_DIM_PACKED);
            vlog_set_left(dim, left);
            vlog_set_right(dim, right);
            vlog_set_loc(dim, loc);
            vlog_add_range(sigtype, dim);
         }
         vlog_set_loc(sigtype, loc);
      }
      if (sig == NULL) {
         sig = vlog_symtab_query(symtab, cv->sig);
         if (sig != NULL && vlog_kind(sig) == V_MOD_INST)
            sig = NULL;   // Type name not declared so parsed as instance
         else if (sig != NULL && vlog_has_type(sig))
            sigtype = vlog_type(sig);
      }

      if (sig == NULL || sigtype == NULL) {
         error_at(loc, "clocking signal %pi is not declared in this scope",
                  cv->sig);
         continue;
      }

      vlog_node_t shadow = clocking_var(
         parent, ident_sprintf("%s$%s", istr(name), istr(cv->sig)),
         sigtype, cv->expr ? NULL : sig, loc);

      if (cv->input) {
         vlog_node_t src = sig;
         if (cv->in_skew != NULL) {
            // The value a number of time units before the clock
            vlog_node_t d = clocking_var(
               parent, ident_sprintf("%s$%s$d", istr(name), istr(cv->sig)),
               sigtype, cv->expr ? NULL : sig, loc);

            vlog_node_t ev1 = vlog_new(V_EVENT);
            vlog_set_subkind(ev1, V_EVENT_LEVEL);
            vlog_set_value(ev1, clocking_sigref(cv, sig, loc));
            vlog_set_loc(ev1, loc);
            vlog_node_t c1 = vlog_new(V_EVENT_CONTROL);
            vlog_add_param(c1, ev1);
            vlog_set_loc(c1, loc);

            clocking_always(parent, "$cbd", clocking_assign(
                               clocking_ref(d, loc), clocking_sigref(cv, sig, loc),
                               vlog_copy(cv->in_skew, vlog_always_true_cb,
                                         NULL), true, loc), c1, loc);
            clocking_initial(parent, d, cv, sig, loc);
            src = d;
         }
         else if (cv->in_step || cv->in_edge >= 0) {
            // The Preponed value is the one at the end of the last step
            vlog_node_t pp = clocking_var(
               parent, ident_sprintf("%s$%s$pp", istr(name), istr(cv->sig)),
               sigtype, cv->expr ? NULL : sig, loc);

            clocking_always(parent, "$postponed_a", clocking_assign(
                               clocking_ref(pp, loc), clocking_sigref(cv, sig, loc),
                               NULL, false, loc), NULL, loc);
            src = pp;
         }

         if (cv->in_edge >= 0) {
            // Sampled at the most recent edge of the clock
            vlog_node_t edge = clocking_edge_control(ctrl, cv->in_edge, loc);
            if (edge != NULL) {
               vlog_node_t r = clocking_var(
                  parent, ident_sprintf("%s$%s$e", istr(name), istr(cv->sig)),
                  sigtype, cv->expr ? NULL : sig, loc);

               clocking_always(parent, "$cbe", clocking_assign(
                                  clocking_ref(r, loc), clocking_ref(src, loc),
                                  NULL, false, loc), edge, loc);
               clocking_initial(parent, r, cv, sig, loc);
               src = r;
            }
         }

         if (cv->output)
            vlog_add_stmt(samples, clocking_assign(
                             clocking_ref(shadow, loc), clocking_ref(src, loc),
                             NULL, false, loc));
         else {
            // The sampled value is visible to the processes waiting on the
            // clockvar only after those waiting for the clocking event
            vlog_node_t taken = clocking_var(
               parent, ident_sprintf("%s$%s$s", istr(name), istr(cv->sig)),
               sigtype, NULL, loc);
            vlog_add_stmt(samples, clocking_assign(
                             clocking_ref(taken, loc), clocking_ref(src, loc),
                             NULL, false, loc));
            vlog_add_stmt(publish, clocking_assign(
                             clocking_ref(shadow, loc),
                             clocking_ref(taken, loc), NULL, false, loc));
         }
      }

      if (cv->output) {
         vlog_node_t ev1 = vlog_new(V_EVENT);
         vlog_set_subkind(ev1, V_EVENT_LEVEL);
         vlog_set_value(ev1, clocking_ref(shadow, loc));
         vlog_set_loc(ev1, loc);
         vlog_node_t c1 = vlog_new(V_EVENT_CONTROL);
         vlog_add_param(c1, ev1);
         vlog_set_loc(c1, loc);

         if (cv->out_edge >= 0) {
            vlog_node_t delay = cv->out_skew != NULL
               ? vlog_copy(cv->out_skew, vlog_always_true_cb, NULL) : NULL;
            vlog_node_t drive = clocking_assign(
               clocking_sigref(cv, sig, loc), clocking_ref(shadow, loc), delay,
               true, loc);

            vlog_node_t edge = clocking_edge_control(ctrl, cv->out_edge, loc);
            if (edge != NULL) {
               // Driven at the next edge of the clock after the drive
               vlog_node_t t = vlog_new(V_TIMING);
               vlog_set_value(t, edge);
               vlog_set_loc(t, loc);
               vlog_add_stmt(t, drive);
               drive = t;
            }

            clocking_always(parent, "$cbo", drive, c1, loc);
            continue;
         }

         vlog_node_t pend = clocking_var(
            parent, ident_sprintf("%s$%s$p", istr(name), istr(cv->sig)),
            bit_type(), NULL, loc);

         vlog_node_t test = vlog_new(V_BINARY);
         vlog_set_subkind(test, V_BINARY_LOG_EQ);
         vlog_set_left(test, clocking_ref(last, loc));
         vlog_set_right(test, clocking_time_plus_one(loc));
         vlog_set_loc(test, loc);

         vlog_node_t zero = vlog_new(V_NUMBER);
         vlog_set_number(zero, number_from_int(0));
         vlog_set_loc(zero, loc);

         vlog_node_t idle = vlog_new(V_BINARY);
         vlog_set_subkind(idle, V_BINARY_LOG_EQ);
         vlog_set_left(idle, clocking_ref(pend, loc));
         vlog_set_right(idle, zero);
         vlog_set_loc(idle, loc);

         vlog_node_t both = vlog_new(V_BINARY);
         vlog_set_subkind(both, V_BINARY_LOG_AND);
         vlog_set_left(both, test);
         vlog_set_right(both, idle);
         vlog_set_loc(both, loc);

         vlog_node_t now = vlog_new(V_COND);
         vlog_set_value(now, both);
         vlog_set_loc(now, loc);
         vlog_add_stmt(now, clocking_assign(
                          clocking_sigref(cv, sig, loc),
                          clocking_ref(shadow, loc),
                          cv->out_skew ? vlog_copy(cv->out_skew,
                                                   vlog_always_true_cb, NULL)
                          : NULL, true, loc));

         vlog_node_t later = vlog_new(V_COND);
         vlog_set_loc(later, loc);
         vlog_add_stmt(later, clocking_flag(pend, 1, loc));

         vlog_node_t sel = vlog_new(V_IF);
         vlog_add_cond(sel, now);
         vlog_add_cond(sel, later);
         vlog_set_loc(sel, loc);

         clocking_always(parent, "$cbo", sel, c1, loc);

         // The value is taken before the sample of an inout overwrites it
         vlog_node_t held = clocking_var(
            parent, ident_sprintf("%s$%s$h", istr(name), istr(cv->sig)),
            sigtype, cv->expr ? NULL : sig, loc);

         vlog_node_t take = vlog_new(V_COND);
         vlog_set_value(take, clocking_ref(pend, loc));
         vlog_set_loc(take, loc);
         vlog_add_stmt(take, clocking_assign(clocking_ref(held, loc),
                                             clocking_ref(shadow, loc), NULL,
                                             false, loc));

         vlog_node_t grab = vlog_new(V_IF);
         vlog_add_cond(grab, take);
         vlog_set_loc(grab, loc);
         vlog_add_stmt(captures, grab);

         vlog_node_t due = vlog_new(V_COND);
         vlog_set_value(due, clocking_ref(pend, loc));
         vlog_set_loc(due, loc);
         vlog_add_stmt(due, clocking_flag(pend, 0, loc));
         vlog_add_stmt(due, clocking_assign(
                          clocking_sigref(cv, sig, loc),
                          clocking_ref(held, loc),
                          cv->out_skew ? vlog_copy(cv->out_skew,
                                                   vlog_always_true_cb, NULL)
                          : NULL, true, loc));

         vlog_node_t chk = vlog_new(V_IF);
         vlog_add_cond(chk, due);
         vlog_set_loc(chk, loc);
         vlog_add_stmt(drives, chk);
      }
   }

   // Processes waiting for the clocking event run after the non-blocking
   // assignments of the time step
   vlog_node_t marker = vlog_new(V_SYS_TCALL);
   vlog_set_ident(marker, ident_new("$__reactive"));
   vlog_set_subkind(marker, V_SYSTF_NONE);
   vlog_set_ival(marker, 0);
   vlog_set_loc(marker, loc);
   const int ncaptures = vlog_stmts(captures);
   for (int i = 0; i < ncaptures; i++)
      vlog_add_stmt(body, vlog_stmt(captures, i));

   const int nsamples = vlog_stmts(samples);
   for (int i = 0; i < nsamples; i++)
      vlog_add_stmt(body, vlog_stmt(samples, i));

   vlog_add_stmt(body, marker);

   const int ndrives = vlog_stmts(drives);
   for (int i = 0; i < ndrives; i++)
      vlog_add_stmt(body, vlog_stmt(drives, i));

   vlog_node_t trig = vlog_new(V_EVENT_TRIGGER);
   vlog_set_ident(trig, vlog_ident(ev));
   vlog_set_value(trig, clocking_ref(ev, loc));
   vlog_set_loc(trig, loc);
   vlog_add_stmt(body, trig);

   const int npublish = vlog_stmts(publish);
   for (int i = 0; i < npublish; i++)
      vlog_add_stmt(body, vlog_stmt(publish, i));

   clocking_always(parent, "$cbs", body, ctrl, loc);

   vlog_node_t evc = vlog_new(V_EVENT_CONTROL);
   vlog_node_t ev2 = vlog_new(V_EVENT);
   vlog_set_subkind(ev2, V_EVENT_LEVEL);
   vlog_set_value(ev2, clocking_ref(ev, loc));
   vlog_set_loc(ev2, loc);
   vlog_add_param(evc, ev2);
   vlog_set_loc(evc, loc);

   if (clocking_blocks == NULL)
      clocking_blocks = hash_new(16);
   hash_put(clocking_blocks, ident_sprintf("%s$cycle", istr(name)), evc);
}

static void p_clocking_item(vlog_node_t parent, clock_var_t **vars, int *nvars,
                            bool *def_in_step, vlog_node_t *def_in,
                            vlog_node_t *def_out)
{
   // default default_skew ; | clocking_direction list_of_clocking_decl_assign ;

   BEGIN("clocking item");

   if (optional(tDEFAULT)) {
      for (;;) {
         if (optional(tINPUT))
            *def_in = clocking_skew(def_in_step);
         else if (optional(tOUTPUT)) {
            bool step;
            *def_out = clocking_skew(&step);
         }
         else
            break;
      }

      consume(tSEMI);
      return;
   }

   const token_t dir = one_of(tINPUT, tOUTPUT, tINOUT);

   bool in_step = *def_in_step, also_out = false;
   vlog_node_t in_skew = *def_in, out_skew = *def_out;
   int in_edge = -1, out_edge = -1;

   if (dir != tOUTPUT) {
      in_edge = clocking_edge();
      if (in_edge >= 0) {
         in_skew = NULL;
         in_step = false;
      }

      if (peek() == tHASH)
         in_skew = clocking_skew(&in_step);
   }
   else {
      out_edge = clocking_edge();
      if (out_edge >= 0)
         out_skew = NULL;

      if (peek() == tHASH) {
         bool step;
         out_skew = clocking_skew(&step);
      }
   }

   if ((dir == tINOUT || dir == tINPUT) && peek() == tOUTPUT) {
      consume(tOUTPUT);
      out_edge = clocking_edge();
      if (out_edge >= 0)
         out_skew = NULL;

      bool step;
      vlog_node_t skew = clocking_skew(&step);
      if (skew != NULL || out_edge < 0)
         out_skew = skew;
      also_out = true;
   }

   do {
      clock_var_t cv = {
         .sig      = p_identifier(),
         .input    = dir != tOUTPUT,
         .output   = dir != tINPUT || also_out,
         .in_step  = in_step,
         .in_skew  = in_skew,
         .out_skew = out_skew,
         .in_edge  = in_edge,
         .out_edge = out_edge,
      };

      if (optional(tEQ))
         cv.expr = p_expression();

      *vars = xrealloc_array(*vars, *nvars + 1, sizeof(clock_var_t));
      (*vars)[(*nvars)++] = cv;
   } while (optional(tCOMMA));

   consume(tSEMI);
}

static void p_clocking_declaration(vlog_node_t parent, bool is_default)
{
   // [ default ] clocking [ clocking_identifier ] clocking_event ;
   //   { clocking_item } endclocking [ : clocking_identifier ]

   BEGIN("clocking declaration");

   consume(tCLOCKING);

   ident_t name = NULL;
   vlog_node_t ctrl = NULL;
   if (peek() == tID && peek_nth(2) != tAT) {
      // default clocking name ;
      name = p_identifier();
      if (is_default && peek() == tSEMI) {
         consume(tSEMI);
         ctrl = clocking_blocks != NULL ? hash_get(clocking_blocks, name) : NULL;
         if (ctrl == NULL)
            parse_error(&state.last_loc, "sorry, only clocking blocks "
                        "declared in the same unit are supported");
         else {
            default_clock = ctrl;
            default_cycle = hash_get(clocking_blocks,
                                     ident_sprintf("%s$cycle", istr(name)));
         }
         return;
      }
   }
   else if (peek() == tID)
      name = p_identifier();

   const loc_t loc = *CURRENT_LOC;

   ctrl = p_clocking_event();
   last_clocking_ctrl = ctrl;
   consume(tSEMI);

   clock_var_t *vars LOCAL = NULL;
   int nvars = 0;
   bool def_in_step = true;
   vlog_node_t def_in = NULL, def_out = NULL;

   while (not_at_token(tENDCLOCKING)) {
      if (scan(tDEFAULT, tINPUT, tOUTPUT, tINOUT))
         p_clocking_item(parent, &vars, &nvars, &def_in_step, &def_in,
                         &def_out);
      else if (scan(tPROPERTY, tSEQUENCE))
         p_assertion_item_declaration(parent);
      else {
         parse_error(&state.last_loc, "sorry, this item is not supported in "
                     "a clocking block");
         drop_tokens_until(&state, tSEMI);   // Also drops the semicolon
      }
   }

   consume(tENDCLOCKING);

   if (optional(tCOLON))
      (void)p_identifier();

   if (name != NULL) {
      if (clocking_blocks == NULL)
         clocking_blocks = hash_new(16);
      hash_put(clocking_blocks, name, ctrl);

      if (clocking_names == NULL)
         clocking_names = hset_new(16);
      hset_insert(clocking_names, name);

      clocking_synthesise(parent, name, ctrl, vars, nvars, &loc);
   }

   if (is_default) {
      default_clock = ctrl;
      default_cycle = name != NULL
         ? hash_get(clocking_blocks, ident_sprintf("%s$cycle", istr(name)))
         : NULL;
   }
}


static ident_t global_clock_id(void)
{
   return ident_new("$global_clock$v");
}

static bool global_clock_copy_pred(vlog_node_t v, void *ctx)
{
   return vlog_kind(v) == V_EVENT_CONTROL || vlog_kind(v) == V_EVENT;
}

static void global_clocking_synthesise(vlog_node_t parent, vlog_node_t ctrl,
                                       const loc_t *loc)
{
   // The global clock is a variable which toggles at every global clocking
   // event so that $global_clock can be found by name from an enclosing
   // instance (14.14)

   if (ctrl == NULL)
      return;

   vlog_node_t var = clocking_var(parent, global_clock_id(), bit_type(), NULL,
                                  loc);

   vlog_node_t flip = vlog_new(V_UNARY);
   vlog_set_subkind(flip, V_UNARY_NOT);
   vlog_set_value(flip, clocking_ref(var, loc));
   vlog_set_loc(flip, loc);

   clocking_always(parent, "$global_clock", clocking_assign(
                      clocking_ref(var, loc), flip, NULL, false, loc),
                   vlog_copy(ctrl, global_clock_copy_pred, NULL), loc);
}

static vlog_node_t global_clock_ref(const loc_t *loc)
{
   // Found by name in this instance or the closest enclosing one when the
   // design is elaborated

   vlog_node_t mr = vlog_new(V_MOD_REF);
   vlog_set_ident(mr, ident_new("$global_clock$scope"));
   vlog_set_loc(mr, loc);

   vlog_node_t v = vlog_new(V_HIER_REF);
   vlog_set_value(v, mr);
   vlog_set_ident(v, global_clock_id());
   vlog_set_loc(v, loc);
   vlog_set_global_flags(v, VLOG_GF_HIER_REF);
   return v;
}

static vlog_node_t global_clock_control(const loc_t *loc)
{
   vlog_node_t ev = vlog_new(V_EVENT);
   vlog_set_subkind(ev, V_EVENT_LEVEL);
   vlog_set_value(ev, global_clock_ref(loc));
   vlog_set_loc(ev, loc);

   vlog_node_t c = vlog_new(V_EVENT_CONTROL);
   vlog_add_param(c, ev);
   vlog_set_loc(c, loc);
   return c;
}

static void p_default_declaration(vlog_node_t parent)
{
   // default clocking ... | default disable iff expression_or_dist ;

   BEGIN("default declaration");

   consume(tDEFAULT);

   if (peek() == tCLOCKING)
      p_clocking_declaration(parent, true);
   else {
      consume(tDISABLE);
      consume(tIFFKW);
      default_disable = p_expression_or_dist();
      consume(tSEMI);
   }
}

static void p_module_common_item(vlog_node_t mod)
{
   // module_or_generate_item_declaration
   //   | interface_instantiation | program_instantiation
   //   | assertion_item | bind_directive | continuous_assign
   //   | net_alias | initial_construct | final_construct
   //   | always_construct | loop_generate_construct
   //   | conditional_generate_construct | elaboration_system_task

   BEGIN("module common item");

   switch (peek()) {
   case tALWAYS:
   case tALWAYSCOMB:
   case tALWAYSFF:
   case tALWAYSLATCH:
      vlog_add_stmt(mod, p_always_construct());
      break;
   case tINITIAL:
      vlog_add_stmt(mod, p_initial_construct());
      break;
   case tWIRE:
   case tUWIRE:
   case tSUPPLY0:
   case tSUPPLY1:
   case tTRI:
   case tTRI0:
   case tTRI1:
   case tTRIAND:
   case tTRIOR:
   case tTRIREG:
   case tWAND:
   case tWOR:
   case tINTERCONNECT:
   case tREG:
   case tSTRUCT:
   case tUNION:
   case tTYPEDEF:
   case tNETTYPE:
   case tENUM:
   case tSVINT:
   case tINTEGER:
   case tSVREAL:
   case tSHORTREAL:
   case tREALTIME:
   case tTIME:
   case tTYPE:
   case tCLASS:
   case tINTERFACE:
   case tCONSTRAINT:
   case tVIRTUAL:
   case tSTATIC:
   case tAUTOMATIC:
   case tCONST:
   case tTASK:
   case tFUNCTION:
   case tLOCALPARAM:
   case tPARAMETER:
   case tEVENT:
   case tGENVAR:
   case tVAR:
   case tLOGIC:
   case tBIT:
   case tSHORTINT:
   case tLONGINT:
   case tBYTE:
   case tSTRINGK: case tCHANDLE:
   case tIMPORT:
   case tSEQUENCE:
   case tPROPERTY:
   case tCLOCKING:
   case tDEFAULT:
      p_module_or_generate_item_declaration(mod);
      break;
   case tASSIGN:
      p_continuous_assign(mod);
      break;
   case tFOR:
      p_loop_generate_construct(mod);
      break;
   case tIF:
   case tCASE:
      vlog_add_stmt(mod, p_conditional_generate_construct());
      break;
   case tFINAL:
      vlog_add_stmt(mod, p_final_construct());
      break;
   case tASSERT:
   case tASSUME:
   case tCOVER:
      {
         vlog_node_t a = p_assertion_item();
         if (a != NULL)
            vlog_add_stmt(mod, a);
      }
      break;
   case tID:
      {
         bool is_assert = false;
         if (peek_nth(2) == tCOLON) {
            const token_t tok3 = peek_nth(3);
            is_assert = tok3 == tASSERT || tok3 == tASSUME || tok3 == tCOVER;
         }

         if (is_assert)
            {
         vlog_node_t a = p_assertion_item();
         if (a != NULL)
            vlog_add_stmt(mod, a);
      }
         else
            p_module_or_generate_item_declaration(mod);
      }
      break;
   default:
      one_of(tALWAYS, tALWAYSCOMB, tALWAYSFF, tALWAYSLATCH, tWIRE, tUWIRE,
             tSUPPLY0, tSUPPLY1, tTRI, tTRI0, tTRI1, tTRIAND, tTRIOR, tTRIREG,
             tWAND, tWOR, tINTERCONNECT, tREG, tSTRUCT, tUNION, tTYPEDEF, tENUM,
             tSVINT, tINTEGER, tSVREAL, tSHORTREAL, tREALTIME, tTIME, tTASK,
             tFUNCTION, tPARAMETER, tLOCALPARAM, tEVENT, tID, tGENVAR, tVAR,
             tLOGIC, tBIT, tSHORTINT, tLONGINT, tBYTE, tSTRINGK, tIMPORT,
             tASSIGN, tFOR, tIF, tCASE, tFINAL, tASSERT, tASSUME, tCOVER);
      drop_tokens_until(&state, tSEMI);
   }
}

static vlog_strength_t p_strength0(void)
{
   // supply0 | strong0 | pull0 | weak0

   BEGIN("strength0");

   switch (one_of(tSUPPLY0, tSTRONG0, tPULL0, tWEAK0)) {
   default:
   case tSUPPLY0: return V_STRENGTH_SUPPLY;
   case tSTRONG0: return V_STRENGTH_STRONG;
   case tPULL0:   return V_STRENGTH_PULL;
   case tWEAK0:   return V_STRENGTH_WEAK;
   }
}

static vlog_strength_t p_strength1(void)
{
   // supply1 | strong1 | pull1 | weak1

   BEGIN("strength1");

   switch (one_of(tSUPPLY1, tSTRONG1, tPULL1, tWEAK1)) {
   default:
   case tSUPPLY1: return V_STRENGTH_SUPPLY;
   case tSTRONG1: return V_STRENGTH_STRONG;
   case tPULL1:   return V_STRENGTH_PULL;
   case tWEAK1:   return V_STRENGTH_WEAK;
   }
}

static vlog_node_t p_pulldown_strength(void)
{
   // ( strength0 , strength1 ) | ( strength1 , strength0 ) | ( strength0 )

   BEGIN("pulldown strength");

   consume(tLPAREN);

   vlog_strength_t s0, s1;
   switch (peek()) {
   case tSUPPLY1:
   case tSTRONG1:
   case tPULL1:
   case tWEAK1:
      s1 = p_strength1();
      consume(tCOMMA);
      s0 = p_strength0();
      break;
   default:
      s0 = s1 = p_strength0();
      if (optional(tCOMMA))
         s1 = p_strength1();
      break;
   }

   consume(tRPAREN);

   vlog_node_t v = vlog_new(V_STRENGTH);
   vlog_set_subkind(v, MAKE_STRENGTH(s0, s1));
   vlog_set_loc(v, CURRENT_LOC);
   return v;
}

static vlog_node_t p_pullup_strength(void)
{
   // ( strength0 , strength1 ) | ( strength1 , strength0 ) | ( strength1 )

   BEGIN("pullup strength");

   consume(tLPAREN);

   vlog_strength_t s0, s1;
   switch (peek()) {
   case tSUPPLY0:
   case tSTRONG0:
   case tPULL0:
   case tWEAK0:
      s0 = p_strength0();
      consume(tCOMMA);
      s1 = p_strength1();
      break;
   default:
      s1 = s0 = p_strength1();
      if (optional(tCOMMA))
         s0 = p_strength0();
      break;
   }

   consume(tRPAREN);

   vlog_node_t v = vlog_new(V_STRENGTH);
   vlog_set_subkind(v, MAKE_STRENGTH(s0, s1));
   vlog_set_loc(v, CURRENT_LOC);
   return v;
}

static void p_gate_instance_name(vlog_node_t v)
{
   // [ name_of_instance ]
   //   name_of_instance ::= identifier [ unpacked_dimension ]

   if (peek() == tID) {
      vlog_set_ident(v, p_identifier());
      vlog_set_loc(v, &state.last_loc);
      vlog_symtab_put(symtab, v);

      // The range of an array of instances is kept among the parameters
      if (peek() == tLSQUARE)
         vlog_add_param(v, p_unpacked_dimension());
   }
   else
      vlog_set_ident(v, default_label("gate"));
}

static vlog_node_t p_pull_gate_instance(vlog_gate_kind_t kind, vlog_node_t st)
{
   // [ name_of_instance ] ( output_terminal )

   BEGIN("pull gate instance");

   vlog_node_t v = vlog_new(V_GATE_INST);
   vlog_set_subkind(v, kind);
   vlog_add_param(v, st);

   p_gate_instance_name(v);

   consume(tLPAREN);

   vlog_symtab_set_implicit(symtab, implicit_kind);

   vlog_set_target(v, p_net_lvalue());

   vlog_symtab_set_implicit(symtab, V_NET_NONE);

   consume(tRPAREN);

   vlog_set_loc(v, CURRENT_LOC);
   return v;
}

static vlog_node_t p_n_terminal_gate_instance(vlog_gate_kind_t kind,
                                              vlog_node_t st)
{
   // [ name_of_instance ] ( output_terminal , input_terminal
   //     { , input_terminal } )

   BEGIN("N-terminal gate instance");

   vlog_node_t v = vlog_new(V_GATE_INST);
   vlog_set_subkind(v, kind);

   p_gate_instance_name(v);

   consume(tLPAREN);

   vlog_symtab_set_implicit(symtab, implicit_kind);

   vlog_set_target(v, p_net_lvalue());

   consume(tCOMMA);

   do {
      vlog_add_param(v, p_expression());
   } while (optional(tCOMMA));

   vlog_symtab_set_implicit(symtab, V_NET_NONE);

   consume(tRPAREN);

   vlog_set_loc(v, CURRENT_LOC);
   return v;
}

static vlog_node_t p_enable_gate_instance(vlog_gate_kind_t kind,
                                          vlog_node_t st)
{
   // [ name_of_instance ] ( output_terminal , input_terminal ,
   //     enable_terminal )

   BEGIN("enable gate instance");

   vlog_node_t v = vlog_new(V_GATE_INST);
   vlog_set_subkind(v, kind);
   vlog_add_param(v, st);

   p_gate_instance_name(v);

   consume(tLPAREN);

   vlog_symtab_set_implicit(symtab, implicit_kind);

   vlog_set_target(v, p_net_lvalue());

   consume(tCOMMA);

   vlog_add_param(v, p_expression());

   consume(tCOMMA);

   vlog_add_param(v, p_expression());

   vlog_symtab_set_implicit(symtab, V_NET_NONE);

   consume(tRPAREN);

   vlog_set_loc(v, CURRENT_LOC);
   return v;
}

static vlog_node_t p_pass_switch_instance(vlog_gate_kind_t kind)
{
   // [ name_of_instance ] ( inout_terminal , inout_terminal )

   BEGIN("pass switch instance");

   vlog_node_t v = vlog_new(V_GATE_INST);
   vlog_set_subkind(v, kind);

   p_gate_instance_name(v);

   consume(tLPAREN);

   vlog_symtab_set_implicit(symtab, implicit_kind);

   vlog_set_target(v, p_net_lvalue());

   consume(tCOMMA);

   vlog_add_param(v, p_net_lvalue());

   vlog_symtab_set_implicit(symtab, V_NET_NONE);

   consume(tRPAREN);

   vlog_set_loc(v, CURRENT_LOC);
   return v;
}

static vlog_node_t p_pass_enable_switch_instance(vlog_gate_kind_t kind)
{
   // [ name_of_instance ] ( inout_terminal , inout_terminal ,
   //     enable_terminal )

   BEGIN("pass enable switch instance");

   vlog_node_t v = vlog_new(V_GATE_INST);
   vlog_set_subkind(v, kind);

   p_gate_instance_name(v);

   consume(tLPAREN);

   vlog_symtab_set_implicit(symtab, implicit_kind);

   vlog_set_target(v, p_net_lvalue());

   consume(tCOMMA);

   vlog_add_param(v, p_net_lvalue());

   consume(tCOMMA);

   vlog_add_param(v, p_expression());

   vlog_symtab_set_implicit(symtab, V_NET_NONE);

   consume(tRPAREN);

   vlog_set_loc(v, CURRENT_LOC);
   return v;
}

static void p_gate_instantiation(vlog_node_t mod)
{
   // cmos_switchtype [ delay3 ] cmos_switch_instance
   //     { , cmos_switch_instance } ;
   //  | enable_gatetype [ drive_strength ] [ delay3 ]
   //     enable_gate_instance { , enable_gate_instance } ;
   //  | mos_switchtype [ delay3 ] mos_switch_instance
   //     { , mos_switch_instance } ;
   //  | n_input_gatetype [ drive_strength ] [ delay2 ] n_input_gate_instance
   //     { , n_input_gate_instance } ;
   //  | n_output_gatetype [ drive_strength ] [ delay2 ] n_output_gate_instance
   //     { , n_output_gate_instance } ;
   //  | pass_en_switchtype [ delay2 ] pass_enable_switch_instance
   //     { , pass_enable_switch_instance } ;
   //  | pass_switchtype pass_switch_instance { , pass_switch_instance } ;
   //  | pulldown [ pulldown_strength ] pull_gate_instance
   //     { , pull_gate_instance } ;
   //  | pullup [ pullup_strength ] pull_gate_instance
   //     { , pull_gate_instance } ;

   BEGIN("gate instantiation");

   token_t token = one_of(tPULLDOWN, tPULLUP, tAND, tNAND, tOR, tNOR,
                          tXOR, tXNOR, tNOT, tBUF, tBUFIF0, tBUFIF1,
                          tNOTIF0, tNOTIF1, tTRAN, tRTRAN, tTRANIF0, tTRANIF1,
                          tRTRANIF0, tRTRANIF1);

   switch (token) {
   case tPULLDOWN:
      {
         vlog_node_t st;
         if (peek() == tLPAREN && peek_nth(2) != tID)
            st = p_pulldown_strength();
         else {
            st = vlog_new(V_STRENGTH);
            vlog_set_subkind(st, ST_PULLUP);
         }

         do {
            vlog_node_t g = p_pull_gate_instance(V_GATE_PULLDOWN, st);
            vlog_add_stmt(mod, g);
         } while (optional(tCOMMA));
      }
      break;

   case tPULLUP:
      {
         vlog_node_t st;
         if (peek() == tLPAREN && peek_nth(2) != tID)
            st = p_pullup_strength();
         else {
            st = vlog_new(V_STRENGTH);
            vlog_set_subkind(st, ST_PULLUP);
         }

         do {
            vlog_node_t g = p_pull_gate_instance(V_GATE_PULLUP, st);
            vlog_add_stmt(mod, g);
         } while (optional(tCOMMA));
      }
      break;

   case tAND:
   case tNAND:
   case tOR:
   case tNOR:
   case tXOR:
   case tXNOR:
   case tNOT:
   case tBUF:
      {
         vlog_node_t st;
         if (peek() == tLPAREN && peek_nth(2) != tID)
            st = p_drive_strength();
         else {
            st = vlog_new(V_STRENGTH);
            vlog_set_subkind(st, ST_STRONG);
         }

         const vlog_gate_kind_t kind = get_gate_kind(token);

         vlog_node_t delay = NULL;
         if (peek() == tHASH)
            delay = p_delay2();

         do {
            vlog_node_t g = p_n_terminal_gate_instance(kind, st);
            if (delay != NULL)
               vlog_set_delay(g, delay);
            vlog_add_stmt(mod, g);
         } while (optional(tCOMMA));
      }
      break;

   case tBUFIF0:
   case tBUFIF1:
   case tNOTIF0:
   case tNOTIF1:
      {
         vlog_node_t st;
         if (peek() == tLPAREN && peek_nth(2) != tID)
            st = p_drive_strength();
         else {
            st = vlog_new(V_STRENGTH);
            vlog_set_subkind(st, ST_STRONG);
         }

         vlog_node_t delay = NULL;
         if (peek() == tHASH)
            delay = p_delay3();

         const vlog_gate_kind_t kind = get_gate_kind(token);

         do {
            vlog_node_t g = p_enable_gate_instance(kind, st);
            if (delay != NULL)
               vlog_set_delay(g, delay);
            vlog_add_stmt(mod, g);
         } while (optional(tCOMMA));
      }
      break;

   case tTRAN:
   case tRTRAN:
      {
         const vlog_gate_kind_t kind = get_gate_kind(token);

         do {
            vlog_add_stmt(mod, p_pass_switch_instance(kind));
         } while (optional(tCOMMA));
      }
      break;

   case tTRANIF0:
   case tTRANIF1:
   case tRTRANIF0:
   case tRTRANIF1:
      {
         if (peek() == tHASH)
            (void)p_delay2();

         const vlog_gate_kind_t kind = get_gate_kind(token);

         do {
            vlog_add_stmt(mod, p_pass_enable_switch_instance(kind));
         } while (optional(tCOMMA));
      }
      break;

   default:
      break;
   }

   consume(tSEMI);
}

static vlog_node_t p_module_path_expression(void)
{
   // module_path_primary
   //   | unary_module_path_operator { attribute_instance } module_path_primary
   //   | module_path_expression binary_module_path_operator
   //      { attribute_instance } module_path_expression
   //   | module_path_conditional_expression

   BEGIN("module path expression");

   // TODO: sem should check valid subset
   return p_expression();
}

static void p_path_delay_expression(void)
{
   // constant_expression
   //   | constant_expression : constant_expression : constant_expression

   BEGIN("path delay expression");

   (void)p_constant_mintypmax_expression();
}

static void p_list_of_path_delay_expressions(void)
{
   // path_delay_expression { , path_delay_expression }

   BEGIN("list of path delay expressions");

   do {
      p_path_delay_expression();
   } while (optional(tCOMMA));
}

static void p_path_delay_value(void)
{
   // list_of_path_delay_expressions | ( list_of_path_delay_expressions )

   BEGIN("path delay value");

   if (optional(tLPAREN)) {
      p_list_of_path_delay_expressions();
      consume(tRPAREN);
   }
   else
      p_list_of_path_delay_expressions();
}

static vlog_node_t p_specify_terminal_descriptor(void)
{
   // identifier [ [ constant_range_expression ] ]

   BEGIN("specify terminal descriptor");

   vlog_node_t v = vlog_new(V_REF);
   vlog_set_ident(v, p_identifier());
   vlog_set_loc(v, CURRENT_LOC);

   vlog_symtab_lookup(symtab, v);

   if (optional(tLSQUARE)) {
      (void)p_constant_range_expression();
      consume(tRSQUARE);
   }

   return v;
}

static void p_list_of_path_inputs(vlog_node_t v, vlog_node_t head)
{
   // specify_input_terminal_descriptor { , specify_input_terminal_descriptor }

   BEGIN_WITH_HEAD("list of path inputs", head);

   while (optional(tCOMMA))
      (void)p_specify_terminal_descriptor();
}

static void p_list_of_path_outputs(vlog_node_t v)
{
   // specify_output_terminal_descriptor
   //     { , specify_output_terminal_descriptor }

   BEGIN("list of path outputs");

   do {
      (void)p_specify_terminal_descriptor();
   } while (optional(tCOMMA));
}

static void p_polarity_operator(void)
{
   // + | -

   BEGIN("polarity operator");

   (void)one_of(tPLUS, tMINUS);
}

static vlog_node_t p_parallel_path_description(vlog_node_t head)
{
   // ( specify_input_terminal_descriptor [ polarity_operator ]
   //     => specify_output_terminal_descriptor )

   EXTEND("parallel path description");

   if (scan(tPLUS, tMINUS))
      (void)p_polarity_operator();

   consume(tASSOC);

   (void)p_specify_terminal_descriptor();

   consume(tRPAREN);
   return NULL;
}

static vlog_node_t p_full_path_description(vlog_node_t head)
{
   // ( list_of_path_inputs [ polarity_operator ] *> list_of_path_outputs )

   EXTEND("full path description");

   p_list_of_path_inputs(NULL, head);

   if (scan(tPLUS, tMINUS))
      (void)p_polarity_operator();

   consume(tTIMESGT);

   p_list_of_path_outputs(NULL);

   consume(tRPAREN);
   return NULL;
}

static vlog_node_t p_simple_path_declaration(void)
{
   // parallel_path_description = path_delay_value
   //   | full_path_description = path_delay_value

   BEGIN("simple path declaration");

   // Parse up to the first terminal descriptor to determine which
   // production to use

   consume(tLPAREN);

   vlog_node_t head = p_specify_terminal_descriptor();

   if (scan(tCOMMA, tTIMESGT) || peek_nth(2) == tTIMESGT)
      (void)p_full_path_description(head);
   else
      (void)p_parallel_path_description(head);

   consume(tEQ);

   (void)p_path_delay_value();

   return NULL;
}

static void p_edge_identifier(void)
{
   // posedge | negedge | edge

   BEGIN("edge identifier");

   one_of(tPOSEDGE, tNEGEDGE, tEDGE);
}

static vlog_node_t p_parallel_edge_sensitive_path_description(vlog_node_t head)
{
   // ( [ edge_identifier ] specify_input_terminal_descriptor
   //     [ polarity_operator ] => ( specify_output_terminal_descriptor
   //     [ polarity_operator ] : data_source_expression ) )

   EXTEND("parallel edge sensitive path description");

   if (scan(tPLUS, tMINUS))
      (void)p_polarity_operator();

   consume(tASSOC);

   consume(tLPAREN);

   (void)p_specify_terminal_descriptor();

   if (scan(tPLUS, tMINUS)) {
      (void)p_polarity_operator();
      consume(tCOLON);
   }
   else if (scan(tINDEXPOS, tINDEXNEG))
      consume(peek());  // Lexing ambiguity with +: and -:
   else
      consume(tCOLON);

   (void)p_expression();

   consume(tRPAREN);
   consume(tRPAREN);
   return NULL;
}

static vlog_node_t p_full_edge_sensitive_path_description(vlog_node_t head)
{
   // ( [ edge_identifier ] list_of_path_inputs [ polarity_operator ] *>
   //     ( list_of_path_outputs [ polarity_operator ]
   //     : data_source_expression ) )

   EXTEND("full edge sensitive path description");

   p_list_of_path_inputs(NULL, head);

   if (scan(tPLUS, tMINUS))
      (void)p_polarity_operator();

   consume(tTIMESGT);

   consume(tLPAREN);

   p_list_of_path_outputs(NULL);

   if (scan(tPLUS, tMINUS)) {
      (void)p_polarity_operator();
      consume(tCOLON);
   }
   else if (scan(tINDEXPOS, tINDEXNEG))
      consume(peek());  // Lexing ambiguity with +: and -:
   else
      consume(tCOLON);

   (void)p_expression();

   consume(tRPAREN);
   consume(tRPAREN);
   return NULL;
}

static vlog_node_t p_edge_sensitive_path_declaration(void)
{
   // parallel_edge_sensitive_path_description = path_delay_value
   //   | full_edge_sensitive_path_description = path_delay_value

   BEGIN("edge sensitive path declaration");

   // Parse up to the first terminal descriptor to determine which
   // production to use

   consume(tLPAREN);

   if (scan(tEDGE, tPOSEDGE, tNEGEDGE))
      p_edge_identifier();

   vlog_node_t head = p_specify_terminal_descriptor();

   if (scan(tCOMMA, tTIMESGT) || peek_nth(2) == tTIMESGT)
      (void)p_full_edge_sensitive_path_description(head);
   else
      (void)p_parallel_edge_sensitive_path_description(head);

   consume(tEQ);

   (void)p_path_delay_value();

   return NULL;
}

static vlog_node_t p_state_dependent_path_declaration(void)
{
   // if ( module_path_expression ) simple_path_declaration
   //   | if ( module_path_expression ) edge_sensitive_path_declaration
   //   | ifnone simple_path_declaration

   BEGIN("state dependent path declaration");

   switch (one_of(tIF, tIFNONE)) {
   case tIF:
      consume(tLPAREN);
      (void)p_module_path_expression();
      consume(tRPAREN);
      break;
   case tIFNONE:
      break;
   }

   if (peek_nth(2) == tID)
      (void)p_simple_path_declaration();
   else {
      // This is invalid for ifnone according to the grammar but is
      // accepted by some simulators and seen in the wild
      (void)p_edge_sensitive_path_declaration();
   }

   return NULL;
}

static vlog_node_t p_path_declaration(void)
{
   // simple_path_declaration ;
   //  | edge_sensitive_path_declaration ;
   //  | state_dependent_path_declaration ;

   BEGIN("path declaration");

   switch (peek()) {
   case tIF:
   case tIFNONE:
      (void)p_state_dependent_path_declaration();
      break;
   case tLPAREN:
      switch (peek_nth(2)) {
      case tEDGE:
      case tNEGEDGE:
      case tPOSEDGE:
         (void)p_edge_sensitive_path_declaration();
         break;
      default:
         (void)p_simple_path_declaration();
         break;
      }
      break;
   default:
      one_of(tIF, tIFNONE);
   }

   consume(tSEMI);
   return NULL;
}

static vlog_node_t p_timing_check_event_control(void)
{
   // posedge | negedge | edge | edge_control_specifier

   BEGIN("timing check event control");

   vlog_node_t v = vlog_new(V_EVENT);

   if (optional(tPOSEDGE))
      vlog_set_subkind(v, V_EVENT_POSEDGE);
   else if (optional(tNEGEDGE))
      vlog_set_subkind(v, V_EVENT_NEGEDGE);
   else if (optional(tEDGE))
      vlog_set_subkind(v, V_EVENT_EDGE);
   else
      vlog_set_subkind(v, V_EVENT_LEVEL);

   return v;
}

static vlog_node_t p_scalar_timing_check_condition(void)
{
   //    expression
   // | ~ expression
   // | expression == scalar_constant
   // | expression === scalar_constant
   // | expression != scalar_constant
   // | expression !== scalar_constant

   BEGIN("scalar timing check condition")

   return p_expression();
}

static vlog_node_t p_timing_check_condition(void)
{
   //     scalar_timing_check_condition
   // | ( scalar_timing_check_condition )

   BEGIN("timing check condition");

   if (optional(tLPAREN)) {
      vlog_node_t cond = p_scalar_timing_check_condition();
      consume(tRPAREN);
      return cond;
   }
   else
      return p_scalar_timing_check_condition();
}

static vlog_node_t p_timing_check_event(void)
{
   // [ timing_check_event_control ] specify_terminal_descriptor
   //    [ &&& timing_check_condition ]

   BEGIN("timing check event");

   vlog_node_t v = vlog_new(V_TCHECK_EVENT);

   if (scan(tPOSEDGE, tNEGEDGE, tEDGE)) {
      vlog_node_t v_e = p_timing_check_event_control();
      vlog_set_value(v_e, p_specify_terminal_descriptor());
      vlog_set_loc(v_e, CURRENT_LOC);
      vlog_add_param(v, v_e);
   }
   else
      vlog_add_param(v, p_specify_terminal_descriptor());

   if (optional(tTRPLAMP))
      vlog_add_param(v, p_timing_check_condition());

   return v;
}

static vlog_node_t p_controlled_timing_check_event(void)
{
   // timing_check_event_control specify_terminal_descriptor
   //    [ &&& timing_check_condition ]

   BEGIN("controlled timing check event");

   p_timing_check_event_control();

   (void)p_specify_terminal_descriptor();

   if (optional(tTRPLAMP))
      p_timing_check_condition();

   return NULL;
}

static void p_notifier(vlog_node_t tcheck)
{
   // notifier ::= variable_identifier
   //
   // A violation toggles the notifier: x becomes 0, 0 and 1 swap and z
   // is left unchanged (1800-2017 section 31.6)

   if (peek() != tID)
      return;

   const loc_t loc = state.last_loc;
   ident_t id = p_identifier();

   vlog_node_t decl = vlog_symtab_query(symtab, id);
   if (decl == NULL || vlog_kind(decl) != V_VAR_DECL)
      return;

   vlog_node_t ref = vlog_new(V_REF);
   vlog_set_ident(ref, id);
   vlog_set_ref(ref, decl);
   vlog_set_loc(ref, &state.last_loc);

   vlog_node_t z = vlog_new(V_NUMBER);
   vlog_set_number(z, number_new("1'bz", &loc));
   vlog_set_loc(z, &loc);

   vlog_node_t zero = vlog_new(V_NUMBER);
   vlog_set_number(zero, number_new("1'b0", &loc));
   vlog_set_loc(zero, &loc);

   vlog_node_t one = vlog_new(V_NUMBER);
   vlog_set_number(one, number_new("1'b1", &loc));
   vlog_set_loc(one, &loc);

   vlog_node_t is_z = vlog_new(V_BINARY);
   vlog_set_subkind(is_z, V_BINARY_CASE_EQ);
   vlog_set_left(is_z, ref);
   vlog_set_right(is_z, z);
   vlog_set_loc(is_z, &loc);

   vlog_node_t is_zero = vlog_new(V_BINARY);
   vlog_set_subkind(is_zero, V_BINARY_CASE_EQ);
   vlog_set_left(is_zero, ref);
   vlog_set_right(is_zero, zero);
   vlog_set_loc(is_zero, &loc);

   vlog_node_t flip = vlog_new(V_COND_EXPR);
   vlog_set_value(flip, is_zero);
   vlog_set_left(flip, one);
   vlog_set_right(flip, zero);
   vlog_set_loc(flip, &loc);

   vlog_node_t toggle = vlog_new(V_COND_EXPR);
   vlog_set_value(toggle, is_z);
   vlog_set_left(toggle, ref);
   vlog_set_right(toggle, flip);
   vlog_set_loc(toggle, &loc);

   vlog_node_t assign = vlog_new(V_BASSIGN);
   vlog_set_target(assign, ref);
   vlog_set_value(assign, toggle);
   vlog_set_loc(assign, &loc);

   vlog_add_param(tcheck, assign);
}

static vlog_node_t p_setup_or_hold_timing_check(void)
{
   // $setup ( data_event , reference_event , timing_check_limit
   //   [ , [ notifier ] ] ) ;
   //
   // $hold ( reference_event , data_event , timing_check_limit
   //   [ , [ notifier ] ] ) ;

   BEGIN("setup/hold timing check");

   vlog_tcheck_kind_t subkind;
   switch (one_of(tDLRSETUP, tDLRHOLD)) {
   case tDLRSETUP:
      subkind = V_TCHECK_SETUP;
      break;
   default:
      subkind = V_TCHECK_HOLD;
      break;
   }
   consume(tLPAREN);

   vlog_node_t v = vlog_new(V_TCHECK);
   vlog_set_subkind(v, subkind);
   vlog_set_ival(v, time_ival());

   vlog_add_param(v, p_timing_check_event());
   consume(tCOMMA);
   vlog_add_param(v, p_timing_check_event());
   consume(tCOMMA);
   vlog_add_param(v, p_expression());

   if (optional(tCOMMA))
      p_notifier(v);

   consume(tRPAREN);
   consume(tSEMI);

   vlog_set_loc(v, CURRENT_LOC);

   return v;
}

static vlog_node_t p_recovery_or_removal_timing_check(void)
{
   // $recovery ( reference_event , data_event , timing_check_limit
   //   [ , [ notifier ] ] ) ;
   //
   // $removal ( reference_event , data_event , timing_check_limit
   //   [ , [ notifier ] ] ) ;

   BEGIN("recovery/removal timing check");

   vlog_tcheck_kind_t subkind;
   switch (one_of(tDLRRECOVERY, tDLRREMOVAL)) {
   case tDLRRECOVERY:
      subkind = V_TCHECK_RECOVERY;
      break;
   default:
      subkind = V_TCHECK_REMOVAL;
      break;
   }
   consume(tLPAREN);

   vlog_node_t v = vlog_new(V_TCHECK);
   vlog_set_subkind(v, subkind);
   vlog_set_ival(v, time_ival());

   vlog_add_param(v, p_timing_check_event());

   consume(tCOMMA);

   vlog_add_param(v, p_timing_check_event());

   consume(tCOMMA);

   vlog_add_param(v, p_expression());

   if (optional(tCOMMA))
      p_notifier(v);

   consume(tRPAREN);
   consume(tSEMI);

   vlog_set_loc(v, CURRENT_LOC);

   return v;
}

static vlog_node_t p_width_timing_check(void)
{
   // $width ( controlled_reference_event , timing_check_limit , threshold
   //   [ , [ notifier ] ] ) ;

   BEGIN("width timing check");

   consume(tDLRWIDTH);
   consume(tLPAREN);

   (void)p_controlled_timing_check_event();

   consume(tCOMMA);

   (void)p_expression();

   consume(tCOMMA);

   (void)p_constant_expression();

   if (optional(tCOMMA)) {
      if (peek() == tID)
         p_identifier();
   }

   consume(tRPAREN);
   consume(tSEMI);

   return NULL;
}

static vlog_node_t p_delayed_data_or_reference(void)
{
   // terminal_identifier
   //   | terminal_identifier [ constant_mintypmax_expression ]

   BEGIN("delayed data/reference");

   p_identifier();

   return NULL;
}

static vlog_node_t p_period_timing_check(void)
{
   // $period ( controlled_reference_event , timing_check_limit
   //   [ , [ notifier ] ] ) ;

   BEGIN("period timing check");

   consume(tDLRPERIOD);
   consume(tLPAREN);

   (void)p_controlled_timing_check_event();

   consume(tCOMMA);

   (void)p_expression();

   if (optional(tCOMMA)) {
      if (peek() == tID)
         p_identifier();
   }

   consume(tRPAREN);
   consume(tSEMI);

   return NULL;
}

static vlog_node_t p_setuphold_or_recrem_timing_check(void)
{
   // $setuphold ( reference_event , data_event , timing_check_limit ,
   //    timing_check_limit [ , [ notifier ] [ , [ timestamp_condition ]
   //    [ , [ timecheck_condition ] [ , [ delayed_reference ]
   //    [ , [ delayed_data ] ] ] ] ] ] ) ;

   BEGIN("setuphold/recrem timing check");

   one_of(tDLRSETUPHOLD, tDLRRECREM);
   consume(tLPAREN);

   (void)p_timing_check_event();

   consume(tCOMMA);

   (void)p_timing_check_event();

   consume(tCOMMA);

   (void)p_expression();

   consume(tCOMMA);

   (void)p_expression();

   if (optional(tCOMMA)) {
      if (peek() == tID)
         p_identifier(); // notifier

      if (optional(tCOMMA)) {
         if (not_at_token(tCOMMA, tRPAREN))
            (void)p_mintypmax_expression();  // timestamp_condition

         if (optional(tCOMMA)) {
            if (not_at_token(tCOMMA, tRPAREN))
               (void)p_mintypmax_expression();  // timecheck_condition

            if (optional(tCOMMA)) {
               if (not_at_token(tCOMMA, tRPAREN))
                  p_delayed_data_or_reference();  // delayed_reference

               if (optional(tCOMMA)) {
                  if (not_at_token(tCOMMA, tRPAREN))
                     p_delayed_data_or_reference();  // delayed_data
               }
            }
         }
      }
   }

   consume(tRPAREN);
   consume(tSEMI);

   return NULL;
}

static vlog_node_t p_system_timing_check(void)
{
   // $setup_timing_check | $hold_timing_check | $setuphold_timing_check
   //   | $recovery_timing_check | $removal_timing_check | $recrem_timing_check
   //   | $skew_timing_check | $timeskew_timing_check | $fullskew_timing_check
   //   | $period_timing_check | $width_timing_check | $nochange_timing_check

   BEGIN("system timing check");

   switch (peek()) {
   case tDLRSETUP:
   case tDLRHOLD:
      return p_setup_or_hold_timing_check();
   case tDLRRECOVERY:
   case tDLRREMOVAL:
      return p_recovery_or_removal_timing_check();
   case tDLRWIDTH:
      return p_width_timing_check();
   case tDLRPERIOD:
      return p_period_timing_check();
   case tDLRSETUPHOLD:
   case tDLRRECREM:
      return p_setuphold_or_recrem_timing_check();
   default:
      should_not_reach_here();
   }
}

static vlog_node_t p_pulse_control_specparam(void)
{
   // PATHPULSE$ = (reject_limit_value [, error_limit_value])
   //   | PATHPULSE$specify_input_terminal_descriptor
   //       $specify_output_terminal_descriptor =
   //       (reject_limit_value [, error_limit_value])

   BEGIN("pulse control specparam");

   vlog_node_t v = vlog_new(V_SPECPARAM);

   consume(tPATHPULSE);
   consume(tEQ);

   if (optional(tLPAREN)) {
      vlog_set_value(v, p_constant_mintypmax_expression());

      if (optional(tCOMMA))
         (void)p_constant_mintypmax_expression();

      consume(tRPAREN);
   }
   else {
      // Accepted by other simulators and present in Xilinx models
      vlog_set_value(v, p_constant_mintypmax_expression());
   }

   vlog_set_loc(v, CURRENT_LOC);
   return v;
}

static vlog_node_t p_specparam_assignment(void)
{
   // specparam_identifier = constant_mintypmax_expression
   //   | pulse_control_specparam

   BEGIN("specparam assignment");

   if (peek() == tPATHPULSE)
      return p_pulse_control_specparam();
   else {
      vlog_node_t v = vlog_new(V_SPECPARAM);
      vlog_set_ident(v, p_identifier());

      consume(tEQ);

      vlog_set_value(v, p_constant_mintypmax_expression());

      vlog_set_loc(v, CURRENT_LOC);
      vlog_symtab_put(symtab, v);
      return v;
   }
}

static void p_list_of_specparam_assignments(vlog_node_t parent)
{
   // specparam_assignment { , specparam_assignment }

   BEGIN("list of specparam assignments");

   do {
      vlog_add_decl(parent, p_specparam_assignment());
   } while (optional(tCOMMA));
}

static void p_specparam_declaration(vlog_node_t parent)
{
   // specparam [ packed_dimension ] list_of_specparam_assignments ;

   BEGIN("specparam declaration");

   consume(tSPECPARAM);

   if (peek() == tLSQUARE)
      (void)p_packed_dimension();

   p_list_of_specparam_assignments(parent);

   consume(tSEMI);
}

static void p_specify_item(vlog_node_t parent)
{
   // specparam_declaration | pulsestyle_declaration | showcancelled_declaration
   //   | path_declaration | system_timing_check

   BEGIN("specify item");

   switch (peek()) {
   case tSPECPARAM:
      p_specparam_declaration(parent);
      break;
   case tLPAREN:
   case tIF:
   case tIFNONE:
      (void)p_path_declaration();
      break;
   case tDLRSETUP:
   case tDLRHOLD:
   case tDLRRECOVERY:
   case tDLRREMOVAL:
   case tDLRSETUPHOLD:
   case tDLRRECREM:
   case tDLRWIDTH:
   case tDLRPERIOD:
      {
         vlog_node_t tcheck = p_system_timing_check();
         // TODO: Remove once all timing checks are supported.
         if (tcheck != NULL)
            vlog_add_stmt(parent, tcheck);
      }
      break;
   default:
      one_of(tSPECPARAM, tLPAREN, tIF, tIFNONE, tDLRSETUP, tDLRHOLD,
             tDLRRECOVERY, tDLRREMOVAL, tDLRSETUPHOLD, tDLRRECREM,
             tDLRWIDTH, tDLRPERIOD);
   }
}

static vlog_node_t p_specify_block(void)
{
   // specify { specify_item } endspecify

   BEGIN("specify block");

   consume(tSPECIFY);

   vlog_node_t v = vlog_new(V_SPECIFY);
   vlog_set_loc(v, CURRENT_LOC);

   vlog_symtab_push(symtab, v);

   while (not_at_token(tENDSPECIFY))
      p_specify_item(v);

   vlog_symtab_pop(symtab);

   consume(tENDSPECIFY);

   vlog_set_loc(v, CURRENT_LOC);
   return v;
}

static void p_port_conn_flags(vlog_node_t v)
{
   vlog_version_t vers;
   (void)get_verilog_keywords(&vers);

   if (vers >= VLOG_1800_2005)
      vlog_set_flags(v, VLOG_F_SYSTEMVERILOG);
}

static vlog_node_t conn_event = NULL;

// The module or interface whose ports are being connected
static vlog_node_t port_target = NULL;
static int port_pos = 0;

static vlog_node_t port_formal(ident_t name, int pos)
{
   // The declaration of the formal a connection is for if known

   if (port_target == NULL)
      return NULL;

   const int nports = vlog_ports(port_target);
   for (int i = 0; i < nports; i++) {
      vlog_node_t p = vlog_port(port_target, i);
      if (vlog_kind(p) != V_REF || !vlog_has_ref(p))
         continue;

      vlog_node_t d = vlog_ref(p);
      if (vlog_kind(d) != V_PORT_DECL || !vlog_has_type(d))
         continue;
      else if (name != NULL ? vlog_ident(d) == name : i == pos)
         return d;
   }

   return NULL;
}

static vlog_node_t p_connection_expression(ident_t name, int index)
{
   // An edge of a signal connected to an event port is an event which a
   // process triggers when the edge occurs

   conn_event = NULL;

   if (peek() == tTICK && peek_nth(2) == tLBRACE) {
      // The type of an assignment pattern is that of the formal
      vlog_node_t formal = port_formal(name, index);
      if (formal != NULL && vlog_get_type(vlog_type(formal)) != NULL) {
         vlog_node_t dt = vlog_type(formal);
         pattern_array_elems = is_fixed_unpacked(formal, dt);
         vlog_node_t v = p_assignment_pattern_expression(vlog_get_type(dt));
         pattern_array_elems = false;
         return v;
      }
   }

   if ((peek() != tPOSEDGE && peek() != tNEGEDGE) || current_module == NULL)
      return p_expression();

   const bool pos = peek() == tPOSEDGE;
   consume(pos ? tPOSEDGE : tNEGEDGE);

   vlog_node_t sig = p_expression();
   const loc_t *loc = vlog_loc(sig);

   static unsigned count = 0;

   vlog_node_t evtype = vlog_new(V_DATA_TYPE);
   vlog_set_subkind(evtype, DT_EVENT);
   vlog_set_loc(evtype, loc);

   vlog_node_t var = clocking_var(current_module,
                                  ident_sprintf("$pev%u", count++), evtype,
                                  NULL, loc);

   vlog_node_t ev = vlog_new(V_EVENT);
   vlog_set_subkind(ev, pos ? V_EVENT_POSEDGE : V_EVENT_NEGEDGE);
   vlog_set_value(ev, sig);
   vlog_set_loc(ev, loc);

   vlog_node_t ctrl = vlog_new(V_EVENT_CONTROL);
   vlog_add_param(ctrl, ev);
   vlog_set_loc(ctrl, loc);

   vlog_node_t trig = vlog_new(V_EVENT_TRIGGER);
   vlog_set_ident(trig, vlog_ident(var));
   vlog_set_value(trig, clocking_ref(var, loc));
   vlog_set_loc(trig, loc);

   clocking_always(current_module, "$pev", trig, ctrl, loc);

   conn_event = ev;
   return clocking_ref(var, loc);
}

static vlog_node_t p_ordered_port_connection(void)
{
   // { attribute_instance } [ expression ]

   BEGIN("ordered port connection");

   optional_attributes();

   vlog_node_t v = vlog_new(V_PORT_CONN);
   p_port_conn_flags(v);

   if (not_at_token(tCOMMA, tRPAREN)) {
      vlog_set_value(v, p_connection_expression(NULL, port_pos));
      if (conn_event != NULL)
         vlog_set_ref(v, conn_event);   // Substituted for an event formal
   }
   port_pos++;

   vlog_set_loc(v, CURRENT_LOC);
   return v;
}

static vlog_node_t p_named_port_connection(void)
{
   // { attribute_instance } . port_identifier [ ( [ expression ] ) ]
   //    | { attribute_instance } .*

   BEGIN("named port connection");

   optional_attributes();

   vlog_node_t v = vlog_new(V_PORT_CONN);
   p_port_conn_flags(v);

   if (optional(tDOTSTAR)) {
      // Implicit connections to the names of the ports of the instance
      vlog_set_flags(v, vlog_flags(v) | VLOG_F_WILDCARD);

      vlog_node_t unit = vlog_symtab_design_unit(symtab);
      if (unit != NULL)
         vlog_set_ref(v, unit);

      vlog_set_loc(v, CURRENT_LOC);
      return v;
   }

   consume(tDOT);

   ident_t id = p_identifier();
   vlog_set_ident(v, id);

   if (optional(tLPAREN)) {

      if (peek() != tRPAREN) {
         vlog_set_value(v, p_connection_expression(id, 0));
         if (conn_event != NULL)
            vlog_set_ref(v, conn_event);
      }

      consume(tRPAREN);
   }
   else {
      // Implicit connection to the object with the same name
      vlog_node_t d = vlog_symtab_query(symtab, id);
      if (d != NULL && (vlog_kind(d) == V_MOD_INST
                        || vlog_kind(d) == V_IFACE_PORT))
         vlog_set_value(v, interface_as_value(id, &state.last_loc));
      else {
         vlog_node_t ref = vlog_new(V_REF);
         vlog_set_ident(ref, id);
         vlog_set_loc(ref, &state.last_loc);
         vlog_symtab_lookup(symtab, ref);
         vlog_set_value(v, ref);
      }
   }

   vlog_set_loc(v, CURRENT_LOC);
   return v;
}

static void p_list_of_port_connections(vlog_node_t inst)
{
   // ordered_port_connection { , ordered_port_connection }
   //   | named_port_connection { , named_port_connection }

   BEGIN("list of port connections");

   vlog_symtab_set_implicit(symtab, implicit_kind);

   port_pos = 0;

   do {
      skip_over_attributes();

      if (peek() == tDOT || peek() == tDOTSTAR)
         vlog_add_param(inst, p_named_port_connection());
      else
         vlog_add_param(inst, p_ordered_port_connection());
   } while (optional(tCOMMA));

   vlog_symtab_set_implicit(symtab, V_NET_NONE);
}

static vlog_node_t p_hierarchical_instance(void)
{
   // name_of_instance ( [ list_of_port_connections ] )

   BEGIN("hierarchical instance");

   vlog_node_t v = vlog_new(V_MOD_INST);
   vlog_set_ident(v, p_identifier());

   if (peek() == tLSQUARE)
      vlog_add_range(v, p_unpacked_dimension());

   if (lib_scan_active() && peek() != tLPAREN) {
      // An instance always has a port list so this must be a declaration
      // of a variable whose type is not available: skip any initial value
      // and do not record it as an instance
      int depth = 0;
      for (;;) {
         const token_t tok = peek();
         if (tok == tEOF || (depth <= 0 && (tok == tSEMI || tok == tCOMMA)))
            break;
         else if (tok == tLPAREN || tok == tLSQUARE || tok == tLBRACE)
            depth++;
         else if (tok == tRPAREN || tok == tRSQUARE || tok == tRBRACE)
            depth--;
         consume(tok);
      }

      vlog_set_loc(v, CURRENT_LOC);
      vlog_symtab_put(symtab, v);
      return NULL;
   }

   consume(tLPAREN);

   if (peek() != tRPAREN)
      p_list_of_port_connections(v);

   consume(tRPAREN);

   vlog_set_loc(v, CURRENT_LOC);
   vlog_symtab_put(symtab, v);
   return v;
}

static vlog_node_t p_udp_instance(void)
{
   // [ name_of_instance ] ( output_terminal , input_terminal
   //   { , input_terminal } )

   BEGIN("udp instance");

   vlog_node_t v = vlog_new(V_MOD_INST);
   if (peek() == tID)
      vlog_set_ident(v, p_identifier());
   else
      vlog_set_ident(v, ident_uniq("$unnamed"));

   consume(tLPAREN);

   p_list_of_port_connections(v);

   consume(tRPAREN);

   vlog_set_loc(v, CURRENT_LOC);
   return v;
}

// The module or interface whose parameters are being assigned
static vlog_node_t param_target = NULL;

static bool is_param_decl(vlog_node_t d)
{
   return vlog_kind(d) == V_PARAM_DECL
      || (vlog_kind(d) == V_TYPE_DECL && (vlog_flags(d) & VLOG_F_TYPEPARAM));
}

static vlog_node_t param_pattern_type(ident_t name, int pos)
{
   // The type an assignment pattern takes as the value of a parameter

   if (param_target == NULL)
      return NULL;

   int index = 0;
   const int ndecls = vlog_decls(param_target);
   for (int i = 0; i < ndecls; i++) {
      vlog_node_t d = vlog_decl(param_target, i);
      if (!is_param_decl(d))
         continue;
      else if (name != NULL ? vlog_ident(d) == name : index++ == pos)
         return vlog_kind(d) == V_PARAM_DECL && vlog_has_type(d)
            ? vlog_get_type(vlog_type(d)) : NULL;
   }

   return NULL;
}

static bool param_is_type(ident_t name, int pos)
{
   // The parameter being assigned is a type parameter of the target

   if (param_target == NULL)
      return false;

   int index = 0;
   const int ndecls = vlog_decls(param_target);
   for (int i = 0; i < ndecls; i++) {
      vlog_node_t d = vlog_decl(param_target, i);
      if (!is_param_decl(d))
         continue;
      else if (name != NULL ? vlog_ident(d) == name : index++ == pos)
         return vlog_kind(d) == V_TYPE_DECL;
   }

   return false;
}

static vlog_node_t p_param_expression(ident_t name, int pos)
{
   // mintypmax_expression | data_type | $

   BEGIN("param expression");

   if (peek() == tTICK && peek_nth(2) == tLBRACE) {
      vlog_node_t type = param_pattern_type(name, pos);
      if (type != NULL)
         return p_assignment_pattern_expression(type);
   }

   if (param_is_type(name, pos)
       || (param_target == NULL
           && !scan(tCONST, tVAR, tAUTOMATIC, tSTATIC)
           && scan_type_declaration()))
      return p_data_type();

   return p_expression();   // TODO
}

static vlog_node_t p_named_parameter_assignment(void)
{
   // . parameter_identifier ( [ param_expression ] )

   BEGIN("named parameter assignment");

   consume(tDOT);

   vlog_node_t v = vlog_new(V_PARAM_ASSIGN);
   vlog_set_ident(v, p_identifier());

   consume(tLPAREN);

   if (peek() != tRPAREN)
      vlog_set_value(v, p_param_expression(vlog_ident(v), 0));

   consume(tRPAREN);

   vlog_set_loc(v, CURRENT_LOC);
   return v;
}

static vlog_node_t p_ordered_parameter_assignment(vlog_node_t inst)
{
   // param_expression

   BEGIN("ordered parameter assignment");

   vlog_node_t v = vlog_new(V_PARAM_ASSIGN);
   vlog_set_value(v, p_param_expression(NULL, vlog_params(inst)));

   vlog_set_loc(v, CURRENT_LOC);
   return v;
}

static void p_list_of_parameter_assignments(vlog_node_t inst)
{
   // ordered_parameter_assignment { , ordered_parameter_assignment }
   //   | named_parameter_assignment { , named_parameter_assignment }

   BEGIN("list of parameter assignments");

   do {
      if (peek() == tDOT)
         vlog_add_param(inst, p_named_parameter_assignment());
      else
         vlog_add_param(inst, p_ordered_parameter_assignment(inst));
   } while (optional(tCOMMA));
}

static void p_parameter_value_assignment(vlog_node_t inst)
{
   // # ( [ list_of_parameter_assignments ] )

   BEGIN("parameter value assignment");

   consume(tHASH);
   consume(tLPAREN);

   if (peek() != tRPAREN)
      p_list_of_parameter_assignments(inst);

   consume(tRPAREN);
}

static void p_module_or_udp_instantiation(vlog_node_t mod)
{
   // module_identifier [ parameter_value_assignment ] hierarchical_instance
   //   { , hierarchical_instance } ;
   //
   // udp_identifier [ drive_strength ] [ delay2 ] udp_instance
   //   { , udp_instance } ;

   BEGIN("module instantiation");

   vlog_node_t v = vlog_new(V_INST_LIST);
   vlog_set_ident(v, p_identifier());

   if (peek() == tHASH) {
      vlog_node_t target = lookup_interface(vlog_ident(v));
      if (target != NULL && vlog_kind(target) != V_MODULE)
         target = NULL;

      param_target = target;
      p_parameter_value_assignment(v);
      param_target = NULL;
   }

   vlog_node_t ptarget = lookup_interface(vlog_ident(v));
   port_target = ptarget != NULL && vlog_kind(ptarget) == V_MODULE
      ? ptarget : NULL;

   // A packed dimension can only follow the type of a declaration
   while (lib_scan_active() && peek() == tLSQUARE) {
      int depth = 0;
      do {
         const token_t tok = peek();
         if (tok == tEOF)
            break;
         else if (tok == tLSQUARE)
            depth++;
         else if (tok == tRSQUARE)
            depth--;
         consume(tok);
      } while (depth > 0);
   }

   do {
      if (peek() == tLPAREN)
         vlog_add_stmt(v, p_udp_instance());
      else {
         vlog_node_t i = p_hierarchical_instance();
         if (i != NULL)   // Null for a declaration when scanning
            vlog_add_stmt(v, i);
      }
   } while (optional(tCOMMA));

   port_target = NULL;

   consume(tSEMI);

   vlog_set_loc(v, CURRENT_LOC);

   if (inst_lists == NULL)
      inst_lists = hash_new(16);

   for (int i = 0; i < vlog_stmts(v); i++)
      hash_put(inst_lists, vlog_stmt(v, i), v);

   if (vlog_params(v) == 0) {
      if (inst_modules == NULL)
         inst_modules = hash_new(16);

      const int ninst = vlog_stmts(v);
      for (int i = 0; i < ninst; i++)
         hash_put(inst_modules, vlog_stmt(v, i), vlog_ident(v));
   }

   vlog_add_stmt(mod, v);
}

static vlog_node_t p_defparam_assignment(void)
{
   // hierarchical_parameter_identifier = constant_mintypmax_expression

   BEGIN("defparam assignment");

   vlog_node_t v = vlog_new(V_DEFPARAM);
   vlog_set_target(v, p_hierarchical_identifier(NULL));

   consume(tEQ);

   vlog_set_value(v, p_constant_mintypmax_expression());

   vlog_set_loc(v, CURRENT_LOC);
   return v;
}

static void p_list_of_defparam_assignments(vlog_node_t parent)
{
   // defparam_assignment { , defparam_assignment }

   BEGIN("list of defparam assignments");

   do {
      vlog_add_stmt(parent, p_defparam_assignment());
   } while (optional(tCOMMA));
}

static void p_parameter_override(vlog_node_t parent)
{
   // defparam list_of_defparam_assignments ;

   BEGIN("parameter override");

   consume(tDEFPARAM);

   p_list_of_defparam_assignments(parent);

   consume(tSEMI);
}

static void p_module_or_generate_item(vlog_node_t mod)
{
   // { attribute_instance } parameter_override
   //   | { attribute_instance } gate_instantiation
   //   | { attribute_instance } udp_instantiation
   //   | { attribute_instance } module_instantiation
   //   | { attribute_instance } module_common_item

   BEGIN("module or generate item");

   optional_attributes();

   if (optional(tSEMI))
      return;   // Null item

   if (peek() == tSYSTASK) {
      // Elaboration system task: reported when the simulation starts
      vlog_node_t v = vlog_new(V_INITIAL);
      vlog_set_ident(v, default_label("initial"));
      vlog_add_stmt(v, p_statement_or_null());
      vlog_set_loc(v, CURRENT_LOC);
      vlog_add_stmt(mod, v);
      return;
   }

   if (peek() == tID && peek_nth(2) == tCLOCKING
       && peek_ident_nth(1) == ident_new("global")) {
      consume(tID);
      last_clocking_ctrl = NULL;
      p_clocking_declaration(mod, false);
      global_clocking_synthesise(mod, last_clocking_ctrl, CURRENT_LOC);
      return;
   }

   if (at_covergroup()) {
      p_covergroup_declaration(mod);
      return;
   }

   if (is_let_declaration()) {
      p_let_declaration();
      return;
   }

   if (peek() == tID && peek_ident_nth(1) == ident_new("bind")
       && peek_nth(2) == tID) {
      // Applies to every instance of the target wherever it is declared so
      // is parsed again as a design unit once the module has been
      static const token_t stops[] = { tSEMI, tEOF };
      cov_toks_t toks = cov_capture(stops, false);
      consume(tSEMI);

      // A branch of a generate construct which is known not to be taken
      // does not bind anything
      if (gen_dead == 0) {
         APUSH(pending_binds, toks);
         APUSH(pending_bind_scopes, vlog_kind(mod) == V_GEN_BLOCK
               ? vlog_ident2(module_root) : vlog_ident2(mod));
      }
      return;
   }

   if (at_checker()) {
      // A checker declared in a module is visible by name like one at the
      // top level so is parsed again as a design unit
      tokenq_t *t = NULL;
      int n = 0;
      while (peek() != tEOF && !(peek() == tID
                                 && peek_ident_nth(1) == ident_new("endchecker")))
         COV_PUSH(t, n, cov_take());

      if (peek() == tID)
         COV_PUSH(t, n, cov_take());

      if (peek() == tCOLON) {
         COV_PUSH(t, n, cov_take());
         COV_PUSH(t, n, cov_take());
      }

      register_checker(t, n);
      APUSH(pending_units, ((cov_toks_t){ t, n }));
      return;
   }

   if (peek() == tMODULE || peek() == tPROGRAM
       || (peek() == tINTERFACE && peek_nth(2) != tCLASS)) {
      // A nested declaration is visible by name like any other design unit
      // so is parsed again as one once the enclosing module has been
      tokenq_t *t = NULL;
      int n = 0, depth = 0;
      do {
         const token_t tok = peek();
         if (tok == tEOF)
            break;
         else if (tok == tMODULE || tok == tPROGRAM
                  || (tok == tINTERFACE && peek_nth(2) != tCLASS))
            depth++;
         else if (tok == tENDMODULE || tok == tENDPROGRAM
                  || tok == tENDINTERFACE)
            depth--;

         COV_PUSH(t, n, cov_take());
      } while (depth > 0);

      if (peek() == tCOLON) {
         COV_PUSH(t, n, cov_take());
         COV_PUSH(t, n, cov_take());
      }

      APUSH(pending_units, ((cov_toks_t){ t, n }));
      return;
   }

   switch (peek()) {
   case tALWAYS:
   case tALWAYSCOMB:
   case tALWAYSFF:
   case tALWAYSLATCH:
   case tWIRE:
   case tUWIRE:
   case tSUPPLY0:
   case tSUPPLY1:
   case tTRI:
   case tTRI0:
   case tTRI1:
   case tTRIAND:
   case tTRIOR:
   case tTRIREG:
   case tWAND:
   case tWOR:
   case tINTERCONNECT:
   case tREG:
   case tSTRUCT:
   case tUNION:
   case tASSIGN:
   case tINITIAL:
   case tTYPEDEF:
   case tNETTYPE:
   case tENUM:
   case tSVINT:
   case tINTEGER:
   case tSVREAL:
   case tSHORTREAL:
   case tREALTIME:
   case tTIME:
   case tTYPE:
   case tCLASS:
   case tINTERFACE:
   case tCONSTRAINT:
   case tVIRTUAL:
   case tSTATIC:
   case tAUTOMATIC:
   case tCONST:
   case tTASK:
   case tFUNCTION:
   case tLOCALPARAM:
   case tPARAMETER:
   case tIF:
   case tCASE:
   case tFOR:
   case tEVENT:
   case tGENVAR:
   case tVAR:
   case tLOGIC:
   case tBIT:
   case tSHORTINT:
   case tLONGINT:
   case tBYTE:
   case tSTRINGK: case tCHANDLE:
   case tIMPORT:
   case tFINAL:
   case tASSERT:
   case tASSUME:
   case tCOVER:
   case tSEQUENCE:
   case tPROPERTY:
   case tCLOCKING:
   case tDEFAULT:
      p_module_common_item(mod);
      break;
   case tPULLDOWN:
   case tPULLUP:
   case tAND:
   case tNAND:
   case tOR:
   case tNOR:
   case tXOR:
   case tXNOR:
   case tNOT:
   case tBUF:
   case tBUFIF0:
   case tBUFIF1:
   case tNOTIF0:
   case tNOTIF1:
   case tTRAN:
   case tTRANIF0:
   case tTRANIF1:
   case tRTRAN:
   case tRTRANIF0:
   case tRTRANIF1:
      p_gate_instantiation(mod);
      break;
   case tDEFPARAM:
      p_parameter_override(mod);
      break;
   case tID:
      if (peek_nth(2) == tCOLON || is_scoped_type_start()) {
         p_module_common_item(mod);
         break;
      }
      else {
         vlog_node_t ref = peek_reference();
         if (ref == NULL && peek_nth(2) == tHASH
             && state.tokenq[state.tokenq_tail].lval.ident
             == ident_new("mailbox")) {
            p_module_common_item(mod);
            break;
         }
         else if (is_process_name() && (peek_nth(2) == tID
                                        || peek_nth(2) == tSCOPE)) {
            p_module_common_item(mod);
            break;
         }
         else if (ref == NULL && peek_nth(2) == tID
                  && (peek_ident_nth(1) == ident_new("semaphore")
                      || peek_ident_nth(1) == ident_new("mailbox"))) {
            p_module_common_item(mod);
            break;
         }
         else if (ref == NULL) {
            p_module_or_udp_instantiation(mod);
            break;
         }
         else if (vlog_kind(ref) == V_MOD_INST
                  && (peek_nth(2) == tHASH
                      || (peek_nth(2) == tID && (peek_nth(3) == tLPAREN
                                                 || peek_nth(3) == tLSQUARE)))) {
            // An instance may have the same name as its module
            p_module_or_udp_instantiation(mod);
            break;
         }
         else if (is_data_type(ref) || peek_nth(2) == tID) {
            p_module_common_item(mod);
            break;
         }
      }
      // Fall-through
   default:
      expect(tALWAYS, tALWAYSCOMB, tALWAYSFF, tALWAYSLATCH, tWIRE, tUWIRE,
             tSUPPLY0, tSUPPLY1, tTRI,  tTRI0, tTRI1, tTRIAND, tTRIOR, tTRIREG,
             tWAND, tWOR, tINTERCONNECT, tREG, tSTRUCT, tUNION, tASSIGN,
             tINITIAL, tTYPEDEF, tENUM, tSVINT, tINTEGER, tSVREAL, tSHORTREAL,
             tREALTIME, tTIME, tTASK, tFUNCTION, tLOCALPARAM, tPARAMETER, tIF,
             tFOR, tEVENT, tGENVAR, tVAR, tLOGIC, tBIT, tSHORTINT, tLONGINT,
             tBYTE, tSTRINGK, tIMPORT, tFINAL, tASSERT, tASSUME, tCOVER,
             tPULLDOWN, tPULLUP, tID, tAND, tNAND, tOR, tNOR, tXOR, tXNOR,
             tNOT, tBUF, tBUFIF0, tBUFIF1, tNOTIF0, tNOTIF1, tDEFPARAM, tID);
      drop_tokens_until(&state, tSEMI);
   }
}

static void p_generate_region(vlog_node_t mod)
{
   // generate { generate_item } endgenerate

   BEGIN("generate region");

   // Has no real meaning in System Verilog

   // TODO: generate regions do not nest so check mod is V_MODULE

   consume(tGENERATE);

   if (optional(tBEGIN)) {
      // This is non-standard but seen in some legacy code
      consume(tCOLON);

      ident_t label = p_identifier();

      vlog_node_t b = vlog_new(V_GEN_BLOCK);
      vlog_set_ident(b, label);

      while (not_at_token(tEND))
         p_generate_item(b);

      consume(tEND);

      if (optional(tCOLON)) {
         ident_t name = p_identifier();
         if (label == NULL)
            parse_error(&state.last_loc, "block does not have a label");
         else if (name != label)
            parse_error(&state.last_loc, "%pQ does not match label %pQ",
                        name, label);
      }

      vlog_set_loc(b, CURRENT_LOC);
      vlog_add_stmt(mod, b);
   }
   else {
      while (not_at_token(tENDGENERATE))
         p_generate_item(mod);
   }

   consume(tENDGENERATE);
}

static void p_non_port_module_item(vlog_node_t mod)
{
   // generate_region | module_or_generate_item | specify_block
   //   | { attribute_instance } specparam_declaration | program_declaration
   //   | module_declaration | interface_declaration | timeunits_declaration

   BEGIN("non-port module item");

   if (peek() == tEXPORT && peek_nth(2) == tSTRING) {
      p_dpi_export(mod);
      return;
   }

   switch (peek()) {
   case tSEMI:
      consume(tSEMI);
      break;
   case tALWAYS:
   case tALWAYSCOMB:
   case tALWAYSFF:
   case tALWAYSLATCH:
   case tWIRE:
   case tUWIRE:
   case tSUPPLY0:
   case tSUPPLY1:
   case tTRI:
   case tTRI0:
   case tTRI1:
   case tTRIAND:
   case tTRIOR:
   case tTRIREG:
   case tWAND:
   case tWOR:
   case tINTERCONNECT:
   case tREG:
   case tSTRUCT:
   case tUNION:
   case tASSIGN:
   case tINITIAL:
   case tPULLDOWN:
   case tPULLUP:
   case tID:
   case tATTRBEGIN:
   case tAND:
   case tNAND:
   case tOR:
   case tNOR:
   case tXOR:
   case tXNOR:
   case tNOT:
   case tBUF:
   case tBUFIF0:
   case tBUFIF1:
   case tNOTIF0:
   case tNOTIF1:
   case tTRAN:
   case tTRANIF0:
   case tTRANIF1:
   case tRTRAN:
   case tRTRANIF0:
   case tRTRANIF1:
   case tTYPEDEF:
   case tNETTYPE:
   case tENUM:
   case tSVINT:
   case tINTEGER:
   case tSVREAL:
   case tSHORTREAL:
   case tREALTIME:
   case tTIME:
   case tTYPE:
   case tCLASS:
   case tINTERFACE:
   case tMODULE:
   case tPROGRAM:
   case tCONSTRAINT:
   case tVIRTUAL:
   case tSTATIC:
   case tAUTOMATIC:
   case tCONST:
   case tTASK:
   case tFUNCTION:
   case tLOCALPARAM:
   case tPARAMETER:
   case tEVENT:
   case tIF:
   case tCASE:
   case tFOR:
   case tGENVAR:
   case tVAR:
   case tLOGIC:
   case tBIT:
   case tSHORTINT:
   case tLONGINT:
   case tBYTE:
   case tSTRINGK: case tCHANDLE:
   case tSYSTASK:
   case tIMPORT:
   case tDEFPARAM:
   case tFINAL:
   case tASSERT:
   case tASSUME:
   case tCOVER:
   case tSEQUENCE:
   case tPROPERTY:
   case tCLOCKING:
   case tDEFAULT:
      p_module_or_generate_item(mod);
      break;
   case tSPECPARAM:
      {
         // A specparam outside a specify block is a constant like a
         // local parameter
         consume(tSPECPARAM);
         vlog_node_t dt = p_data_type_or_implicit();

         const int first = vlog_decls(mod);
         p_list_of_param_assignments(mod, dt, V_LOCALPARAM);
         for (int i = first; i < vlog_decls(mod); i++) {
            vlog_node_t d = vlog_decl(mod, i);
            vlog_set_flags(d, vlog_flags(d) | VLOG_F_SPECPARAM);
         }

         consume(tSEMI);
      }
      break;
   case tSPECIFY:
      vlog_add_stmt(mod, p_specify_block());
      break;
   case tGENERATE:
      p_generate_region(mod);
      break;
   default:
      one_of(tALWAYS, tALWAYSCOMB, tALWAYSFF, tALWAYSLATCH, tWIRE, tUWIRE,
             tSUPPLY0, tSUPPLY1, tTRI, tTRI0, tTRI1, tTRIAND, tTRIOR, tTRIREG,
             tWAND, tWOR, tINTERCONNECT, tREG, tSTRUCT, tUNION, tASSIGN,
             tPULLDOWN, tPULLUP, tID, tATTRBEGIN, tAND, tNAND, tOR, tNOR, tXOR,
             tXNOR, tNOT, tBUF, tBUFIF0, tBUFIF1, tNOTIF0, tNOTIF1, tTYPEDEF,
             tENUM, tSVINT, tINTEGER, tSVREAL, tSHORTREAL, tREALTIME, tTIME,
             tTASK, tFUNCTION, tLOCALPARAM, tPARAMETER, tEVENT, tIF, tFOR,
             tGENVAR, tVAR, tLOGIC, tBIT, tSHORTINT, tLONGINT, tBYTE, tSTRINGK,
             tIMPORT, tDEFPARAM, tFINAL, tASSERT, tASSUME, tCOVER, tSPECIFY,
             tGENERATE);
      drop_tokens_until(&state, tSEMI);
   }
}

static void p_module_item(vlog_node_t mod)
{
   // port_declaration ; | non_port_module_item

   BEGIN("module item");

   skip_over_attributes();

   if (scan(tINOUT, tINPUT, tOUTPUT)) {
      p_port_declaration(mod);
      consume(tSEMI);
   }
   else if (scan(tTIMEUNIT, tTIMEPRECISION))
      p_timeunits_declaration();
   else if (is_nonansi_interface_port(mod)) {
      p_nonansi_interface_port_declaration(mod);
      consume(tSEMI);
   }
   else
      p_non_port_module_item(mod);
}

static vlog_node_t p_modport_name(vlog_node_t iface)
{
   // The dot has been consumed

   ident_t name = p_identifier();

   if (iface == NULL)
      return NULL;   // Declared later so cannot check this

   const int ndecls = vlog_decls(iface);
   for (int i = 0; i < ndecls; i++) {
      vlog_node_t d = vlog_decl(iface, i);
      if (vlog_kind(d) == V_MODPORT && vlog_ident(d) == name)
         return d;
   }

   error_at(&state.last_loc, "interface %pQ has no modport named %pQ",
            vlog_ident2(iface), name);
   return NULL;
}

static bool is_interface_port(void)
{
   // [ interface | interface_identifier ] [ . modport_identifier ] identifier

   if (peek() == tINTERFACE)
      return true;
   else if (peek() != tID)
      return false;
   else if (peek_nth(2) == tDOT)
      return peek_nth(3) == tID && peek_nth(4) == tID;
   else if (peek_nth(2) != tID)
      return false;

   // Otherwise this is the name of a data type
   vlog_node_t d = vlog_symtab_query(symtab, peek_ident_nth(1));
   return d == NULL;
}

static int find_unresolved_port(vlog_node_t mod, ident_t id)
{
   const int nports = vlog_ports(mod);
   for (int i = 0; i < nports; i++) {
      vlog_node_t p = vlog_port(mod, i);
      if (vlog_kind(p) == V_REF && vlog_ident(p) == id && !vlog_has_ref(p))
         return i;
   }

   return -1;
}

static bool is_nonansi_interface_port(vlog_node_t mod)
{
   // interface_identifier [ . modport_identifier ] port_identifier ;

   if (peek() != tID)
      return false;

   int nth = 2;
   if (peek_nth(nth) == tDOT) {
      if (peek_nth(3) != tID)
         return false;
      nth = 4;
   }

   if (peek_nth(nth) != tID)
      return false;
   else if (lookup_interface(peek_ident_nth(1)) == NULL
            && vlog_symtab_query(symtab, peek_ident_nth(1)) != NULL)
      return false;   // Name of a data type

   return find_unresolved_port(mod, peek_ident_nth(nth)) != -1;
}

static void p_nonansi_interface_port_declaration(vlog_node_t mod)
{
   // interface_identifier [ . modport_identifier ] list_of_port_identifiers

   BEGIN("interface port declaration");

   ident_t name = p_identifier();
   scan_note_name(name);
   vlog_node_t decl = lookup_interface(name), mp = NULL;

   if (optional(tDOT))
      mp = p_modport_name(decl);

   do {
      ident_t id = p_identifier();
      const loc_t loc = state.last_loc;

      const int index = find_unresolved_port(mod, id);
      if (index == -1) {
         error_at(&loc, "'%s' does not appear in module port list", istr(id));
         continue;
      }

      // Position in the list of ports with those of other kinds
      int pos = 0, remaining = index;
      for (;; pos++) {
         bool taken = false;
         const int ndecls = vlog_decls(mod);
         for (int i = 0; i < ndecls && !taken; i++) {
            vlog_node_t d = vlog_decl(mod, i);
            taken = vlog_kind(d) == V_IFACE_PORT && vlog_subkind(d) == pos;
         }

         if (!taken && remaining-- == 0)
            break;
      }

      vlog_delete_port(mod, index);

      vlog_node_t v = vlog_new(V_IFACE_PORT);
      vlog_set_ident(v, id);
      vlog_set_ident2(v, name);
      vlog_set_loc(v, &loc);
      vlog_set_subkind(v, pos);

      while (peek() == tLSQUARE)
         vlog_add_range(v, p_unpacked_dimension());

      if (mp != NULL)
         vlog_set_ref(v, mp);

      vlog_add_decl(mod, v);
      vlog_symtab_put(symtab, v);
   } while (optional(tCOMMA));
}

static void p_interface_port_declaration(vlog_node_t mod, ident_t iface)
{
   // interface_port_header port_identifier

   BEGIN("interface port declaration");

   ident_t name = iface;
   vlog_node_t decl = NULL, mp = NULL;
   if (iface == NULL) {
      if (optional(tINTERFACE))
         name = ident_new("interface");
      else {
         name = p_identifier();
         scan_note_name(name);
         decl = lookup_interface(name);
      }

      if (optional(tDOT))
         mp = p_modport_name(decl);
   }

   vlog_node_t v = vlog_new(V_IFACE_PORT);
   vlog_set_ident(v, p_identifier());
   vlog_set_ident2(v, name);
   vlog_set_loc(v, &state.last_loc);

   while (peek() == tLSQUARE)
      vlog_add_range(v, p_unpacked_dimension());

   if (iface == NULL)
      last_modport = mp;
   else
      mp = last_modport;

   if (mp != NULL)
      vlog_set_ref(v, mp);

   // Position in the list of ports with those of other kinds
   int pos = vlog_ports(mod);
   const int ndecls = vlog_decls(mod);
   for (int i = 0; i < ndecls; i++)
      pos += vlog_kind(vlog_decl(mod, i)) == V_IFACE_PORT;

   vlog_set_subkind(v, pos);

   vlog_add_decl(mod, v);
   vlog_symtab_put(symtab, v);
}

static void p_ansi_port_declaration(vlog_node_t mod, v_port_kind_t *kind,
                                    vlog_node_t *dt, ident_t *iface)
{
   // [ net_port_header | interface_port_header ] port_identifier
   //     { unpacked_dimension } [ = constant_expression ]
   // | [ variable_port_header ] port_identifier { variable_dimension }
   //     [ = constant_expression ]
   // | [ port_direction ] . port_identifier ( [ expression ] )

   BEGIN("ANSI port declaration");

   if (is_interface_port()) {
      const bool generic = peek() == tINTERFACE;
      if (!generic)
         *iface = peek_ident_nth(1);
      else
         *iface = ident_new("interface");
      p_interface_port_declaration(mod, NULL);
      return;
   }
   else if (peek() == tID && *iface != NULL) {
      // A name alone has the same kind as the previous port
      p_interface_port_declaration(mod, *iface);
      return;
   }

   *iface = NULL;

   // A data type name or package scope can follow the direction of the
   // previous port without a direction of its own
   if (peek() != tID || peek_nth(2) == tID || peek_nth(2) == tSCOPE)
      p_net_port_header(kind, dt);
   else if (*dt == NULL)
      *dt = implicit_type();

   ident_t id, ext;
   p_external_identifier(&id, &ext);

   vlog_node_t v = vlog_new(V_PORT_DECL);
   vlog_set_subkind(v, *kind);
   vlog_set_ident(v, id);
   vlog_set_ident2(v, ext);
   vlog_set_type(v, *dt);
   vlog_set_loc(v, &state.last_loc);

   // An unpacked array of elements which are not held in signals is a
   // dynamic array with a fixed size like a variable of that type
   vlog_node_t base = vlog_get_type(*dt);
   const bool in_memory = base != NULL && vlog_is_dynamic_type(base)
      && !(vlog_kind(base) == V_DATA_TYPE
           && (vlog_subkind(base) == DT_PROCESS
               || vlog_subkind(base) == DT_CHANDLE));

   if (peek() == tLSQUARE
       && (peek_nth(2) == tRSQUARE || peek_nth(2) == tDOLLAR || in_memory))
      vlog_set_type(v, p_variable_dimensions(*dt, v));
   else {
      while (peek() == tLSQUARE)
         vlog_add_range(v, p_unpacked_dimension());
   }

   if (optional(tEQ))
      vlog_set_value(v, p_constant_expression());

   vlog_add_decl(mod, v);
   vlog_symtab_put(symtab, v);

   vlog_node_t ref = vlog_new(V_REF);
   vlog_set_loc(ref, CURRENT_LOC);
   vlog_set_ident(ref, id);
   vlog_set_ref(ref, v);

   vlog_add_port(mod, ref);
}

static void p_list_of_port_declarations(vlog_node_t mod)
{
   // ( [ { attribute_instance } ansi_port_declaration
   //   { , { attribute_instance } ansi_port_declaration } ] )

   BEGIN("list of port declarations");

   consume(tLPAREN);

   if (peek() != tRPAREN) {
      v_port_kind_t kind = V_PORT_INPUT;
      vlog_node_t dt = NULL;
      ident_t iface = NULL;
      do {
         optional_attributes();
         p_ansi_port_declaration(mod, &kind, &dt, &iface);
      } while (optional(tCOMMA));
   }

   consume(tRPAREN);
}

static void p_parameter_port_declaration(vlog_node_t mod)
{
   // parameter_declaration
   //    | local_parameter_declaration
   //    | data_type list_of_param_assignments
   //    | type list_of_type_assignments

   BEGIN("parameter port declaration");

   switch (peek()) {
   case tPARAMETER:
      p_parameter_declaration(mod);
      break;
   case tLOCALPARAM:
      p_local_parameter_declaration(mod);
      break;
   case tTYPE:
      consume(tTYPE);
      p_type_assignments(mod, true);
      break;
   default:
      {
         vlog_node_t datatype = p_data_type();
         p_list_of_param_assignments(mod, datatype, V_PARAM_DECL);
      }
      break;
   }
}

static void p_parameter_port_list(vlog_node_t mod)
{
   // # ( list_of_param_assignments { , parameter_port_declaration } )
   //    | # ( parameter_port_declaration { , parameter_port_declaration } )
   //    | # ( )

   BEGIN("parameter port list");

   consume(tHASH);
   consume(tLPAREN);

   if (peek() != tRPAREN) {
      do {
         if (peek() == tID && peek_nth(2) != tID && !is_scoped_type_start())
            p_list_of_param_assignments(mod, implicit_type(), V_PARAM_DECL);
         else
            p_parameter_port_declaration(mod);
      } while(optional(tCOMMA));
   }

   consume(tRPAREN);
}

static void p_module_ansi_header(vlog_node_t mod)
{
   // { attribute_instance } module_keyword [ lifetime ] module_identifier
   //    { package_import_declaration } [ parameter_port_list ]
   ///   [ list_of_port_declarations ] ;

   EXTEND("module ANSI header");

   if (peek() == tLPAREN)
      p_list_of_port_declarations(mod);

   consume(tSEMI);

   vlog_set_loc(mod, CURRENT_LOC);
}

static vlog_node_t p_port_reference(void)
{
   // port_identifier constant_select

   BEGIN("port reference");

   vlog_node_t v = vlog_new(V_REF);
   vlog_set_ident(v, p_identifier());
   vlog_set_loc(v, CURRENT_LOC);
   return v;
}

static vlog_node_t p_port_expression(void)
{
   // port_reference | { port_reference { , port_reference } }

   BEGIN("port expression");

   return p_port_reference();
}

static vlog_node_t p_port(void)
{
   // [ port_expression ] | . port_identifier ( [ port_expression ] )

   BEGIN("port");

   return p_port_expression();
}

static void p_list_of_ports(vlog_node_t mod)
{
   // ( port { , port } )

   BEGIN("list of ports");

   consume(tLPAREN);

   do {
      vlog_add_port(mod, p_port());
   } while (optional(tCOMMA));

   consume(tRPAREN);
}

static void p_module_nonansi_header(vlog_node_t mod)
{
   // { attribute_instance } module_keyword [ lifetime ] module_identifier
   //    { package_import_declaration } [ parameter_port_list ] list_of_ports ;

   EXTEND("module non-ANSI header");

   p_list_of_ports(mod);

   consume(tSEMI);

   vlog_set_loc(mod, CURRENT_LOC);
}

static bool is_modport_lvalue(vlog_node_t v)
{
   switch (vlog_kind(v)) {
   case V_REF:
   case V_MEMBER_REF:
      return true;
   case V_BIT_SELECT:
   case V_PART_SELECT:
      return is_modport_lvalue(vlog_value(v));
   case V_CONCAT:
      {
         const int nparams = vlog_params(v);
         for (int i = 0; i < nparams; i++) {
            if (!is_modport_lvalue(vlog_param(v, i)))
               return false;
         }

         return !vlog_has_value(v);
      }
   default:
      return false;
   }
}

static void p_modport_declaration(vlog_node_t mod)
{
   // modport modport_item { , modport_item } ;
   // modport_item ::= modport_identifier ( modport_ports_declaration
   //                                       { , modport_ports_declaration } )
   // modport_ports_declaration ::= port_direction modport_simple_port
   //     { , modport_simple_port } | import_export modport_tf_port
   //     { , modport_tf_port }
   // modport_simple_port ::= port_identifier | . port_identifier
   //     ( [ expression ] )

   BEGIN("modport declaration");

   consume(tMODPORT);

   do {
      vlog_node_t v = vlog_new(V_MODPORT);
      vlog_set_ident(v, p_identifier());
      vlog_set_loc(v, &state.last_loc);

      consume(tLPAREN);

      // The scope for the expressions is the interface
      vlog_modport_dir_t dir = V_MODPORT_INPUT;
      bool tf = false;
      do {
         optional_attributes();

         if (peek() == tCLOCKING) {
            // The items of the clocking block are members of the modport
            consume(tCLOCKING);
            const char *prefix = istr(p_identifier());
            const size_t plen = strlen(prefix);
            const int ndecls = vlog_decls(mod);
            for (int i = 0; i < ndecls; i++) {
               vlog_node_t d = vlog_decl(mod, i);
               if (!vlog_has_ident(d) || vlog_kind(d) != V_VAR_DECL
                   || strncmp(istr(vlog_ident(d)), prefix, plen) != 0
                   || istr(vlog_ident(d))[plen] != '$')
                  continue;

               vlog_node_t mp = vlog_new(V_MODPORT_PORT);
               vlog_set_subkind(mp, V_MODPORT_INOUT);
               vlog_set_ident(mp, vlog_ident(d));
               vlog_set_loc(mp, CURRENT_LOC);
               vlog_add_decl(v, mp);
            }

            continue;
         }

         switch (peek()) {
         case tINPUT:  consume(tINPUT);  dir = V_MODPORT_INPUT;  tf = false; break;
         case tOUTPUT: consume(tOUTPUT); dir = V_MODPORT_OUTPUT; tf = false; break;
         case tINOUT:  consume(tINOUT);  dir = V_MODPORT_INOUT;  tf = false; break;
         case tREF:    consume(tREF);    dir = V_MODPORT_REF;    tf = false; break;
         case tIMPORT: consume(tIMPORT); dir = V_MODPORT_IMPORT; tf = true;  break;
         case tEXPORT: consume(tEXPORT); dir = V_MODPORT_EXPORT; tf = true;  break;
         default: break;
         }

         vlog_node_t mp = vlog_new(V_MODPORT_PORT);
         vlog_set_subkind(mp, dir);

         if (tf && scan(tFUNCTION, tTASK)) {
            // The name of the prototype is the last identifier before
            // the argument list
            consume(peek());
            ident_t name = NULL;
            for (int depth = 0; peek() != tEOF; ) {
               const token_t tok = peek();
               if (depth == 0 && (tok == tCOMMA || tok == tRPAREN))
                  break;
               else if (depth == 0 && tok == tID && peek_nth(2) != tSCOPE)
                  name = peek_ident_nth(1);

               consume(tok);
               if (tok == tLPAREN)
                  depth++;
               else if (tok == tRPAREN)
                  depth--;
            }

            vlog_set_ident(mp, name ?: error_marker());
         }
         else if (!tf && optional(tDOT)) {
            vlog_set_ident(mp, p_identifier());
            consume(tLPAREN);
            if (peek() != tRPAREN) {
               vlog_node_t expr = p_expression();
               vlog_set_value(mp, expr);

               if ((dir == V_MODPORT_OUTPUT || dir == V_MODPORT_INOUT)
                   && !is_modport_lvalue(expr))
                  error_at(vlog_loc(expr), "output modport expression for %pQ "
                           "must be a net or variable", vlog_ident(mp));
            }
            consume(tRPAREN);
         }
         else
            vlog_set_ident(mp, p_identifier());

         vlog_set_loc(mp, CURRENT_LOC);
         vlog_add_decl(v, mp);
      } while (optional(tCOMMA));

      consume(tRPAREN);

      vlog_add_decl(mod, v);
   } while (optional(tCOMMA));

   consume(tSEMI);
}

typedef struct {
   hset_t         *mod_decls;
   hset_t         *shadows;
   hash_t         *map;
   hash_t         *owner;
   vlog_node_t     mod;
   A(vlog_node_t)  src;
   A(vlog_node_t)  dst;
   A(vlog_node_t)  where;
} sample_ctx_t;

static void sample_collect_owners(vlog_node_t n, hash_t *owner)
{
   // The generate block which declares each variable

   switch (vlog_kind(n)) {
   case V_GEN_BLOCK:
      {
         const int ndecls = vlog_decls(n);
         for (int i = 0; i < ndecls; i++)
            hash_put(owner, vlog_decl(n, i), n);
      }
      // Fall-through
   case V_FOR_GENERATE:
      {
         const int nstmts = vlog_stmts(n);
         for (int i = 0; i < nstmts; i++)
            sample_collect_owners(vlog_stmt(n, i), owner);
      }
      break;
   case V_IF_GENERATE:
      {
         const int nconds = vlog_conds(n);
         for (int i = 0; i < nconds; i++) {
            vlog_node_t c = vlog_cond(n, i);
            const int nstmts = vlog_stmts(c);
            for (int j = 0; j < nstmts; j++)
               sample_collect_owners(vlog_stmt(c, j), owner);
         }
      }
      break;
   default:
      break;
   }
}

static vlog_node_t sample_iface_member(vlog_node_t v)
{
   // The member of an interface instance or port whose type is known now

   vlog_node_t mr = vlog_value(v);
   if (vlog_kind(mr) != V_MOD_REF || vlog_has_value(mr) || vlog_params(mr) > 0
       || !vlog_has_ref(mr) || vlog_ident(v) == ident_new("$self"))
      return NULL;

   ident_t name = NULL;
   vlog_node_t port = vlog_ref(mr);
   switch (vlog_kind(port)) {
   case V_IFACE_PORT:
      name = vlog_ident2(port);
      break;
   case V_MOD_INST:
      if (inst_modules != NULL && vlog_ranges(port) == 0)
         name = hash_get(inst_modules, port);
      break;
   default:
      break;
   }

   if (name == NULL)
      return NULL;

   vlog_node_t iface = lookup_interface(name);
   if (iface == NULL || vlog_kind(iface) != V_MODULE
       || !(vlog_flags(iface) & VLOG_F_INTERFACE))
      return NULL;

   vlog_node_t member = NULL;
   const int ndecls = vlog_decls(iface);
   for (int i = 0; i < ndecls; i++) {
      vlog_node_t d = vlog_decl(iface, i);
      switch (vlog_kind(d)) {
      case V_PARAM_DECL:
      case V_LOCALPARAM:
         return NULL;
      case V_NET_DECL:
      case V_VAR_DECL:
         if (vlog_ident(d) == vlog_ident(v))
            member = d;
         break;
      default:
         break;
      }
   }

   if (member == NULL || !vlog_has_type(member) || vlog_ranges(member) > 0
       || (vlog_kind(member) == V_VAR_DECL && vlog_has_value(member)))
      return NULL;

   return member;
}

static vlog_node_t sample_rewrite_cb(vlog_node_t v, void *ctx)
{
   sample_ctx_t *sc = ctx;

   if (vlog_kind(v) == V_HIER_REF && !vlog_has_ref(v)) {
      vlog_node_t d = sample_iface_member(v);
      if (d == NULL)
         return v;

      ident_t key = ident_prefix(vlog_ident(vlog_value(v)), vlog_ident(v), '.');
      vlog_node_t s = hash_get(sc->map, key);
      if (s == NULL) {
         s = vlog_new(V_VAR_DECL);
         vlog_set_ident(s, ident_prefix(ident_new("$sampled"), key, '.'));
         vlog_set_type(s, vlog_type(d));
         vlog_set_loc(s, vlog_loc(v));
         vlog_add_decl(sc->mod, s);

         hash_put(sc->map, key, s);
         hset_insert(sc->shadows, s);
         APUSH(sc->src, v);
         APUSH(sc->dst, s);
         APUSH(sc->where, sc->mod);
      }

      vlog_node_t use = vlog_new(V_REF);
      vlog_set_ident(use, vlog_ident(s));
      vlog_set_ref(use, s);
      vlog_set_loc(use, vlog_loc(v));
      return use;
   }

   if (vlog_kind(v) != V_REF || !vlog_has_ref(v))
      return v;

   vlog_node_t d = vlog_ref(v);
   switch (vlog_kind(d)) {
   case V_VAR_DECL:
   case V_NET_DECL:
   case V_PORT_DECL:
      break;
   default:
      return v;
   }

   vlog_node_t where = hset_contains(sc->mod_decls, d) ? sc->mod
      : hash_get(sc->owner, d);
   if (where == NULL || hset_contains(sc->shadows, d) || !vlog_has_type(d))
      return v;

   // A queue, associative array, class handle or string has no copy
   vlog_node_t dt = vlog_get_type(vlog_type(d));
   if (dt != NULL && ((vlog_kind(dt) == V_DATA_TYPE
                       && (vlog_subkind(dt) == DT_QUEUE
                           || vlog_subkind(dt) == DT_DYNARRAY
                           || vlog_subkind(dt) == DT_ASSOC
                           || vlog_subkind(dt) == DT_STRING))
                      || vlog_kind(dt) == V_CLASS_DECL)) {
      warn_at(vlog_loc(v), "sorry, %pQ is not sampled in the Preponed region "
              "in a concurrent assertion", vlog_ident(d));
      return v;
   }

   vlog_node_t s = hash_get(sc->map, d);
   if (s == NULL) {
      s = vlog_new(V_VAR_DECL);
      vlog_set_ident(s, ident_prefix(ident_new("$sampled"), vlog_ident(d), '.'));
      vlog_set_type(s, vlog_type(d));
      vlog_set_loc(s, vlog_loc(d));

      if (vlog_kind(d) == V_PORT_DECL) {
         const int nranges = vlog_ranges(d);
         for (int i = 0; i < nranges; i++) {
            if (vlog_subkind(vlog_range(d, i)) == V_DIM_UNPACKED)
               vlog_add_range(s, vlog_range(d, i));
         }
      }

      if (vlog_kind(d) != V_PORT_DECL) {
         const int nranges = vlog_ranges(d);
         for (int i = 0; i < nranges; i++)
            vlog_add_range(s, vlog_range(d, i));

         if (nranges == 0) {
            // Until the first change it has the initial value
            vlog_node_t init = vlog_new(V_REF);
            vlog_set_ident(init, vlog_ident(d));
            vlog_set_ref(init, d);
            vlog_set_loc(init, vlog_loc(v));
            vlog_set_value(s, init);
         }
      }

      vlog_add_decl(where, s);

      hash_put(sc->map, d, s);
      hset_insert(sc->shadows, s);
      APUSH(sc->src, d);
      APUSH(sc->dst, s);
      APUSH(sc->where, where);
   }

   vlog_node_t use = vlog_new(V_REF);
   vlog_set_ident(use, vlog_ident(s));
   vlog_set_ref(use, s);
   vlog_set_loc(use, vlog_loc(v));
   return use;
}

static bool is_sampled_value_name(ident_t id)
{
   return id == ident_new("$past") || id == ident_new("$rose")
      || id == ident_new("$fell") || id == ident_new("$stable")
      || id == ident_new("$changed");
}

static vlog_node_t sample_action_cb(vlog_node_t v, void *ctx)
{
   // $sampled in an action block gives the value of its argument in the
   // Preponed region of the time step in which the assertion was
   // evaluated (16.9.3), which the sampled copies hold until the Postponed
   // region.  The other sampled value functions use the same copies.

   if (vlog_kind(v) != V_SYS_FCALL || vlog_params(v) < 1)
      return v;

   if (vlog_ident(v) == ident_new("$sampled"))
      return vlog_params(v) == 1
         ? vlog_rewrite(vlog_param(v, 0), NULL, sample_rewrite_cb, ctx) : v;
   else if (!is_sampled_value_name(vlog_ident(v)))
      return v;

   // The arguments are the expression, the number of ticks and the gating
   // expression of $past, then the clocking event
   for (int i = 0; i < vlog_params(v); i++) {
      vlog_node_t p = vlog_param(v, i);
      if (i == 1 && vlog_ident(v) == ident_new("$past"))
         continue;
      else if (vlog_kind(p) == V_EVENT_CONTROL || vlog_kind(p) == V_EMPTY)
         continue;

      vlog_set_param(v, i, vlog_rewrite(p, NULL, sample_rewrite_cb, ctx));
   }

   return v;
}

static void sample_assertions(vlog_node_t mod, int first)
{
   // A concurrent assertion uses the values its variables had in the
   // Preponed region.  These are copies updated in the Postponed region of
   // each time step in which the variable changed.

   if (module_asserts.count == first)
      return;

   static unsigned count = 0;

   sample_ctx_t sc = {
      .mod_decls = hset_new(64),
      .shadows   = hset_new(16),
      .map       = hash_new(16),
      .mod       = mod,
      .owner     = hash_new(16),
   };

   const int nmstmts = vlog_stmts(mod);
   for (int i = 0; i < nmstmts; i++)
      sample_collect_owners(vlog_stmt(mod, i), sc.owner);

   const int ndecls = vlog_decls(mod);
   for (int i = 0; i < ndecls; i++)
      hset_insert(sc.mod_decls, vlog_decl(mod, i));

   const int nports = vlog_ports(mod);
   for (int i = 0; i < nports; i++)
      hset_insert(sc.mod_decls, vlog_port(mod, i));

   for (int i = first; i < module_asserts.count; i++) {
      vlog_node_t spec = vlog_value(module_asserts.items[i]);
      if (vlog_kind(spec) != V_PROP_SPEC)
         continue;

      vlog_set_value(spec, vlog_rewrite(vlog_value(spec), NULL,
                                        sample_rewrite_cb, &sc));

      const int nconds = vlog_conds(module_asserts.items[i]);
      for (int j = 0; j < nconds; j++)
         (void)vlog_rewrite(vlog_cond(module_asserts.items[i], j), NULL,
                            sample_action_cb, &sc);
   }

   for (unsigned i = 0; i < sc.src.count; i++) {
      vlog_node_t src = sc.src.items[i], dst = sc.dst.items[i];

      vlog_node_t sref = src;
      if (vlog_kind(src) != V_HIER_REF) {
         sref = vlog_new(V_REF);
         vlog_set_ident(sref, vlog_ident(src));
         vlog_set_ref(sref, src);
         vlog_set_loc(sref, vlog_loc(src));
      }

      vlog_node_t dref = vlog_new(V_REF);
      vlog_set_ident(dref, vlog_ident(dst));
      vlog_set_ref(dref, dst);
      vlog_set_loc(dref, vlog_loc(src));

      vlog_node_t assign = vlog_new(V_BASSIGN);
      vlog_set_target(assign, dref);
      vlog_set_value(assign, sref);
      vlog_set_loc(assign, vlog_loc(src));

      vlog_node_t p = vlog_new(V_ALWAYS);
      vlog_set_subkind(p, V_ALWAYS_COMB);
      vlog_set_ident(p, ident_sprintf("$postponed_a%u", count++));
      vlog_set_loc(p, vlog_loc(src));
      vlog_add_stmt(p, assign);
      vlog_add_stmt(sc.where.items[i], p);
   }

   ACLEAR(sc.src);
   ACLEAR(sc.dst);
   ACLEAR(sc.where);
   hash_free(sc.owner);
   hset_free(sc.mod_decls);
   hset_free(sc.shadows);
   hash_free(sc.map);
}

static bool at_checker(void)
{
   return peek() == tID && peek_ident_nth(1) == ident_new("checker")
      && peek_nth(2) == tID;
}

static vlog_node_t p_module_declaration(void)
{
   // module_nonansi_header [ timeunits_declaration ] { module_item }
   //      endmodule [ : module_identifier ]
   //   | module_ansi_header [ timeunits_declaration ] { non_port_module_item }
   //      endmodule [ : module_identifier ]
   //   | { attribute_instance } module_keyword [ lifetime ] module_identifier
   //      ( .* ) ; [ timeunits_declaration ] { module_item } endmodule
   //      [ : module_identifier ]
   //   | extern module_nonansi_header
   //   | extern module_ansi_header

   BEGIN("module declaration");

   vlog_node_t mod = vlog_new(V_MODULE);

   vlog_node_t saved_module = current_module;
   current_module = mod;
   module_root = mod;

   const unsigned saved_constructs = gen_constructs;
   const unsigned saved_construct = gen_construct;
   gen_constructs = gen_construct = 0;

   const unsigned first_tick = shared_ticks.count;
   const int first_assert = module_asserts.count;

   vlog_node_t saved_clock = default_clock;
   vlog_node_t saved_disable = default_disable;
   default_clock = default_disable = NULL;

   const uint64_t saved_time_scale = time_scale;
   const unsigned saved_time_precision = time_precision_log10;
   const bool saved_time_unscaled = time_unscaled;

   optional_attributes();

   const bool is_iface = peek() == tINTERFACE;
   const bool is_checker = at_checker();
   if (is_iface) {
      consume(tINTERFACE);
      vlog_set_flags(mod, VLOG_F_INTERFACE);
   }
   else if (is_checker) {
      consume(tID);   // A checker is instantiated like a module
      vlog_set_flags(mod, VLOG_F_CHECKER);
   }
   else
      consume(tMODULE);

   const bool saved_subr = subr_automatic;
   subr_automatic = false;

   if (scan(tAUTOMATIC, tSTATIC))
      subr_automatic = p_lifetime();

   ident_t id, ext;
   p_external_identifier(&id, &ext);
   vlog_set_ident2(mod, id);

   vlog_set_loc(mod, &state.last_loc);

   ident_t qual = ident_prefix(lib_name(lib_work()), ext, '.');
   vlog_set_ident(mod, qual);

   vlog_symtab_push(symtab, mod);

   for (int i = 0; i < file_namespaces.count; i++)
      add_scope_init(mod, file_namespaces.items[i]);

   while (peek() == tIMPORT)
      p_package_import_declaration(mod);

   if (peek() == tHASH) {
      p_parameter_port_list(mod);
      param_kind = V_LOCALPARAM;
   }

   if (!is_checker && peek() == tLPAREN && peek_nth(2) == tID
       && peek_nth(3) != tID && peek_nth(3) != tDOT)
      p_module_nonansi_header(mod);
   else
      p_module_ansi_header(mod);

   if (is_checker) {
      while (peek() != tEOF && !(peek() == tID
                                 && peek_ident_nth(1) == ident_new("endchecker")))
         p_module_item(mod);

      if (peek() == tID)
         consume(tID);
      else
         expect(tID);
   }
   else if (is_iface) {
      if (interfaces == NULL)
         interfaces = hash_new(16);
      hash_put(interfaces, id, mod);

      while (not_at_token(tENDINTERFACE)) {
         if (peek() == tMODPORT)
            p_modport_declaration(mod);
         else
            p_module_item(mod);
      }

      consume(tENDINTERFACE);
   }
   else {
      while (not_at_token(tENDMODULE))
         p_module_item(mod);

      consume(tENDMODULE);
   }

   expand_user_nets(mod);

   if (optional(tCOLON)) {
      ident_t name = p_identifier();
      if (id != name)
         error_at(&state.last_loc, "'%s' does not match module name '%s'",
                  istr(name), istr(id));
   }

   flush_deferred_bodies(true);
   mark_disabled_blocks(mod);
   check_pending_disables();

   subr_automatic = saved_subr;
   add_pending_statics(mod);

   vlog_set_ival(mod, time_ival());

   time_scale = saved_time_scale;
   time_precision_log10 = saved_time_precision;
   time_unscaled = saved_time_unscaled;

   shared_ticks.count = first_tick;
   current_module = saved_module;
   gen_constructs = saved_constructs;
   gen_construct = saved_construct;
   default_clock = saved_clock;
   default_disable = saved_disable;

   vlog_symtab_pop(symtab);

   sample_assertions(mod, first_assert);
   module_asserts.count = first_assert;

   return mod;
}

static void p_udp_port_list(vlog_node_t udp)
{
   // output_port_identifier , input_port_identifier { , input_port_identifier }

   BEGIN("UDP port list");

   vlog_node_t oref = vlog_new(V_REF);
   vlog_set_ident(oref, p_identifier());
   vlog_set_loc(oref, &state.last_loc);

   vlog_add_port(udp, oref);

   consume(tCOMMA);

   vlog_node_t iref = vlog_new(V_REF);
   vlog_set_ident(iref, p_identifier());
   vlog_set_loc(iref, &state.last_loc);

   vlog_add_port(udp, iref);

   while (optional(tCOMMA)) {
      vlog_node_t iref = vlog_new(V_REF);
      vlog_set_ident(iref, p_identifier());
      vlog_set_loc(iref, &state.last_loc);

      vlog_add_port(udp, iref);
   }
}

static vlog_node_t p_udp_nonansi_declaration(void)
{
   // { attribute_instance } primitive udp_identifier ( udp_port_list ) ;

   BEGIN("UDP non-ANSI declaration");

   vlog_node_t udp = vlog_new(V_PRIMITIVE);

   consume(tPRIMITIVE);

   ident_t id, ext;
   p_external_identifier(&id, &ext);
   vlog_set_ident2(udp, id);

   ident_t qual = ident_prefix(lib_name(lib_work()), ext, '.');
   vlog_set_ident(udp, qual);

   consume(tLPAREN);

   p_udp_port_list(udp);

   consume(tRPAREN);
   consume(tSEMI);

   vlog_set_loc(udp, CURRENT_LOC);
   return udp;
}

static void p_list_of_udp_port_identifiers(vlog_node_t udp, v_port_kind_t kind)
{
   // port_identifier { , port_identifier }

   BEGIN("list of UDP port identifiers");

   do {
      ident_t id, ext;
      p_external_identifier(&id, &ext);

      vlog_node_t p = vlog_new(V_PORT_DECL);
      vlog_set_subkind(p, kind);
      vlog_set_ident(p, id);
      vlog_set_ident2(p, ext);
      vlog_set_type(p, implicit_type());
      vlog_set_loc(p, &state.last_loc);

      vlog_add_decl(udp, p);
      vlog_symtab_put(symtab, p);
   } while (peek_nth(2) == tID && optional(tCOMMA));
}

static void p_udp_output_declaration(vlog_node_t udp, bool *has_reg)
{
   // { attribute_instance } output port_identifier
   //    | { attribute_instance } output reg port_identifier
   //         [ = constant_expression ]

   BEGIN("UDP output declaration");

   consume(tOUTPUT);

   const bool isreg = optional(tREG);

   ident_t id, ext;
   p_external_identifier(&id, &ext);

   vlog_node_t v = vlog_new(V_PORT_DECL);
   vlog_set_subkind(v, V_PORT_OUTPUT);
   vlog_set_ident(v, id);
   vlog_set_ident2(v, ext);
   vlog_set_loc(v, &state.last_loc);

   if (isreg) {
      vlog_set_type(v, logic_type());
      *has_reg = true;
   }
   else
      vlog_set_type(v, implicit_type());

   vlog_add_decl(udp, v);
   vlog_symtab_put(symtab, v);
}

static void p_udp_input_declaration(vlog_node_t udp)
{
   // { attribute_instance } input list_of_udp_port_identifiers

   BEGIN("UDP input declaration");

   consume(tINPUT);

   p_list_of_udp_port_identifiers(udp, V_PORT_INPUT);
}

static vlog_node_t p_udp_reg_declaration(void)
{
   // { attribute_instance } reg variable_identifier

   BEGIN("UDP reg declaration");

   consume(tREG);

   ident_t id = p_identifier();

   vlog_node_t reg = vlog_new(V_VAR_DECL);
   vlog_set_loc(reg, &state.last_loc);
   vlog_set_ident(reg, id);
   vlog_set_type(reg, logic_type());

   return reg;
}

static void p_udp_port_declaration(vlog_node_t udp, bool *has_reg)
{
   // udp_output_declaration ; | udp_input_declaration ; | udp_reg_declaration ;

   BEGIN("UDP port declaration");

   switch (peek()) {
   case tOUTPUT:
      p_udp_output_declaration(udp, has_reg);
      break;
   case tINPUT:
      p_udp_input_declaration(udp);
      break;
   case tREG:
      {
         vlog_node_t v = p_udp_reg_declaration();
         vlog_add_decl(udp, v);
         vlog_symtab_put(symtab, v);
         *has_reg = true;
      }
      break;
   default:
      one_of(tOUTPUT, tINPUT, tREG);
      break;
   }

   consume(tSEMI);
}

static void p_udp_declaration_port_list(vlog_node_t udp, bool *has_reg)
{
   // udp_output_declaration , udp_input_declaration { , udp_input_declaration }

   BEGIN("UDP declaration port list");

   p_udp_output_declaration(udp, has_reg);

   consume(tCOMMA);

   do {
      p_udp_input_declaration(udp);
   } while (optional(tCOMMA));

   const int ndecls = vlog_decls(udp);
   for (int i = 0; i < ndecls; i++) {
      vlog_node_t p = vlog_decl(udp, i);
      if (vlog_kind(p) != V_PORT_DECL)
         continue;

      vlog_node_t ref = vlog_new(V_REF);
      vlog_set_loc(ref, vlog_loc(p));
      vlog_set_ident(ref, vlog_ident(p));
      vlog_set_ref(ref, p);

      vlog_add_port(udp, ref);
   }
}

static vlog_node_t p_output_symbol(void)
{
   // 0 | 1 | x | X

   BEGIN("output symbol");

   vlog_node_t v = vlog_new(V_UDP_LEVEL);
   vlog_set_subkind(v, V_UDP_SYMBOL_OUTPUT);

   if (consume(tUDPLEVEL)) {
      switch (state.last_lval.i64) {
      case '0':
      case '1':
      case 'x':
      case 'X':
         vlog_set_ival(v, state.last_lval.i64);
         break;
      default:
         parse_error(&state.last_loc, "'%c' is not a valid output symbol",
                     (char)state.last_lval.i64);
         break;
      }
   }

   vlog_set_loc(v, CURRENT_LOC);
   return v;
}

static vlog_node_t p_level_symbol(void)
{
   // 0 | 1 | x | X | ? | b | B

   BEGIN("level symbol");

   consume(tUDPLEVEL);

   vlog_node_t v = vlog_new(V_UDP_LEVEL);
   vlog_set_subkind(v, V_UDP_SYMBOL_INPUT);
   vlog_set_ival(v, state.last_lval.i64);

   vlog_set_loc(v, CURRENT_LOC);
   return v;
}

static vlog_node_t p_next_state(void)
{
   // output_symbol | -

   BEGIN("next state");

   switch (peek()) {
   case tMINUS:
      consume(tMINUS);
      break;
   case tUDPLEVEL:
      return p_output_symbol();
   default:
      one_of(tUDPLEVEL, tMINUS);
   }

   vlog_node_t v = vlog_new(V_UDP_LEVEL);
   vlog_set_subkind(v, V_UDP_SYMBOL_OUTPUT);
   vlog_set_ival(v, '-');

   vlog_set_loc(v, CURRENT_LOC);
   return v;
}

static vlog_node_t p_edge_symbol(void)
{
   // r | R | f | F | p | P | n | N | *

   BEGIN("edge symbol");

   consume(tUDPEDGE);

   char left, right;
   switch (state.last_lval.i64) {
   case 'r': case 'R': left = '0'; right = '1'; break;
   case 'f': case 'F': left = '1'; right = '0'; break;
   case 'p': case 'P': left = '?'; right = '1'; break;
   case 'n': case 'N': left = '?'; right = '0'; break;
   case '*':           left = '?'; right = '?'; break;
   default: should_not_reach_here();
   }

   vlog_node_t lsym = vlog_new(V_UDP_LEVEL);
   vlog_set_ival(lsym, left);

   vlog_node_t rsym = vlog_new(V_UDP_LEVEL);
   vlog_set_ival(rsym, right);

   vlog_node_t v = vlog_new(V_UDP_EDGE);
   vlog_set_subkind(v, V_UDP_SYMBOL_INPUT);
   vlog_set_left(v, lsym);
   vlog_set_right(v, rsym);

   vlog_set_loc(v, CURRENT_LOC);
   return v;
}

static void p_level_input_list(vlog_node_t entry)
{
   // level_symbol { level_symbol }

   BEGIN("level input list");

   do {
      vlog_add_param(entry, p_level_symbol());
   } while (not_at_token(tCOLON));
}

static vlog_node_t p_edge_indicator(void)
{
   // ( level_symbol level_symbol ) | edge_symbol

   BEGIN("edge indicator");

   switch (peek()) {
   case tUDPEDGE:
      return p_edge_symbol();
   case tLPAREN:
      {
         consume(tLPAREN);

         vlog_node_t v = vlog_new(V_UDP_EDGE);
         vlog_set_left(v, p_level_symbol());
         vlog_set_right(v, p_level_symbol());

         consume(tRPAREN);

         vlog_set_loc(v, CURRENT_LOC);
         return v;
      }
      break;
   default:
      should_not_reach_here();
   }
}

static void p_seq_input_list(vlog_node_t entry)
{
   // level_input_list | edge_input_list

   BEGIN("sequential input list");

   bool have_edge = false;
   do {
      switch (peek()) {
      case tUDPEDGE:
      case tLPAREN:
         vlog_add_param(entry, p_edge_indicator());
         if (have_edge)
            parse_error(&state.last_loc, "a sequential input list may have at "
                        "most one edge indicator");
         have_edge = true;
         break;

      case tUDPLEVEL:
         vlog_add_param(entry, p_level_symbol());
         break;

      default:
         one_of(tUDPEDGE, tUDPIND, tUDPLEVEL);
         break;
      }
   } while (not_at_token(tCOLON));
}

static vlog_node_t p_combinational_entry(void)
{
   // level_input_list : output_symbol ;

   BEGIN("combinational entry");

   vlog_node_t v = vlog_new(V_UDP_ENTRY);
   p_level_input_list(v);

   consume(tCOLON);

   vlog_add_param(v, p_output_symbol());

   consume(tSEMI);

   vlog_set_loc(v, CURRENT_LOC);
   return v;
}

static vlog_node_t p_combinational_body(void)
{
   // table combinational_entry { combinational_entry } endtable

   BEGIN("combinational UDP body");

   consume(tTABLE);

   scan_as_udp();

   vlog_node_t v = vlog_new(V_UDP_TABLE);
   vlog_set_subkind(v, V_UDP_COMB);
   vlog_set_ident(v, ident_new("combinational"));

   do {
      vlog_add_param(v, p_combinational_entry());
   } while (not_at_token(tENDTABLE));

   scan_as_verilog();

   consume(tENDTABLE);

   vlog_set_loc(v, CURRENT_LOC);
   return v;
}

static vlog_node_t p_sequential_entry(void)
{
   // seq_input_list : current_state : next_state ;

   BEGIN("sequential entry");

   vlog_node_t v = vlog_new(V_UDP_ENTRY);
   p_seq_input_list(v);

   consume(tCOLON);

   vlog_add_param(v, p_level_symbol());

   consume(tCOLON);

   vlog_add_param(v, p_next_state());

   consume(tSEMI);

   vlog_set_loc(v, CURRENT_LOC);
   return v;
}

static vlog_node_t p_udp_initial_statement(void)
{
   // initial output_port_identifier = init_val ;

   BEGIN("UDP initial statement");

   consume(tINITIAL);

   vlog_node_t ref = vlog_new(V_REF);
   vlog_set_ident(ref, p_identifier());
   vlog_set_loc(ref, &state.last_loc);

   vlog_symtab_lookup(symtab, ref);

   consume(tEQ);

   vlog_node_t v = vlog_new(V_BASSIGN);
   vlog_set_target(v, ref);
   vlog_set_value(v, p_integral_number());

   consume(tSEMI);

   vlog_set_loc(v, CURRENT_LOC);
   return v;
}

static vlog_node_t p_sequential_body(void)
{
   // [ udp_initial_statement ] table sequential_entry
   //     { sequential_entry } endtable

   BEGIN("sequential UDP body");

   vlog_node_t v = vlog_new(V_UDP_TABLE);
   vlog_set_subkind(v, V_UDP_SEQ);
   vlog_set_ident(v, ident_new("sequential"));

   if (peek() == tINITIAL)
      vlog_add_stmt(v, p_udp_initial_statement());

   consume(tTABLE);

   scan_as_udp();

   do {
      vlog_add_param(v, p_sequential_entry());
   } while (not_at_token(tENDTABLE));

   scan_as_verilog();

   consume(tENDTABLE);

   vlog_set_loc(v, CURRENT_LOC);
   return v;
}

static vlog_node_t p_udp_body(bool has_reg)
{
   // combinational_body | sequential_body

   BEGIN("UDP body");

   if (has_reg)
      return p_sequential_body();
   else
      return p_combinational_body();
}

static vlog_node_t p_udp_ansi_declaration(bool *has_reg)
{
   // { attribute_instance } primitive udp_identifier
   //    ( udp_declaration_port_list ) ;

   BEGIN("UDP ANSI declaration");

   optional_attributes();

   vlog_node_t udp = vlog_new(V_PRIMITIVE);

   consume(tPRIMITIVE);

   ident_t id, ext;
   p_external_identifier(&id, &ext);
   vlog_set_ident2(udp, id);

   ident_t qual = ident_prefix(lib_name(lib_work()), ext, '.');
   vlog_set_ident(udp, qual);

   vlog_symtab_push(symtab, udp);

   consume(tLPAREN);

   p_udp_declaration_port_list(udp, has_reg);

   consume(tRPAREN);
   consume(tSEMI);

   vlog_set_loc(udp, CURRENT_LOC);
   return udp;
}

static vlog_node_t p_udp_declaration(void)
{
   // udp_nonansi_declaration udp_port_declaration { udp_port_declaration }
   //        udp_body endprimitive [ : udp_identifier ]
   //   | udp_ansi_declaration udp_body endprimitive [ : udp_identifier ]
   //   | extern udp_nonansi_declaration
   //   | extern udp_ansi_declaration
   //   | { attribute_instance } primitive udp_identifier ( .* ) ;
   //        { udp_port_declaration } udp_body endprimitive [ : udp_identifier ]

   BEGIN("UDP declaration");

   bool has_reg = false;
   vlog_node_t udp;
   if (peek_nth(4) == tID) {
      udp = p_udp_nonansi_declaration();

      vlog_symtab_push(symtab, udp);

      do {
         p_udp_port_declaration(udp, &has_reg);
      } while (not_at_token(tTABLE, tINITIAL));

      const int nports = vlog_ports(udp);
      for (int i = 0; i < nports; i++)
         vlog_symtab_lookup(symtab, vlog_port(udp, i));
   }
   else
      udp = p_udp_ansi_declaration(&has_reg);

   vlog_add_stmt(udp, p_udp_body(has_reg));

   vlog_symtab_pop(symtab);

   consume(tENDPRIMITIVE);

   return udp;
}

static void p_package_item(vlog_node_t parent)
{
   // package_or_generate_item_declaration | anonymous_program
   //   | package_export_declaration | timeunits_declaration

   BEGIN("package item");

   if (at_covergroup()) {
      p_covergroup_declaration(parent);
      return;
   }
   else if (scan(tTIMEUNIT, tTIMEPRECISION)) {
      p_timeunits_declaration();
      return;
   }

   p_package_or_generate_item_declaration(parent);
}

static vlog_node_t p_package_declaration(void)
{
   // { attribute_instance } package [ lifetime ] package_identifier ;
   //    [ timeunits_declaration ] { { attribute_instance } package_item }
   // endpackage [ : package_identifier ]

   BEGIN("package declaration");

   consume(tPACKAGE);

   param_kind = V_LOCALPARAM;

   const bool saved_subr = subr_automatic;
   subr_automatic = false;

   if (scan(tSTATIC, tAUTOMATIC))
      subr_automatic = p_lifetime();

   vlog_node_t v = vlog_new(V_PACKAGE);

   const uint64_t saved_time_scale = time_scale;
   const unsigned saved_time_precision = time_precision_log10;
   const bool saved_time_unscaled = time_unscaled;

   ident_t id, ext;
   p_external_identifier(&id, &ext);
   vlog_set_ident2(v, id);

   ident_t qual = ident_prefix(lib_name(lib_work()), ext, '.');
   vlog_set_ident(v, qual);

   consume(tSEMI);

   vlog_symtab_push(symtab, v);

   vlog_node_t saved_package = current_package;
   current_package = v;

   while (not_at_token(tENDPACKAGE)) {
      optional_attributes();
      p_package_item(v);
   }

   flush_deferred_bodies(true);

   current_package = saved_package;

   vlog_symtab_pop(symtab);

   consume(tENDPACKAGE);

   vlog_set_ival(v, time_ival());

   time_scale = saved_time_scale;
   time_precision_log10 = saved_time_precision;
   time_unscaled = saved_time_unscaled;

   set_decl_scopes(v, qual);

   if (optional(tCOLON)) {
      ident_t name = p_identifier();
      if (id != name)
         error_at(&state.last_loc, "'%s' does not match package name '%s'",
                  istr(name), istr(id));
   }

   subr_automatic = saved_subr;
   add_pending_statics(v);

   vlog_set_loc(v, CURRENT_LOC);
   return v;
}

static void p_list_of_port_declarations(vlog_node_t mod);

static vlog_node_t p_program_nonansi_header(void)
{
   // { attribute_instance } program [ lifetime ] program_identifier
   //    { package_import_declaration } [ parameter_port_list ] list_of_ports ;

   BEGIN("program non-ANSI header");

   optional_attributes();

   vlog_node_t v = vlog_new(V_PROGRAM);

   consume(tPROGRAM);

   if (scan(tSTATIC, tAUTOMATIC))
      subr_automatic = p_lifetime();

   ident_t id, ext;
   p_external_identifier(&id, &ext);
   vlog_set_ident2(v, id);

   ident_t qual = ident_prefix(lib_name(lib_work()), ext, '.');
   vlog_set_ident(v, qual);

   vlog_symtab_push(symtab, v);

   while (peek() == tIMPORT)
      p_package_import_declaration(v);

   if (peek() == tHASH) {
      p_parameter_port_list(v);
      param_kind = V_LOCALPARAM;
   }

   if (peek() == tLPAREN)
      p_list_of_port_declarations(v);

   consume(tSEMI);

   vlog_set_loc(v, CURRENT_LOC);
   return v;
}

static vlog_node_t program_marker(const char *name, const loc_t *loc)
{
   vlog_node_t call = vlog_new(V_SYS_TCALL);
   vlog_set_ident(call, ident_new(name));
   vlog_set_subkind(call, V_SYSTF_NONE);
   vlog_set_ival(call, 0);
   vlog_set_loc(call, loc);
   return call;
}

static vlog_node_t p_program_initial(void)
{
   // The simulation ends when all the initial blocks of programs complete

   vlog_node_t init = p_initial_construct();

   const loc_t *loc = vlog_loc(init);
   vlog_node_t v = vlog_new(V_INITIAL);
   vlog_set_ident(v, vlog_ident(init));
   vlog_set_loc(v, loc);

   vlog_add_stmt(v, program_marker("$__program_begin", loc));

   const int nstmts = vlog_stmts(init);
   for (int i = 0; i < nstmts; i++)
      vlog_add_stmt(v, vlog_stmt(init, i));

   vlog_add_stmt(v, program_marker("$__program_end", loc));
   return v;
}

static void p_non_port_program_item(vlog_node_t parent)
{
   // { attribute_instance } continuous_assign
   //   | { attribute_instance } module_or_generate_item_declaration
   //   | { attribute_instance } initial_construct
   //   | { attribute_instance } final_construct
   //   | { attribute_instance } concurrent_assertion_item
   //   | timeunits_declaration
   //   | program_generate_item

   BEGIN("non-port program item");

   optional_attributes();

   switch (peek()) {
   case tINITIAL:
      vlog_add_stmt(parent, p_program_initial());
      break;
   case tFINAL:
      vlog_add_stmt(parent, p_final_construct());
      break;
   case tASSERT:
   case tASSUME:
   case tCOVER:
      {
         vlog_node_t a = p_assertion_item();
         if (a != NULL)
            vlog_add_stmt(parent, a);
      }
      break;
   case tTIMEUNIT:
   case tTIMEPRECISION:
      p_timeunits_declaration();
      break;
   default:
      p_module_or_generate_item_declaration(parent);
      break;
   }
}


static bool at_config_word(const char *word)
{
   return peek() == tID && peek_ident_nth(1) == ident_new(word);
}

static void config_unexpected(const char *expect)
{
   error_at(&state.tokenq[state.tokenq_tail].loc, "unexpected %s while parsing "
            "configuration declaration, expecting %s", token_str(peek()),
            expect);
   state.n_correct = 0;
}

static ident_t p_config_cell_name(void)
{
   // [ library_identifier . ] cell_identifier

   ident_t id = p_identifier();
   if (optional(tDOT))
      return ident_prefix(id, p_identifier(), '.');

   return id;
}

static void p_config_liblist(void)
{
   // liblist { library_identifier }

   consume(tID);
   while (peek() == tID || peek() == tDOT)
      consume(peek());
}

static void p_config_use_clause(vlog_node_t rule)
{
   // use [ library_identifier . ] cell_identifier [ : config ]
   //   | use named_parameter_assignment { , named_parameter_assignment }
   //   | use [ library_identifier . ] cell_identifier
   //       named_parameter_assignment { , named_parameter_assignment }

   consume(tID);

   if (peek() == tID)
      vlog_set_ident2(rule, p_config_cell_name());

   if (peek() == tHASH)
      p_parameter_value_assignment(rule);
   else if (peek() == tDOT)
      p_list_of_parameter_assignments(rule);

   // The value of a parameter is a constant of the configuration
   const int nparams = vlog_params(rule);
   for (int i = 0; i < nparams; i++) {
      vlog_node_t p = vlog_param(rule, i);
      int64_t value;
      if (vlog_kind(p) == V_PARAM_ASSIGN && vlog_has_value(p)
          && vlog_kind(vlog_value(p)) != V_STRING
          && vlog_get_const(vlog_value(p), &value)) {
         vlog_node_t n = vlog_new(V_NUMBER);
         vlog_set_number(n, number_from_int(value));
         vlog_set_loc(n, vlog_loc(vlog_value(p)));
         vlog_set_value(p, n);
      }
   }

   if (optional(tCOLON))
      consume(tID);   // config
}

static vlog_node_t p_config_declaration(void)
{
   // config config_identifier ; { local_parameter_declaration ; }
   //   design_statement { config_rule_statement } endconfig
   //   [ : config_identifier ]

   BEGIN("configuration declaration");

   consume(tID);

   vlog_node_t v = vlog_new(V_CONFIG);

   ident_t id, ext;
   p_external_identifier(&id, &ext);
   vlog_set_ident2(v, id);
   vlog_set_ident(v, ident_prefix(lib_name(lib_work()), ext, '.'));
   vlog_set_loc(v, &state.last_loc);

   consume(tSEMI);

   vlog_symtab_push(symtab, v);

   while (peek() == tLOCALPARAM) {
      p_local_parameter_declaration(v);
      consume(tSEMI);
   }

   while (peek() == tPARAMETER) {
      config_unexpected("design");   // Only local parameters
      drop_tokens_until(&state, tSEMI);
   }

   if (at_config_word("design")) {
      consume(tID);

      while (peek() == tID) {
         vlog_node_t cell = vlog_new(V_CONFIG_RULE);
         vlog_set_subkind(cell, V_CONFIG_CELL);
         vlog_set_ident(cell, p_config_cell_name());
         vlog_set_loc(cell, &state.last_loc);
         vlog_add_param(v, cell);
      }

      consume(tSEMI);
   }
   else
      config_unexpected("design");

   bool have_default = false;
   while (peek() != tEOF && !at_config_word("endconfig")) {
      vlog_node_t rule = vlog_new(V_CONFIG_RULE);

      if (peek() == tDEFAULT) {
         consume(tDEFAULT);
         vlog_set_subkind(rule, V_CONFIG_DEFAULT);
         vlog_set_ident(rule, ident_new("default"));
         vlog_set_loc(rule, &state.last_loc);

         if (have_default)
            error_at(&state.last_loc, "configuration '%s' has more than one "
                     "default clause", istr(vlog_ident2(v)));

         have_default = true;
      }
      else if (at_config_word("instance")) {
         consume(tID);

         LOCAL_TEXT_BUF tb = tb_new();
         ident_t first = p_identifier();
         const loc_t loc = state.last_loc;
         tb_istr(tb, first);
         while (optional(tDOT)) {
            tb_append(tb, '.');
            tb_istr(tb, p_identifier());
         }

         vlog_set_subkind(rule, V_CONFIG_INSTANCE);
         vlog_set_ident(rule, ident_new(tb_get(tb)));
         vlog_set_loc(rule, &loc);

         bool found = false;
         const int ncells = vlog_params(v);
         for (int i = 0; i < ncells; i++) {
            const char *cell = istr(vlog_ident(vlog_param(v, i)));
            const char *dot = strchr(cell, '.');
            found |= ident_new(dot ? dot + 1 : cell) == first;
         }

         if (!found)
            error_at(&loc, "'%s' is not a top-level cell of configuration "
                     "'%s'", istr(first), istr(vlog_ident2(v)));
      }
      else if (at_config_word("cell")) {
         consume(tID);
         vlog_set_subkind(rule, V_CONFIG_CELL);
         vlog_set_ident(rule, p_config_cell_name());
         vlog_set_loc(rule, &state.last_loc);
      }
      else {
         config_unexpected("default, instance, cell or endconfig");
         drop_tokens_until(&state, tSEMI);
         continue;
      }

      if (at_config_word("liblist"))
         p_config_liblist();
      else if (at_config_word("use")) {
         p_config_use_clause(rule);
         if (vlog_subkind(rule) != V_CONFIG_DEFAULT)
            vlog_add_stmt(v, rule);
      }
      else
         config_unexpected("liblist or use");

      consume(tSEMI);
   }

   vlog_symtab_pop(symtab);

   consume(tID);   // endconfig

   if (optional(tCOLON)) {
      ident_t end_name = p_identifier();
      if (end_name != vlog_ident2(v))
         error_at(&state.last_loc, "'%s' does not match configuration name "
                  "'%s'", istr(end_name), istr(vlog_ident2(v)));
   }

   vlog_set_loc(v, CURRENT_LOC);
   return v;
}

static vlog_node_t p_program_declaration(void)
{
   // program_nonansi_header [ timeunits_declaration ] { program_item }
   //       endprogram [ : program_identifier ]
   //   | program_ansi_header [ timeunits_declaration ]
   //       { non_port_program_item }
   //   | { attribute_instance } program program_identifier ( .* ) ;
   //       [ timeunits_declaration ] { program_item } endprogram
   //       [ : program_identifier ]
   //   | extern program_nonansi_header
   //   | extern program_ansi_header

   BEGIN("program declaration");

   const uint64_t saved_time_scale = time_scale;
   const unsigned saved_time_precision = time_precision_log10;
   const bool saved_time_unscaled = time_unscaled;
   const bool saved_subr = subr_automatic;

   vlog_node_t v = p_program_nonansi_header();

   while (not_at_token(tENDPROGRAM))
      p_non_port_program_item(v);

   consume(tENDPROGRAM);

   vlog_symtab_pop(symtab);

   subr_automatic = saved_subr;

   vlog_set_ival(v, time_ival());

   time_scale = saved_time_scale;
   time_precision_log10 = saved_time_precision;
   time_unscaled = saved_time_unscaled;

   if (optional(tCOLON)) {
      ident_t name = p_identifier();
      if (name != vlog_ident2(v))
         parse_error(&state.last_loc, "'%s' does not match program name '%s'",
                     istr(name), istr(vlog_ident2(v)));
   }

   vlog_set_loc(v, CURRENT_LOC);
   return v;
}

static void p_directive_list(void);

static bool at_namespace_item(void)
{
   switch (peek()) {
   case tINTERFACE:
      return peek_nth(2) == tCLASS;
   case tCONSTRAINT:
      return true;
   case tCLASS:    case tVIRTUAL:  case tTYPEDEF:   case tNETTYPE:   case tIMPORT:
   case tPARAMETER: case tLOCALPARAM: case tFUNCTION: case tTASK:
   case tSTATIC:   case tAUTOMATIC: case tCONST:    case tVAR:
   case tEVENT:    case tREG:      case tLOGIC:     case tBIT:
   case tSTRUCT:   case tUNION:    case tENUM:      case tSTRINGK: case tCHANDLE:
   case tSVINT:    case tINTEGER:  case tLONGINT:   case tSHORTINT:
   case tBYTE:     case tTIME:     case tSVREAL:    case tSHORTREAL:
   case tREALTIME: case tSEMI:
      return true;
   case tID:
      {
         if (at_covergroup())
            return true;

         vlog_node_t d = peek_reference();
         if (d == NULL && (peek_nth(2) == tID || peek_nth(2) == tHASH)) {
            const ident_t id = peek_ident_nth(1);
            return id == ident_new("semaphore") || id == ident_new("mailbox");
         }

         return d != NULL && is_data_type(d);
      }
   default:
      return false;
   }
}

static void set_decl_scopes_in(vlog_node_t container, ident_t prefix,
                               ident_t unit)
{
   // Subprograms and classes declared outside of any module live in a
   // lowered package with this name

   const int ndecls = vlog_decls(container);
   for (int i = 0; i < ndecls; i++) {
      vlog_node_t d = vlog_decl(container, i);
      switch (vlog_kind(d)) {
      case V_FUNC_DECL:
      case V_TASK_DECL:
      case V_CONSTRUCTOR:
         vlog_set_ident2(d, prefix);
         vlog_set_flags(d, vlog_flags(d) | VLOG_F_GLOBAL);
         break;
      case V_CLASS_DECL:
         if (vlog_flags(d) & VLOG_F_SPEC)
            break;   // Has its own scope
         vlog_set_ident2(d, prefix);
         vlog_set_flags(d, vlog_flags(d) | VLOG_F_GLOBAL);
         if (unit == NULL)
            unit = vlog_kind(container) == V_CLASS_DECL
               ? ident_runtil(prefix, '.') : prefix;
         set_decl_scopes_in(d, ident_prefix(prefix, vlog_ident(d), '.'), unit);
         break;
      case V_VAR_DECL:
         // Outside any module, or static in a class outside any module
         if (vlog_kind(container) != V_CLASS_DECL)
            vlog_set_ident2(d, prefix);
         else if (vlog_flags(d) & VLOG_F_STATIC)
            vlog_set_ident2(d, unit != NULL ? unit
                            : ident_runtil(prefix, '.'));
         break;
      default:
         break;
      }
   }
}

static void set_decl_scopes(vlog_node_t container, ident_t prefix)
{
   set_decl_scopes_in(container, prefix, NULL);
}

static bool p_repeated_design_unit(void)
{
   // A header with no include guard may define the same unit more than
   // once: keep the first definition as other units already refer to it

   const int start = peek();
   if (start != tMODULE && (start != tINTERFACE || peek_nth(2) == tCLASS))
      return false;

   (void)peek_nth(1);

   const loc_t *loc = &(state.tokenq[state.tokenq_tail].loc);
   ident_t key = ident_sprintf("%u:%u:%u", loc->file_ref, loc->first_line,
                               loc->first_column);

   if (seen_units == NULL)
      seen_units = hset_new(16);

   if (!hset_contains(seen_units, key)) {
      hset_insert(seen_units, key);
      return false;
   }

   const int end = start == tMODULE ? tENDMODULE : tENDINTERFACE;
   for (int depth = 0;;) {
      const int tok = peek();
      if (tok == tEOF)
         return true;

      depth += tok == start ? 1 : tok == end ? -1 : 0;

      if (consume(tok)) {
         // These tokens own storage which is normally freed when parsed
         switch (tok) {
         case tSTRING:
            tb_free(state.last_lval.text);
            break;
         case tNUMBER:
         case tUNSNUM:
         case tTIMELIT:
            free(state.last_lval.str);
            break;
         default:
            break;
         }
      }

      if (depth == 0)
         break;
   }

   if (optional(tCOLON))
      (void)p_identifier();

   return true;
}

static vlog_node_t bind_find_child(vlog_node_t scope, ident_t id)
{
   // An instance named id or a generate block named id in the statements
   // of scope, looking through if-generate branches; the result is the
   // module of the instance or the generate block

   const int nstmts = vlog_stmts(scope);
   for (int i = 0; i < nstmts; i++) {
      vlog_node_t s = vlog_stmt(scope, i);
      switch (vlog_kind(s)) {
      case V_INST_LIST:
         {
            const int ninst = vlog_stmts(s);
            for (int j = 0; j < ninst; j++) {
               vlog_node_t inst = vlog_stmt(s, j);
               if (vlog_kind(inst) == V_MOD_INST && vlog_ident(inst) == id) {
                  vlog_node_t next = lookup_interface(vlog_ident(s));
                  return next != NULL && vlog_kind(next) == V_MODULE
                     ? next : NULL;
               }
            }
         }
         break;

      case V_GEN_BLOCK:
         if (vlog_has_ident(s) && vlog_ident(s) == id)
            return s;
         break;

      case V_IF_GENERATE:
         {
            const int nconds = vlog_conds(s);
            for (int j = 0; j < nconds; j++) {
               vlog_node_t c = vlog_cond(s, j);
               if (vlog_stmts(c) == 0)
                  continue;

               vlog_node_t blk = vlog_stmt(c, 0);
               if (vlog_kind(blk) == V_GEN_BLOCK) {
                  if (vlog_has_ident(blk) && vlog_ident(blk) == id)
                     return blk;

                  vlog_node_t r = bind_find_child(blk, id);
                  if (r != NULL)
                     return r;
               }
            }
         }
         break;

      default:
         break;
      }
   }

   return NULL;
}

static vlog_node_t bind_child_module(vlog_node_t mod, ident_t id)
{
   vlog_node_t next = bind_find_child(mod, id);
   return next != NULL && vlog_kind(next) == V_MODULE ? next : NULL;
}

static vlog_node_t bind_walk_path(vlog_node_t mod, text_buf_t *path)
{
   // The module of the instance named by the identifiers after the dots,
   // which are appended to the path

   while (optional(tDOT)) {
      ident_t id = p_identifier();
      tb_append(path, '.');
      tb_istr(path, id);

      vlog_node_t next = bind_find_child(mod, id);
      if (next == NULL) {
         error_at(&state.last_loc, "cannot find instance %pI in module %pI "
                  "to bind into", id, vlog_ident(mod));
         return NULL;
      }

      mod = next;
   }

   return mod;
}

static int next_bind = 0;

static vlog_node_t p_bind_directive(void)
{
   // bind bind_target_scope [ : bind_target_instance_list ]
   //    bind_instantiation ;

   BEGIN("bind directive");

   consume(tID);

   vlog_node_t v = vlog_new(V_BIND);

   const loc_t target_loc = *CURRENT_LOC;
   ident_t target = p_identifier();

   // The first name may be an instance in the module with the directive
   vlog_node_t mod = NULL, scope_mod = NULL;
   if (peek() == tDOT && bind_scope != NULL) {
      scope_mod = lookup_interface(bind_scope);
      if (scope_mod != NULL && vlog_kind(scope_mod) == V_MODULE)
         mod = bind_child_module(scope_mod, target);
   }

   bool relative = mod != NULL;
   if (!relative)
      mod = lookup_interface(target);

   if (mod == NULL && peek() != tDOT && peek() != tCOLON && bind_scope != NULL) {
      // The target is an instance in the module with the directive (23.11)
      scope_mod = lookup_interface(bind_scope);
      if (scope_mod != NULL && vlog_kind(scope_mod) == V_MODULE)
         mod = bind_child_module(scope_mod, target);

      if (mod != NULL) {
         LOCAL_TEXT_BUF path = tb_new();
         tb_istr(path, bind_scope);
         tb_append(path, '.');
         tb_istr(path, target);

         vlog_node_t sel = vlog_new(V_STRING);
         vlog_set_number(sel, number_from_string(tb_get(path), tb_len(path)));
         vlog_set_loc(sel, &target_loc);
         vlog_add_param(v, sel);
         relative = true;
      }
   }

   if (peek() == tDOT && mod != NULL && vlog_kind(mod) == V_MODULE) {
      // The target is the instance at the end of a hierarchical path
      LOCAL_TEXT_BUF path = tb_new();
      if (relative) {
         tb_istr(path, bind_scope);
         tb_append(path, '.');
      }
      tb_istr(path, target);

      mod = bind_walk_path(mod, path);
      if (mod == NULL) {
         drop_tokens_until(&state, tSEMI);   // Also drops the semicolon
         return NULL;
      }

      vlog_node_t sel = vlog_new(V_STRING);
      vlog_set_number(sel, number_from_string(tb_get(path), tb_len(path)));
      vlog_set_loc(sel, &target_loc);
      vlog_add_param(v, sel);
   }
   else if (peek() == tCOLON && mod != NULL && vlog_kind(mod) == V_MODULE) {
      // Only the listed instances of the module are targets
      consume(tCOLON);

      do {
         LOCAL_TEXT_BUF path = tb_new();
         const loc_t loc = *CURRENT_LOC;

         // The path is relative to the module with the directive if it
         // starts with the name of an instance there
         if (bind_scope != NULL && peek() == tID) {
            ident_t first = peek_ident_nth(1);
            vlog_node_t sm = lookup_interface(bind_scope);
            if (sm != NULL && vlog_kind(sm) == V_MODULE
                && bind_child_module(sm, first) != NULL) {
               tb_istr(path, bind_scope);
               tb_append(path, '.');
            }
         }

         tb_istr(path, p_identifier());
         while (optional(tDOT)) {
            tb_append(path, '.');
            tb_istr(path, p_identifier());
         }

         vlog_node_t sel = vlog_new(V_STRING);
         vlog_set_number(sel, number_from_string(tb_get(path), tb_len(path)));
         vlog_set_loc(sel, &loc);
         vlog_add_param(v, sel);
      } while (optional(tCOMMA));
   }

   if (mod == NULL || vlog_kind(mod) != V_MODULE) {
      error_at(&target_loc, "cannot find module or interface %pI to bind into",
               target);
      drop_tokens_until(&state, tSEMI);   // Also drops the semicolon
      return NULL;
   }

   vlog_set_ident2(v, vlog_ident(mod));

   char hash[SHA_HEX_LEN];
   get_hex_hash(loc_file_str(&state.last_loc), hash);
   // The module with the directive is part of the name: the bind only
   // applies if that module is in the design (23.11)
   ident_t name = bind_scope != NULL
      ? ident_sprintf("BIND#%s#%d#%s", hash, next_bind++, istr(bind_scope))
      : ident_sprintf("BIND#%s#%d", hash, next_bind++);
   vlog_set_ident(v, ident_prefix(lib_name(lib_work()), name, '.'));

   vlog_symtab_push(symtab, v);

   // A port declaration is shadowed by the net or variable it declares
   const int ndecls = vlog_decls(mod);
   for (int pass = 0; pass < 2; pass++) {
      for (int i = 0; i < ndecls; i++) {
         vlog_node_t d = vlog_decl(mod, i);
         switch (vlog_kind(d)) {
         case V_VAR_DECL:
         case V_NET_DECL:
         case V_PARAM_DECL:
         case V_LOCALPARAM:
         case V_TYPE_DECL:
         case V_ENUM_DECL:
         case V_STRUCT_DECL:
            if (pass == 0) {
               vlog_symtab_put(symtab, d);

               // The names of an enumeration are visible in the module
               if (vlog_kind(d) == V_TYPE_DECL && vlog_has_type(d)) {
                  vlog_node_t et = vlog_type(d);
                  if (vlog_kind(et) == V_ENUM_DECL) {
                     const int nenum = vlog_decls(et);
                     for (int k = 0; k < nenum; k++) {
                        vlog_node_t en = vlog_decl(et, k);
                        if (vlog_kind(en) == V_ENUM_NAME)
                           vlog_symtab_put(symtab, en);
                     }
                  }
               }
            }
            break;
         case V_PORT_DECL:
            if (pass == 1 && vlog_symtab_query(symtab, vlog_ident(d)) == NULL)
               vlog_symtab_put(symtab, d);
            break;
         default:
            break;
         }
      }
   }

   p_module_or_udp_instantiation(v);

   vlog_symtab_pop(symtab);

   vlog_set_loc(v, CURRENT_LOC);
   return v;
}

static vlog_node_t p_description(void)
{
   // module_declaration | udp_declaration | interface_declaration
   //   | program_declaration | package_declaration
   //   | { attribute_instance } package_item
   //   | { attribute_instance } bind_directive
   //   | config_declaration

   BEGIN("description");

   skip_over_attributes();

   if (at_checker())
      return p_module_declaration();

   if (peek() == tID && peek_ident_nth(1) == ident_new("bind"))
      return p_bind_directive();

   if (peek() == tID && peek_ident_nth(1) == ident_new("config")
       && peek_nth(2) == tID)
      return p_config_declaration();

   switch (peek()) {
   case tMODULE:
      return p_module_declaration();
   case tPRIMITIVE:
      return p_udp_declaration();
   case tPACKAGE:
      return p_package_declaration();
   case tPROGRAM:
      return p_program_declaration();
   case tINTERFACE:
      if (peek_nth(2) != tCLASS)
         return p_module_declaration();
      // Fall-through
   default:
      if (!at_namespace_item()) {
         expect(tPRIMITIVE, tMODULE, tPACKAGE, tPROGRAM, tCLASS, tTYPEDEF,
                tIMPORT, tPARAMETER);
         return NULL;
      }
      else {
         vlog_node_t v = vlog_new(V_NAMESPACE);
         current_namespace = v;

         char hash[SHA_HEX_LEN];
         get_hex_hash(loc_file_str(&state.last_loc), hash);

         ident_t name = ident_sprintf("%s$%d", hash, next_namespace++);
         vlog_set_ident(v, ident_prefix(lib_name(lib_work()), name, '.'));

         param_kind = V_LOCALPARAM;

         do {
            p_package_item(v);
            p_directive_list();
         } while (at_namespace_item());

         flush_deferred_bodies(true);

         vlog_symtab_resolve_deferred(symtab);

         set_decl_scopes(v, vlog_ident(v));

         // Only matters if it has anything other than plain declarations
         for (int i = 0; i < vlog_decls(v); i++) {
            const vlog_kind_t dk = vlog_kind(vlog_decl(v, i));
            if (dk == V_CLASS_DECL || dk == V_IMPORT_DECL) {
               APUSH(file_namespaces, vlog_ident(v));
               break;
            }
         }
         current_namespace = NULL;

         vlog_set_loc(v, CURRENT_LOC);
         return v;
      }
   }
}

static void p_time_scale_value(uint64_t *value, uint64_t *scale,
                               const char *what)
{
   // unsigned_number time_unit

   *value = 1;
   *scale = 1;

   switch (peek()) {
   case tUNSNUM:
      *value = p_unsigned_integer();
      *scale = p_time_unit();
      break;
   case tTIMELIT:
      {
         unsigned frac;
         p_time_literal(value, scale, &frac);
         if (frac > 0) {
            error_at(&state.last_loc, "the value in a %s must be an integer",
                     what);
            *value = 1;
         }
      }
      break;
   default:
      one_of(tUNSNUM, tTIMELIT);
      break;
   }
}

static void p_timescale_compiler_directive(void)
{
   // `timescale time_unit / time_precision

   BEGIN("timescale compiler directive");

   consume(tTIMESCALE);

   uint64_t unit_value, unit_scale, prec_value, prec_scale;
   p_time_scale_value(&unit_value, &unit_scale, "`timescale directive");

   consume(tOVER);

   p_time_scale_value(&prec_value, &prec_scale, "`timescale directive");

   set_timescale(unit_value, unit_scale, prec_value, prec_scale);
}

static void p_timeunits_declaration(void)
{
   // timeunit time_literal [ / time_literal ] ;
   //   | timeprecision time_literal ;
   //   | timeunit time_literal ; timeprecision time_literal ;
   //   | timeprecision time_literal ; timeunit time_literal ;

   BEGIN("time units declaration");

   const char *what = "time units declaration";

   if (optional(tTIMEUNIT)) {
      uint64_t value, scale;
      p_time_scale_value(&value, &scale, what);
      set_time_unit(value, scale, what);

      if (optional(tOVER)) {
         p_time_scale_value(&value, &scale, what);
         set_time_precision(value, scale, what);
      }
   }
   else {
      consume(tTIMEPRECISION);

      uint64_t value, scale;
      p_time_scale_value(&value, &scale, what);
      set_time_precision(value, scale, what);
   }

   consume(tSEMI);

   if (!scan(tTIMEUNIT, tTIMEPRECISION))
      check_time_precision(what);
}

static void p_defaultnettype_compiler_directive(void)
{
   // `default_nettype wire | tri | tri0 | tri1 | wand | triand | wor
   //    | trior | trireg | uwire | none

   BEGIN("default_nettype directive");

   consume(tDEFNETTYPE);

   // None is not a reserved word
   if (peek() == tID && peek_ident_nth(1) == ident_new("none")) {
      consume(tID);
      implicit_kind = V_NET_NONE;
      return;
   }

   switch (one_of(tWIRE, tTRI, tTRI0, tTRI1, tWAND, tTRIAND, tWOR, tTRIOR,
                  tTRIREG, tUWIRE)) {
   case tWIRE: implicit_kind = V_NET_WIRE; break;
   }
}

static void p_unconnected_drive_directive(void)
{
   // `unconnected_drive pull0 | pull1

   BEGIN("unconnected_drive directive");

   consume(tUNCTDRIVE);

   // TODO set default drive for unconnected nets
   one_of(tPULL0, tPULL1);
}

static void p_nounconnected_drive_directive(void)
{
   // `nounconnected_drive

   BEGIN("nounconnected_drive directive");

   consume(tNOUNCTDRIVE);

   // TODO reset default drive for unconnected nets
}

static void p_keywords_directive(void)
{
   // `begin_keywords "version_specifier"

   BEGIN("keywords directive");

   consume(tBEGINKEYWORDS);

   if (consume(tSTRING)) {
      vlog_version_t vers;
      if (parse_verilog_version(tb_get(state.last_lval.text), &vers))
         push_keywords(vers);
      else
         error_at(&state.last_loc, "\"%s\" is not a recognised Verilog or "
                  "System Verilog version", tb_get(state.last_lval.text));

      tb_free(state.last_lval.text);
   }
}

static void p_endkeywords_directive(void)
{
   // `end_keywords

   BEGIN("endkeywords directive");

   consume(tENDKEYWORDS);

   if (!pop_keywords())
      error_at(&state.last_loc, "`end_keywords directive without matching "
               "`begin_keywords");
}

static void p_resetall_directive(void)
{
   // `resetall

   BEGIN("resetall directive");

   consume(tRESETALL);

   implicit_kind = V_NET_WIRE;
   time_scale = default_time_scale;
   time_precision_log10 = default_precision_log10;
   time_unscaled = default_unscaled;
}

static void p_directive_list(void)
{
   BEGIN("directive list");

   for (;;) {
      switch (peek()) {
      case tDEFNETTYPE:
         p_defaultnettype_compiler_directive();
         break;
      case tTIMESCALE:
         p_timescale_compiler_directive();
         break;
      case tUNCTDRIVE:
         p_unconnected_drive_directive();
         break;
      case tNOUNCTDRIVE:
         p_nounconnected_drive_directive();
         break;
      case tBEGINKEYWORDS:
         p_keywords_directive();
         break;
      case tENDKEYWORDS:
         p_endkeywords_directive();
         break;
      case tRESETALL:
         p_resetall_directive();
         break;
      default:
         return;
      }
   }
}

static vlog_node_t end_of_file(void)
{
   for (int i = 0; i < forward_types.count; i++)
      error_at(&forward_types.items[i].loc, "forward type declaration of "
               "%pQ is never resolved to a data type",
               forward_types.items[i].id);

   ACLEAR(forward_types);

   // Anything still waiting to be replayed belongs to this file
   ACLEAR(pending_units);
   ACLEAR(pending_binds);
   ACLEAR(pending_bind_scopes);

   vlog_symtab_pop(symtab);
   vlog_symtab_free(symtab);
   symtab = NULL;
   return NULL;
}

static vlog_node_t p_source_text(void)
{
   // [ timeunits_declaration ] { description }

   BEGIN("source text");

   for (;;) {
      if (peek() == tEOF) {
         discard_global_arena();
         return end_of_file();
      }

      if (p_repeated_design_unit()) {
         p_directive_list();
         continue;
      }

      if (scan(tTIMEUNIT, tTIMEPRECISION)) {
         p_timeunits_declaration();
         continue;
      }

      vlog_node_t v = p_description();
      if (v == NULL) {
         // Syntax error: nothing in this arena can be used and the
         // symbol table must not leak into the next file
         discard_global_arena();
         return end_of_file();
      }
      else if (is_top_level(v))
         return v;
   }
}

vlog_node_t vlog_parse(void)
{
   if (pending_units.count > 0) {
      cov_toks_t toks = pending_units.items[0];
      for (int i = 1; i < pending_units.count; i++)
         pending_units.items[i - 1] = pending_units.items[i];
      pending_units.count--;

      make_new_arena();
      vlog_node_t u = bind_replay(toks, false);
      if (u != NULL)
         return u;
   }

   if (pending_binds.count > 0) {
      cov_toks_t toks = pending_binds.items[0];
      for (int i = 1; i < pending_binds.count; i++)
         pending_binds.items[i - 1] = pending_binds.items[i];
      pending_binds.count--;

      bind_scope = pending_bind_scopes.items[0];
      for (int i = 1; i < pending_bind_scopes.count; i++)
         pending_bind_scopes.items[i - 1] = pending_bind_scopes.items[i];
      pending_bind_scopes.count--;

      make_new_arena();
      vlog_node_t b = bind_replay(toks, true);
      bind_scope = NULL;
      if (b != NULL)
         return b;
   }

   state.n_correct = RECOVER_THRESH;
   param_kind = V_PARAM_DECL;

   if (used_labels != NULL) {
      hset_free(used_labels);
      used_labels = NULL;
   }

   for (int i = 0; i < ARRAY_LEN(atom_types); i++)
      atom_types[i] = NULL;

   scan_as_verilog();

   if (symtab == NULL) {
      symtab = vlog_symtab_new();
      vlog_symtab_push(symtab, NULL);   // Compilation unit scope
      vlog_symtab_poison(symtab, error_marker());
   }

   p_directive_list();

   if (peek() == tEOF)
      return end_of_file();

   make_new_arena();

   return p_source_text();
}

void vlog_set_default_timescale(uint64_t unit_fs, unsigned precision_log10)
{
   default_time_scale = unit_fs;
   default_precision_log10 = precision_log10;
   default_unscaled = false;
   time_scale = unit_fs;
   time_precision_log10 = precision_log10;
   time_unscaled = false;
}

void reset_verilog_parser(void)
{
   state.n_correct = RECOVER_THRESH;
   state.tokenq_head = state.tokenq_tail = 0;
   state.lex_fn = processed_yylex;
   state.error_fn = vlog_parse_error_cb;

   implicit_kind = V_NET_WIRE;
   last_attr = NULL;
   next_namespace = 1;
   time_scale = default_time_scale;
   time_precision_log10 = default_precision_log10;
   time_unscaled = default_unscaled;
   proc_automatic = subr_automatic = false;
   current_func = NULL;
   in_subroutine = false;
   ACLEAR(pending_statics);
   ACLEAR(pending_disables);
   spec_depth = 0;
   ACLEAR(deferred_bodies);
   ACLEAR(forward_classes);
   ACLEAR(forward_specs);
   ACLEAR(unit_specs);
   ACLEAR(unit_generics);
   ACLEAR(file_namespaces);
   last_modport = NULL;

   // These refer to scopes which are about to be freed
   if (generic_classes != NULL) {
      hash_free(generic_classes);
      generic_classes = NULL;
   }

   if (generic_homes != NULL) {
      hash_free(generic_homes);
      generic_homes = NULL;
   }

   class_depth = 0;
   flushing_bodies = false;
   spec_into = NULL;

   if (seen_units != NULL) {
      hset_free(seen_units);
      seen_units = NULL;
   }

   if (symtab != NULL) {
      vlog_symtab_free(symtab);
      symtab = NULL;
   }
}
