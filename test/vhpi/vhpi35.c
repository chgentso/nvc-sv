#include "vhpi_test.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

// Variables and parameters of a subprogram frame (vpi_ext_nvc.h)
typedef void (*stmt_fn_t)(const char *file, int line, const char *process,
                          const char *scope, void *user_data);
void vpi_nvc_set_stmt_cb(stmt_fn_t fn, void *user_data);
int vpi_nvc_stmt_breakpoint_add(const char *file, int line);

enum { K_NONE, K_LOGIC, K_BIT, K_INTEGER, K_ENUM, K_REAL, K_TIME, K_STRING };

typedef struct {
   const char        *name;
   int                is_param;
   int                kind;
   int                is_signed;
   int                width;
   const char        *type_name;
   const char        *bits;
   const char        *text;
   int                nliterals;
   const char *const *literals;
   int                has_range;
   long long          low;
   long long          high;
   const char        *file;
   int                line;
} var_t;

typedef void (*var_fn_t)(const var_t *var, void *user_data);
int vpi_nvc_frame_variables(int frame, var_fn_t fn, void *user_data);

#define check_int(a, b) fail_unless((long)(a) == (long)(b))
#define check_str(a, b) \
   do { \
      if (strcmp((a), (b))) { \
         printf("'%s' != '%s'\n", (a), (b)); \
         fail_unless(false); \
      } \
   } while (0)

#define MAX_HITS 16
#define MAX_VARS 16

typedef struct {
   char *name;
   int   is_param, kind, width, nlits, line;
   char *bits, *text, *lit0;
} saved_t;

static struct {
   int     line;
   int     count;
   saved_t vars[MAX_VARS];
} hits[MAX_HITS];
static int nhits;

static void on_var(const var_t *v, void *user_data)
{
   int *n = user_data;
   fail_unless(*n < MAX_VARS);
   saved_t *s = &(hits[nhits].vars[(*n)++]);
   s->name = strdup(v->name);
   s->is_param = v->is_param;
   s->kind = v->kind;
   s->width = v->width;
   s->nlits = v->nliterals;
   s->line = v->line;
   s->bits = v->bits ? strdup(v->bits) : NULL;
   s->text = strdup(v->text);
   s->lit0 = v->nliterals > 0 ? strdup(v->literals[0]) : NULL;
}

static void on_stmt(const char *file, int line, const char *process,
                    const char *scope, void *user_data)
{
   fail_unless(nhits < MAX_HITS);

   int n = 0;
   const int ret = vpi_nvc_frame_variables(0, on_var, &n);
   check_int(ret, n);
   // Only the innermost frame has variables
   check_int(vpi_nvc_frame_variables(1, on_var, &n), 0);

   hits[nhits].line = line;
   hits[nhits].count = n;
   nhits++;
}

static const saved_t *find(int hit, const char *name)
{
   for (int i = 0; i < hits[hit].count; i++) {
      if (strcasecmp(hits[hit].vars[i].name, name) == 0)
         return &(hits[hit].vars[i]);
   }

   printf("hit %d line %d: no variable %s\n", hit, hits[hit].line, name);
   fail_unless(false);
   return NULL;
}

static void expect(int hit, const char *name, int is_param, int kind,
                   const char *text, const char *bits)
{
   const saved_t *s = find(hit, name);
   if (s->is_param != is_param || s->kind != kind || strcmp(s->text, text)
       || (bits == NULL) != (s->bits == NULL)
       || (bits != NULL && strcmp(s->bits, bits)))
      printf("hit %d %s: param %d kind %d text '%s' bits '%s'\n", hit, name,
             s->is_param, s->kind, s->text, s->bits ? s->bits : "(null)");
   check_int(s->is_param, is_param);
   check_int(s->kind, kind);
   check_str(s->text, text);
   if (bits != NULL)
      check_str(s->bits, bits);
   else
      fail_unless(s->bits == NULL);
}

static void start_of_sim(const vhpiCbDataT *cb_data)
{
   vpi_nvc_set_stmt_cb(on_stmt, NULL);

   // Not in a statement callback
   check_int(vpi_nvc_frame_variables(0, on_var, NULL), -1);

   check_int(vpi_nvc_stmt_breakpoint_add("vhpi35.vhd", 25), 1);
   check_int(vpi_nvc_stmt_breakpoint_add("vhpi35.vhd", 40), 1);
   check_int(vpi_nvc_stmt_breakpoint_add("vhpi35.vhd", 47), 1);
   check_int(vpi_nvc_stmt_breakpoint_add("vhpi35.vhd", 57), 1);
   check_int(vpi_nvc_stmt_breakpoint_add("vhpi35.vhd", 66), 1);
   check_int(vpi_nvc_stmt_breakpoint_add("vhpi35.vhd", 74), 1);
}

static void end_of_sim(const vhpiCbDataT *cb_data)
{
   vpi_nvc_set_stmt_cb(NULL, NULL);

   for (int i = 0; i < nhits; i++) {
      printf("hit %d line %d:", i, hits[i].line);
      for (int j = 0; j < hits[i].count; j++)
         printf(" %s=%s", hits[i].vars[j].name, hits[i].vars[j].text);
      printf("\n");
   }

   check_int(nhits, 8);

   // scale (pt, 2): a record parameter, an integer and two variables
   check_int(hits[0].line, 25);
   check_int(hits[0].count, 4);
   fail_if(strcasecmp(hits[0].vars[0].name, "P"));
   fail_if(strcasecmp(hits[0].vars[1].name, "K"));
   expect(0, "P", 1, K_NONE, "(x => 4, y => 5, c => red)", NULL);
   expect(0, "K", 1, K_INTEGER, "2", "00000000000000000000000000000010");
   expect(0, "R", 0, K_INTEGER, "8", "00000000000000000000000000001000");
   expect(0, "Q", 0, K_INTEGER, "13", "00000000000000000000000000001101");

   // scale (p, 3) called from mix
   check_int(hits[1].line, 25);
   expect(1, "P", 1, K_NONE, "(x => 1, y => 2, c => blue)", NULL);
   expect(1, "K", 1, K_INTEGER, "3", "00000000000000000000000000000011");
   expect(1, "R", 0, K_INTEGER, "3", "00000000000000000000000000000011");
   expect(1, "Q", 0, K_INTEGER, "5", "00000000000000000000000000000101");

   // mix: unconstrained and constrained arrays, strings, real and time
   check_int(hits[2].line, 40);
   expect(2, "V", 1, K_LOGIC, "\"10Z1\"", "10z1");
   check_int(find(2, "V")->width, 4);
   expect(2, "W", 1, K_BIT, "\"0110\"", "0110");
   expect(2, "S", 1, K_STRING, "\"hi!\"",
          "011010000110100100100001");
   expect(2, "R", 1, K_REAL, "2.5",
          "0100000000000100000000000000000000000000000000000000000000000000");
   expect(2, "T", 1, K_TIME, "3 ns",
          "0000000000000000000000000000000000000000001011011100011011000000");
   expect(2, "ACC", 0, K_INTEGER, "16", "00000000000000000000000000010000");
   expect(2, "LV", 0, K_LOGIC, "\"10Z1\"", "10z1");
   expect(2, "COL", 0, K_ENUM, "green", "01");
   check_int(find(2, "COL")->nlits, 3);
   check_str(find(2, "COL")->lit0, "red");
   expect(2, "AR", 0, K_NONE, "(10, 20, 30)", NULL);
   expect(2, "SV", 0, K_STRING, "\"abc\"", "011000010110001001100011");
   expect(2, "P", 0, K_NONE, "(x => 1, y => 2, c => blue)", NULL);
   expect(2, "OK", 0, K_ENUM, "true", "1");

   // fact (1) at the bottom of the recursion
   check_int(hits[3].line, 47);
   check_int(hits[3].count, 2);
   expect(3, "N", 1, K_INTEGER, "1", "00000000000000000000000000000001");
   expect(3, "HERE", 0, K_INTEGER, "1", "00000000000000000000000000000001");

   // bump: an inout variable parameter and a constant, not the signal
   check_int(hits[4].line, 57);
   expect(4, "CNT", 1, K_INTEGER, "0", "00000000000000000000000000000000");
   expect(4, "STEP", 1, K_INTEGER, "5", "00000000000000000000000000000101");
   expect(4, "TMP", 0, K_INTEGER, "5", "00000000000000000000000000000101");

   // waits: after the wait, the variables held in the state of the call
   check_int(hits[5].line, 66);
   expect(5, "X", 1, K_INTEGER, "7", "00000000000000000000000000000111");
   expect(5, "LOCAL", 0, K_INTEGER, "15", "00000000000000000000000000001111");

   // init: variables initialised from a parameter, from another variable
   // and from a function call, at the first statement
   check_int(hits[7].line, 74);
   expect(7, "X", 1, K_INTEGER, "3", "00000000000000000000000000000011");
   expect(7, "R", 0, K_INTEGER, "3", "00000000000000000000000000000011");
   expect(7, "S", 0, K_INTEGER, "4", "00000000000000000000000000000100");
   expect(7, "T", 0, K_INTEGER, "8", "00000000000000000000000000001000");
}

void vhpi35_startup(void)
{
   vhpiCbDataT cb_data = {
      .reason = vhpiCbStartOfSimulation,
      .cb_rtn = start_of_sim,
   };
   VHPI_CHECK(vhpi_register_cb(&cb_data, 0));

   cb_data.reason = vhpiCbEndOfSimulation;
   cb_data.cb_rtn = end_of_sim;
   VHPI_CHECK(vhpi_register_cb(&cb_data, 0));
}
