//
//  Copyright (C) 2022-2026  Nick Gasson
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

#ifndef _VLOG_NODE_H
#define _VLOG_NODE_H

#include "prim.h"

typedef enum {
   V_SVA_DELAY,
   V_SVA_REPEAT,
   V_SVA_GOTO,
   V_SVA_NONCONS,
   V_SVA_AND,
   V_SVA_OR,
   V_SVA_INTERSECT,
   V_SVA_WITHIN,
   V_SVA_THROUGHOUT,
   V_SVA_FIRST_MATCH,
   V_SVA_NOT,
   V_SVA_IMPL_OVER,
   V_SVA_IMPL_NON,
   V_SVA_FOLLOW_OVER,
   V_SVA_FOLLOW_NON,
   V_SVA_STRONG,
   V_SVA_WEAK,
   V_SVA_IF,
   V_SVA_NEXTTIME,
   V_SVA_S_NEXTTIME,
   V_SVA_ALWAYS,
   V_SVA_S_ALWAYS,
   V_SVA_EVENTUALLY,
   V_SVA_S_EVENTUALLY,
   V_SVA_UNTIL,
   V_SVA_S_UNTIL,
   V_SVA_UNTIL_WITH,
   V_SVA_S_UNTIL_WITH,
   V_SVA_IMPLIES,
   V_SVA_IFF,
   V_SVA_ACCEPT_ON,
   V_SVA_REJECT_ON,
   V_SVA_SYNC_ACCEPT_ON,
   V_SVA_SYNC_REJECT_ON,
   V_SVA_CLOCKED,
   V_SVA_DISABLE,
   V_SVA_MATCH,
} vlog_sva_op_t;

typedef enum {
   V_SVA_SEQUENCE,
   V_SVA_PROPERTY,
} vlog_sva_decl_t;

typedef enum {
   V_ASSERT_SIMPLE,
   V_ASSERT_DEFERRED,
   V_ASSERT_FINAL,
   V_ASSERT_CONCURRENT,
   V_ASSERT_COVER_SEQ,
   V_ASSERT_PROCEDURAL,
   V_ASSERT_EXPECT,
} vlog_assert_t;

typedef enum {
   V_EVENT_LEVEL,
   V_EVENT_POSEDGE,
   V_EVENT_NEGEDGE,
   V_EVENT_EDGE
} v_event_kind_t;

typedef enum {
   V_PORT_INPUT,
   V_PORT_OUTPUT,
   V_PORT_INOUT,
   V_PORT_REF,
} v_port_kind_t;

typedef enum {
   V_FORK_JOIN,
   V_FORK_JOIN_ANY,
   V_FORK_JOIN_NONE,
} vlog_fork_kind_t;

typedef enum {
   V_FORK_WAIT,
   V_FORK_DISABLE,
} vlog_fork_ctrl_t;

typedef enum {
   V_JUMP_BREAK,
   V_JUMP_CONTINUE,
} vlog_jump_kind_t;

typedef enum {
   V_NET_NONE,
   V_NET_WIRE,
   V_NET_UWIRE,
   V_NET_SUPPLY0,
   V_NET_SUPPLY1,
   V_NET_TRI,
   V_NET_TRIAND,
   V_NET_TRIOR,
   V_NET_TRIREG,
   V_NET_TRI0,
   V_NET_TRI1,
   V_NET_WAND,
   V_NET_WOR,
} vlog_net_kind_t;

typedef enum {
   V_UDP_COMB,
   V_UDP_SEQ,
} vlog_udp_kind_t;

typedef enum {
   V_ALWAYS_PLAIN,
   V_ALWAYS_FF,
   V_ALWAYS_COMB,
   V_ALWAYS_LATCH,
   V_ALWAYS_COMB_DONE,    // always_comb with its sensitivity made explicit
   V_ALWAYS_LATCH_DONE,
} vlog_always_kind_t;

typedef enum {
   V_CASE_NORMAL,
   V_CASE_X,
   V_CASE_Z,
   V_CASE_INSIDE,
} vlog_case_kind_t;

typedef enum {
   V_TCHECK_SETUP,
   V_TCHECK_HOLD,
   V_TCHECK_RECOVERY,
   V_TCHECK_REMOVAL
} vlog_tcheck_kind_t;

typedef enum {
   V_MODULE,
   V_PORT_DECL,
   V_REF,
   V_ALWAYS,
   V_TIMING,
   V_NBASSIGN,
   V_EVENT,
   V_INITIAL,
   V_SEQ_BLOCK,
   V_SYS_TCALL,
   V_STRING,
   V_NUMBER,
   V_NET_DECL,
   V_ASSIGN,
   V_DIMENSION,
   V_IF,
   V_COND,
   V_VAR_DECL,
   V_DELAY_CONTROL,
   V_BINARY,
   V_BASSIGN,
   V_UNARY,
   V_GATE_INST,
   V_STRENGTH,
   V_MOD_INST,
   V_BIT_SELECT,
   V_SYS_FCALL,
   V_FOREVER,
   V_SPECIFY,
   V_PRIMITIVE,
   V_UDP_TABLE,
   V_UDP_ENTRY,
   V_DATA_TYPE,
   V_TYPE_DECL,
   V_ENUM_DECL,
   V_ENUM_NAME,
   V_UNION_DECL,
   V_STRUCT_DECL,
   V_EVENT_CONTROL,
   V_EMPTY,
   V_REPEAT,
   V_WHILE,
   V_DO_WHILE,
   V_TASK_DECL,
   V_FUNC_DECL,
   V_WAIT,
   V_PARAM_DECL,
   V_COND_EXPR,
   V_REAL,
   V_CONCAT,
   V_FOR_LOOP,
   V_FOR_INIT,
   V_FOR_STEP,
   V_PREFIX,
   V_POSTFIX,
   V_LOCALPARAM,
   V_CASE,
   V_CASE_ITEM,
   V_INST_LIST,
   V_PARAM_ASSIGN,
   V_INST_BODY,
   V_PORT_CONN,
   V_PART_SELECT,
   V_IF_GENERATE,
   V_EVENT_TRIGGER,
   V_USER_FCALL,
   V_UDP_LEVEL,
   V_UDP_EDGE,
   V_SPECPARAM,
   V_FORK,
   V_ATTR_INST,
   V_USER_TCALL,
   V_VOID_CALL,
   V_GENVAR_DECL,
   V_FOR_GENERATE,
   V_DEASSIGN,
   V_FORCE,
   V_RELEASE,
   V_DISABLE,
   V_HIER_REF,
   V_TF_PORT_DECL,
   V_RETURN,
   V_OP_ASSIGN,
   V_MEMBER_REF,
   V_PACKAGE,
   V_MIN_TYP_MAX,
   V_PROGRAM,
   V_CLASS_DECL,
   V_NULL,
   V_CLASS_NEW,
   V_DYNAMIC_NEW,
   V_CONSTRUCTOR,
   V_SUPER_CALL,
   V_IMPORT_DECL,
   V_NAMESPACE,
   V_DEFPARAM,
   V_PORT_MAP,
   V_FINAL,
   V_LOCAL_DECL,
   V_GEN_BLOCK,
   V_METHOD_CALL,
   V_MOD_REF,
   V_ASSERT,
   V_ASSUME,
   V_COVER,
   V_PATTERN_EXPR,
   V_PATTERN_ITEM,
   V_CAST,
   V_TCHECK,
   V_TCHECK_EVENT,
   V_THIS,
   V_DOLLAR,
   V_LOOP_JUMP,
   V_FORK_CTRL,
   V_CONSTRAINT,
   V_CONS_EXPR,
   V_CONS_IMPLY,
   V_CONS_IF,
   V_CONS_BLOCK,
   V_CONS_FOREACH,
   V_CONS_UNIQUE,
   V_CONS_SOLVE,
   V_INSIDE,
   V_INSIDE_RANGE,
   V_DIST,
   V_DIST_ITEM,
   V_STD_RANDOMIZE,
   V_RANDCASE,
   V_RANDCASE_ITEM,
   V_STREAM,
   V_NAMED_ARG,
   V_MODPORT,
   V_IFACE_PORT,
   V_MODPORT_PORT,
   V_MATCHES,
   V_MATCH_PAT,
   V_SVA_OP,
   V_SVA_DECL,
   V_PROP_SPEC,
   V_EXPECT,
   V_BIND,
   V_DPI_EXPORT,
   V_CONFIG,
   V_CONFIG_RULE,

   V_LAST_NODE_KIND
} vlog_kind_t;

typedef enum {
   V_CONFIG_INSTANCE,
   V_CONFIG_CELL,
   V_CONFIG_DEFAULT,
} vlog_config_rule_t;

typedef enum {
   DT_IMPLICIT,
   DT_LOGIC,
   DT_INTEGER,
   DT_REAL,
   DT_BYTE,
   DT_SHORTINT,
   DT_INT,
   DT_LONGINT,
   DT_TIME,
   DT_BIT,
   DT_SHORTREAL,
   DT_REALTIME,
   DT_EVENT,
   DT_STRING,
   DT_DYNARRAY,
   DT_QUEUE,
   DT_MAILBOX,
   DT_VIRTUAL,
   DT_ASSOC,
   DT_PROCESS,
   DT_CHANDLE,
   DT_SEMAPHORE,
} data_type_t;

typedef enum {
   V_PAT_WILD,
   V_PAT_BIND,
   V_PAT_CONST,
   V_PAT_TAGGED,
   V_PAT_STRUCT,
} vlog_pattern_t;

typedef enum {
   V_DIM_PACKED,
   V_DIM_UNPACKED,
   V_DIM_UNSIZED,
   V_DIM_ASSOC,
} vlog_dimension_kind_t;

typedef enum {
   V_RANGE_CONST,
   V_RANGE_POS,
   V_RANGE_NEG,
} vlog_range_kind_t;

typedef enum {
   V_BINARY_OR,
   V_BINARY_AND,
   V_BINARY_PLUS,
   V_BINARY_MINUS,
   V_BINARY_TIMES,
   V_BINARY_DIVIDE,
   V_BINARY_MOD,
   V_BINARY_LOG_EQ,
   V_BINARY_LOG_NEQ,
   V_BINARY_CASE_EQ,
   V_BINARY_CASE_NEQ,
   V_BINARY_LOG_OR,
   V_BINARY_LOG_AND,
   V_BINARY_SHIFT_LL,
   V_BINARY_SHIFT_RL,
   V_BINARY_SHIFT_LA,
   V_BINARY_SHIFT_RA,
   V_BINARY_LT,
   V_BINARY_GT,
   V_BINARY_LEQ,
   V_BINARY_GEQ,
   V_BINARY_EXP,
   V_BINARY_XOR,
   V_BINARY_XNOR,
   V_BINARY_NAND,
   V_BINARY_NOR,
   V_BINARY_WILD_EQ,
   V_BINARY_WILD_NEQ,
} vlog_binary_t;

typedef enum {
   V_UNARY_BITNEG,
   V_UNARY_NOT,
   V_UNARY_NEG,
   V_UNARY_IDENTITY,
   V_UNARY_AND,
   V_UNARY_OR,
   V_UNARY_XOR,
   V_UNARY_NAND,
   V_UNARY_NOR,
   V_UNARY_XNOR,
} vlog_unary_t;

typedef enum {
   V_INCDEC_PLUS,
   V_INCDEC_MINUS,
} vlog_incdec_t;

typedef enum {
   V_ASSIGN_EQUALS,
   V_ASSIGN_PLUS,
   V_ASSIGN_MINUS,
   V_ASSIGN_TIMES,
   V_ASSIGN_DIVIDE,
   V_ASSIGN_MOD,
   V_ASSIGN_AND,
   V_ASSIGN_OR,
   V_ASSIGN_XOR,
   V_ASSIGN_SHIFT_LL,
   V_ASSIGN_SHIFT_RL,
   V_ASSIGN_SHIFT_LA,
   V_ASSIGN_SHIFT_RA,
} vlog_assign_t;

typedef enum {
   V_GATE_PULLDOWN,
   V_GATE_PULLUP,
   V_GATE_AND,
   V_GATE_NAND,
   V_GATE_OR,
   V_GATE_NOR,
   V_GATE_XOR,
   V_GATE_XNOR,
   V_GATE_NOT,
   V_GATE_NOTIF0,
   V_GATE_NOTIF1,
   V_GATE_BUF,
   V_GATE_BUFIF0,
   V_GATE_BUFIF1,
   V_GATE_TRAN,
   V_GATE_TRANIF0,
   V_GATE_TRANIF1,
   V_GATE_RTRAN,
   V_GATE_RTRANIF0,
   V_GATE_RTRANIF1,
} vlog_gate_kind_t;

typedef enum {
   V_STRENGTH_HIGHZ,
   V_STRENGTH_SMALL,
   V_STRENGTH_MEDIUM,
   V_STRENGTH_WEAK,
   V_STRENGTH_LARGE,
   V_STRENGTH_PULL,
   V_STRENGTH_STRONG,
   V_STRENGTH_SUPPLY,
} vlog_strength_t;

typedef enum {
   V_UDP_SYMBOL_OUTPUT,
   V_UDP_SYMBOL_INPUT,
} vlog_udp_symbol_t;

typedef enum {
   V_SYSTF_NONE,
   V_SYSTF_SIGNED,
   V_SYSTF_UNSIGNED,
   V_SYSTF_CLOG2,
   V_SYSTF_SQRT,
   V_SYSTF_BITS,
   V_SYSTF_RTOI,
   V_SYSTF_CEIL,
   V_SYSTF_FLOOR,
   V_SYSTF_MATH,
} vlog_systf_t;

typedef enum {
   V_METHOD_USER,
   V_METHOD_ENUM_FIRST,
   V_METHOD_ENUM_LAST,
   V_METHOD_ENUM_NEXT,
   V_METHOD_ENUM_PREV,
   V_METHOD_ENUM_NUM,
   V_METHOD_ENUM_NAME,
   V_METHOD_SIZE,
   V_METHOD_DELETE,
   V_METHOD_PUSH_BACK,
   V_METHOD_PUSH_FRONT,
   V_METHOD_POP_BACK,
   V_METHOD_POP_FRONT,
   V_METHOD_MBOX_PUT,
   V_METHOD_MBOX_GET,
   V_METHOD_MBOX_TRY_GET,
   V_METHOD_MBOX_NUM,
   V_METHOD_MBOX_TRY_PUT,
   V_METHOD_MBOX_PEEK,
   V_METHOD_MBOX_TRY_PEEK,
   V_METHOD_CLASS,
   V_METHOD_RANDOMIZE,
   V_METHOD_RAND_MODE,
   V_METHOD_CONSTRAINT_MODE,
   V_METHOD_STR_LEN,
   V_METHOD_STR_PUTC,
   V_METHOD_STR_GETC,
   V_METHOD_STR_TOUPPER,
   V_METHOD_STR_TOLOWER,
   V_METHOD_STR_COMPARE,
   V_METHOD_STR_ICOMPARE,
   V_METHOD_STR_SUBSTR,
   V_METHOD_STR_ATOI,
   V_METHOD_STR_ATOHEX,
   V_METHOD_STR_ATOOCT,
   V_METHOD_STR_ATOBIN,
   V_METHOD_STR_ATOREAL,
   V_METHOD_STR_ITOA,
   V_METHOD_STR_HEXTOA,
   V_METHOD_STR_OCTTOA,
   V_METHOD_STR_BINTOA,
   V_METHOD_STR_REALTOA,
   V_METHOD_ARR_INSERT,
   V_METHOD_ARR_REVERSE,
   V_METHOD_ARR_SORT,
   V_METHOD_ARR_RSORT,
   V_METHOD_ARR_SHUFFLE,
   V_METHOD_ARR_SUM,
   V_METHOD_ARR_PRODUCT,
   V_METHOD_ARR_AND,
   V_METHOD_ARR_OR,
   V_METHOD_ARR_XOR,
   V_METHOD_ARR_MIN,
   V_METHOD_ARR_MAX,
   V_METHOD_ARR_UNIQUE,
   V_METHOD_ARR_UNIQUE_INDEX,
   V_METHOD_ARR_FIND,
   V_METHOD_ARR_FIND_INDEX,
   V_METHOD_ARR_FIND_FIRST,
   V_METHOD_ARR_FIND_FIRST_INDEX,
   V_METHOD_ARR_FIND_LAST,
   V_METHOD_ARR_FIND_LAST_INDEX,
   V_METHOD_ASSOC_EXISTS,
   V_METHOD_ASSOC_FIRST,
   V_METHOD_ASSOC_LAST,
   V_METHOD_ASSOC_NEXT,
   V_METHOD_ASSOC_PREV,
   V_METHOD_PROC_STATUS,
   V_METHOD_PROC_KILL,
   V_METHOD_PROC_AWAIT,
   V_METHOD_PROC_SUSPEND,
   V_METHOD_PROC_RESUME,
   V_METHOD_RANDSTATE_GET,
   V_METHOD_RANDSTATE_SET,
   V_METHOD_SRANDOM,
   V_METHOD_EVENT_TRIGGERED,
   V_METHOD_SEM_GET,
   V_METHOD_SEM_PUT,
   V_METHOD_SEM_TRY_GET,
   V_METHOD_NOP,
} vlog_method_t;

typedef enum {
   V_STREAM_RIGHT,
   V_STREAM_LEFT,
} vlog_stream_t;

typedef enum {
   V_DIST_PER_VALUE,
   V_DIST_PER_RANGE,
} vlog_dist_t;

typedef enum {
   V_CAST_TYPE,
   V_CAST_WIDTH,
   V_CAST_SIGN,
   V_CAST_STRING,
   V_CAST_CONST,
   V_CAST_TYPEREF,
} vlog_cast_t;

typedef enum {
   VLOG_F_SIGNED = (1 << 0),
   VLOG_F_PACKED = (1 << 1),
   VLOG_F_CONST  = (1 << 2),
   VLOG_F_FOLDED = (1 << 3),
   VLOG_F_VIRTUAL = (1 << 4),
   VLOG_F_STATIC = (1 << 5),
   VLOG_F_PURE = (1 << 6),
   VLOG_F_INTERFACE = (1 << 7),
   VLOG_F_GLOBAL = (1 << 8),
   VLOG_F_VHDL = (1 << 9),
   VLOG_F_GENERIC = (1 << 10),
   VLOG_F_RAND = (1 << 11),
   VLOG_F_RANDC = (1 << 12),
   VLOG_F_SOFT = (1 << 13),
   VLOG_F_UNIQUE = (1 << 14),
   VLOG_F_UNIQUE0 = (1 << 15),
   VLOG_F_PRIORITY = (1 << 16),
   VLOG_F_FILL = (1 << 17),
   VLOG_F_WILDCARD = (1 << 18),
   VLOG_F_PROPERTY = (1 << 19),
   VLOG_F_EXTERN = (1 << 20),
   VLOG_F_DISABLE = (1 << 21),
   VLOG_F_IMPLICIT_STATIC = (1 << 22),
   VLOG_F_FORWARD = (1 << 23),
   VLOG_F_SPEC = (1 << 24),
   VLOG_F_DPI = (1 << 25),
   VLOG_F_SYSTEMVERILOG = (1 << 26),
   VLOG_F_TIMELIT = (1 << 27),
   VLOG_F_ONESTEP = (1 << 28),
   VLOG_F_IN_SUBR = (1 << 29),
   VLOG_F_VHDL_PATH = (1 << 30),
   VLOG_F_TAGGED = VLOG_F_PURE,      // On a union type
   VLOG_F_SPECPARAM = VLOG_F_PURE,   // On a local parameter
   VLOG_F_EXPORT = VLOG_F_PURE,      // On a package export declaration
   VLOG_F_VOID = VLOG_F_PURE,        // On a void member of a tagged union
   VLOG_F_TIMEEXPR = VLOG_F_SOFT,    // On a time literal in an expression
   VLOG_F_TYPEPARAM = VLOG_F_CONST,  // On a type declaration which is a parameter
   VLOG_F_NETTYPE = VLOG_F_RAND,     // On a type declaration of a net type
   VLOG_F_USERNET = VLOG_F_PRIORITY, // On a variable which is a net of a user-defined net type
   VLOG_F_BLACKBOX = VLOG_F_UNIQUE0,   // On a module made for a missing unit
   VLOG_F_NESTED_INST = VLOG_F_SPEC,   // On a variable for an interface instance
   VLOG_F_CHECKER = VLOG_F_DISABLE,  // On a module which is a checker
   VLOG_F_LET = VLOG_F_SOFT,         // On a function declaration
   VLOG_F_NAMED_DISABLE = VLOG_F_VIRTUAL,  // On a block named by a disable
   VLOG_F_UNPACKED_VIEW = VLOG_F_FILL, // On a packed union declared unpacked
   VLOG_F_GEN_BODY = VLOG_F_FILL,    // On the body of a generate block
   VLOG_F_REALVIEW = VLOG_F_ONESTEP, // On a real member of a union held as bits
   VLOG_F_REFVAR = VLOG_F_TIMELIT,   // On a class property holding a reference
   VLOG_F_SVA_LOCAL = VLOG_F_SOFT,   // On a local variable of a sequence
   VLOG_F_CONTEXT = VLOG_F_RANDC,    // On a DPI import declared context
   VLOG_F_IMPLICIT = VLOG_F_FORWARD, // On a constraint prototype without extern
   VLOG_F_OPEN_PACKED = VLOG_F_WILDCARD,  // On an unsized packed dimension
   VLOG_F_OVERRIDABLE = VLOG_F_GENERIC,   // On a local parameter bound to a parameter
} vlog_flags_t;

typedef enum {
   V_MODPORT_INPUT,
   V_MODPORT_OUTPUT,
   V_MODPORT_INOUT,
   V_MODPORT_REF,
   V_MODPORT_IMPORT,
   V_MODPORT_EXPORT,
} vlog_modport_dir_t;

typedef enum {
   VLOG_GF_HIER_REF = (1 << 0),
} vlog_global_flags_t;

#define STRENGTH1(st) (((st) >> 5) & 7)
#define STRENGTH0(st) (((st) >> 2) & 7)

#define MAKE_STRENGTH(s0, s1) ((s1) << 5 | (s0) << 2)

#define ST_SUPPLY MAKE_STRENGTH(V_STRENGTH_SUPPLY, V_STRENGTH_SUPPLY)
#define ST_STRONG MAKE_STRENGTH(V_STRENGTH_STRONG, V_STRENGTH_STRONG)
#define ST_PULLUP MAKE_STRENGTH(V_STRENGTH_PULL, V_STRENGTH_PULL)

vlog_node_t vlog_new(vlog_kind_t kind);
vlog_kind_t vlog_kind(vlog_node_t v);
const char *vlog_kind_str(vlog_kind_t kind);

void make_new_vlog_arena(void);

const loc_t *vlog_loc(vlog_node_t v);
void vlog_set_loc(vlog_node_t v, const loc_t *loc);

ident_t vlog_ident(vlog_node_t v);
void vlog_set_ident(vlog_node_t v, ident_t i);
bool vlog_has_ident(vlog_node_t v);

ident_t vlog_ident2(vlog_node_t v);
void vlog_set_ident2(vlog_node_t v, ident_t i);
bool vlog_has_ident2(vlog_node_t v);
ident_t vlog_ident3(vlog_node_t v);
void vlog_set_ident3(vlog_node_t v, ident_t i);
bool vlog_has_ident3(vlog_node_t v);

vlog_node_t vlog_ref(vlog_node_t v);
void vlog_set_ref(vlog_node_t v, vlog_node_t d);
bool vlog_has_ref(vlog_node_t v);

unsigned vlog_stmts(vlog_node_t v);
vlog_node_t vlog_stmt(vlog_node_t v, unsigned n);
void vlog_add_stmt(vlog_node_t v, vlog_node_t s);

unsigned vlog_ports(vlog_node_t v);
vlog_node_t vlog_port(vlog_node_t v, unsigned n);
void vlog_add_port(vlog_node_t v, vlog_node_t p);
void vlog_delete_port(vlog_node_t v, unsigned n);

unsigned vlog_params(vlog_node_t v);
vlog_node_t vlog_param(vlog_node_t v, unsigned n);
void vlog_add_param(vlog_node_t v, vlog_node_t p);
void vlog_set_param(vlog_node_t v, unsigned n, vlog_node_t p);

unsigned vlog_ranges(vlog_node_t v);
vlog_node_t vlog_range(vlog_node_t v, unsigned n);
void vlog_add_range(vlog_node_t v, vlog_node_t r);
void vlog_clear_ranges(vlog_node_t v);

unsigned vlog_decls(vlog_node_t v);
vlog_node_t vlog_decl(vlog_node_t v, unsigned n);
void vlog_add_decl(vlog_node_t v, vlog_node_t d);

unsigned vlog_conds(vlog_node_t v);
vlog_node_t vlog_cond(vlog_node_t v, unsigned n);
void vlog_add_cond(vlog_node_t v, vlog_node_t c);

unsigned vlog_subkind(vlog_node_t v);
void vlog_set_subkind(vlog_node_t v, unsigned sub);

vlog_node_t vlog_value(vlog_node_t v);
bool vlog_has_value(vlog_node_t v);
void vlog_set_value(vlog_node_t v, vlog_node_t e);

vlog_node_t vlog_target(vlog_node_t v);
void vlog_set_target(vlog_node_t v, vlog_node_t e);

vlog_node_t vlog_delay(vlog_node_t v);
bool vlog_has_delay(vlog_node_t v);
void vlog_set_delay(vlog_node_t v, vlog_node_t d);

vlog_node_t vlog_type(vlog_node_t v);
bool vlog_has_type(vlog_node_t v);
void vlog_set_type(vlog_node_t v, vlog_node_t t);

vlog_node_t vlog_left(vlog_node_t v);
bool vlog_has_left(vlog_node_t v);
bool vlog_has_target(vlog_node_t v);
void vlog_set_left(vlog_node_t v, vlog_node_t e);

vlog_node_t vlog_right(vlog_node_t v);
bool vlog_has_right(vlog_node_t v);
void vlog_set_right(vlog_node_t v, vlog_node_t e);

const char *vlog_text(vlog_node_t v);
void vlog_set_text(vlog_node_t v, const char *text);

number_t vlog_number(vlog_node_t v);
void vlog_set_number(vlog_node_t v, number_t n);

double vlog_dval(vlog_node_t v);
void vlog_set_dval(vlog_node_t v, double d);

int64_t vlog_ival(vlog_node_t v);
void vlog_set_ival(vlog_node_t v, int64_t i);

vlog_flags_t vlog_flags(vlog_node_t t);
void vlog_set_flags(vlog_node_t v, vlog_flags_t mask);
void vlog_clear_flags(vlog_node_t v, vlog_flags_t mask);

// System tasks hold the time unit of the module in femtoseconds, stored
// as one plus its base ten logarithm in bits 0 to 5 or zero if not known,
// with the base ten logarithm of the time precision in bits 54 to 58 and
// the same for the whole design plus one above that, or zero if not known
static inline int64_t vlog_time_unit_code(int64_t unit_fs)
{
   int code = 0;
   for (; unit_fs > 0; unit_fs /= 10)
      code++;
   return code;
}

static inline int64_t vlog_time_unit_value(int64_t code)
{
   int64_t unit_fs = 0;
   if (code > 0) {
      unit_fs = 1;
      for (; code > 1; code--)
         unit_fs *= 10;
   }
   return unit_fs;
}

#define VLOG_TIME_ENCODE(unit, log10) \
   (vlog_time_unit_code(unit) | ((int64_t)(log10) << 54))
// The precision is this if there is no timescale in effect, in which case
// the default unit and precision of 1ns/1ns apply
#define VLOG_TIME_UNSCALED            31
#define VLOG_DEFAULT_LOG10            6
#define VLOG_TIME_UNIT(ival)          vlog_time_unit_value((ival) & 63)
#define VLOG_TIME_LOG10(ival)         ((unsigned)(((ival) >> 54) & 31))
#define VLOG_TIME_SET_DESIGN(ival, g) \
   ((int64_t)((uint64_t)(ival) & ((UINT64_C(1) << 59) - 1)) \
    | (int64_t)((uint64_t)((g) + 1) << 59))
#define VLOG_TIME_DESIGN(ival) ((int)(((uint64_t)(ival) >> 59) & 31) - 1)

typedef void (*vlog_visit_fn_t)(vlog_node_t v, void *context);

void vlog_visit(vlog_node_t v, vlog_visit_fn_t fn, void *context);
void vlog_visit_only(vlog_node_t v, vlog_visit_fn_t fn, void *context,
                     vlog_kind_t kind);

typedef void (*vlog_rewrite_pre_fn_t)(vlog_node_t t, void *context);
typedef vlog_node_t (*vlog_rewrite_post_fn_t)(vlog_node_t t, void *context);

vlog_node_t vlog_rewrite(vlog_node_t v, vlog_rewrite_pre_fn_t pre_fn,
                         vlog_rewrite_post_fn_t post_fn, void *context);

typedef bool (*vlog_copy_pred_t)(vlog_node_t, void *);

vlog_node_t vlog_copy(vlog_node_t v, vlog_copy_pred_t pred, void *ctx);

object_t *vlog_to_object(vlog_node_t v);
vlog_node_t vlog_from_object(object_t *obj);

vlog_global_flags_t vlog_global_flags(vlog_node_t v);
void vlog_set_global_flags(vlog_node_t v, vlog_global_flags_t flags);

#endif  // _VLOG_NODE_H
