#include "vpi_test.h"

#include <stdlib.h>
#include <string.h>

#define MAX_HITS 8
#define MAX_VARS 16

typedef struct {
   char *name;
   int   is_param, kind, width;
   char *bits, *text;
} saved_t;

static struct {
   int     line;
   int     count;
   saved_t vars[MAX_VARS];
} hits[MAX_HITS];
static int nhits;

static void on_var(const vpi_nvc_var_t *v, void *user_data)
{
   int *n = user_data;
   fail_unless(*n < MAX_VARS);
   saved_t *s = &(hits[nhits].vars[(*n)++]);
   s->name = strdup(v->name);
   s->is_param = v->is_param;
   s->kind = v->kind;
   s->width = v->width;
   s->bits = v->bits ? strdup(v->bits) : NULL;
   s->text = strdup(v->text);
}

static void on_stmt(const char *file, int line, const char *process,
                    const char *scope, void *user_data)
{
   fail_unless(nhits < MAX_HITS);

   int n = 0;
   const int ret = vpi_nvc_frame_variables(0, on_var, &n);
   check_int(ret, n);
   check_int(vpi_nvc_frame_variables(1, on_var, &n), 0);

   hits[nhits].line = line;
   hits[nhits].count = n;
   nhits++;
}

static const saved_t *find(int hit, const char *name)
{
   for (int i = 0; i < hits[hit].count; i++) {
      if (strcmp(hits[hit].vars[i].name, name) == 0)
         return &(hits[hit].vars[i]);
   }

   vpi_printf("hit %d line %d: no variable %s\n", hit, hits[hit].line, name);
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
      vpi_printf("hit %d %s: param %d kind %d text '%s' bits '%s'\n", hit,
                 name, s->is_param, s->kind, s->text,
                 s->bits ? s->bits : "(null)");
   check_int(s->is_param, is_param);
   check_int(s->kind, kind);
   check_string(s->text, text);
   if (bits != NULL)
      check_string(s->bits, bits);
   else
      fail_unless(s->bits == NULL);
}

static PLI_INT32 start_of_sim(p_cb_data cb)
{
   vpi_nvc_set_stmt_cb(on_stmt, NULL);

   // Not in a statement callback
   check_int(vpi_nvc_frame_variables(0, on_var, NULL), -1);

   check_int(vpi_nvc_stmt_breakpoint_add("vpi26.sv", 18), 1);
   check_int(vpi_nvc_stmt_breakpoint_add("vpi26.sv", 24), 1);
   check_int(vpi_nvc_stmt_breakpoint_add("vpi26.sv", 42), 1);
   check_int(vpi_nvc_stmt_breakpoint_add("vpi26.sv", 30), 1);
   return 0;
}

static PLI_INT32 end_of_sim(p_cb_data cb)
{
   vpi_nvc_set_stmt_cb(NULL, NULL);

   check_int(nhits, 5);

   // mix: the arguments, the result and the locals
   check_int(hits[0].line, 18);
   check_int(hits[0].count, 8);
   check_string(hits[0].vars[0].name, "a");
   expect(0, "a", 1, vpiNvcVarInteger, "5", "00000000000000000000000000000101");
   expect(0, "b", 1, vpiNvcVarLogic, "240", "11110000");
   expect(0, "r", 1, vpiNvcVarReal, "2.5",
          "0100000000000100000000000000000000000000000000000000000000000000");
   expect(0, "s", 1, vpiNvcVarString, "\"hey\"", "011010000110010101111001");
   expect(0, "mix", 0, vpiNvcVarInteger, "0",
          "00000000000000000000000000000000");
   expect(0, "sum", 0, vpiNvcVarInteger, "6",
          "00000000000000000000000000000110");
   expect(0, "x", 0, vpiNvcVarLogic, "255", "11111111");
   expect(0, "flag", 0, vpiNvcVarBit, "1", "1");

   // The second call: the code may have been compiled by now
   check_int(hits[1].line, 18);
   expect(1, "a", 1, vpiNvcVarInteger, "7", "00000000000000000000000000000111");
   expect(1, "b", 1, vpiNvcVarLogic, "1", "00000001");
   expect(1, "r", 1, vpiNvcVarReal, "1.5",
          "0011111111111000000000000000000000000000000000000000000000000000");
   expect(1, "s", 1, vpiNvcVarString, "\"yo\"", "0111100101101111");
   expect(1, "sum", 0, vpiNvcVarInteger, "8",
          "00000000000000000000000000001000");
   expect(1, "x", 0, vpiNvcVarLogic, "14", "00001110");

   // wide: vectors of more than 64 bits, a signed byte and the unknown
   // initial value of the result
   check_int(hits[2].line, 24);
   expect(2, "c", 1, vpiNvcVarInteger, "-2", "11111110");
   check_int(find(2, "v")->width, 100);
   check_string(find(2, "v")->text, "100'h3");
   check_string(find(2, "w")->text, "100'h6");
   check_int(find(2, "w")->kind, vpiNvcVarLogic);
   fail_unless(strchr(find(2, "wide")->text, 'x') != NULL);

   // initv: variables initialised from an argument and from another variable
   check_int(hits[3].line, 30);
   expect(3, "x", 1, vpiNvcVarInteger, "6", "00000000000000000000000000000110");
   expect(3, "r", 0, vpiNvcVarInteger, "6", "00000000000000000000000000000110");
   expect(3, "s", 0, vpiNvcVarInteger, "7", "00000000000000000000000000000111");

   // waiter, after the wait: an input and an output argument, the locals
   check_int(hits[4].line, 42);
   expect(4, "n", 1, vpiNvcVarInteger, "4", "00000000000000000000000000000100");
   expect(4, "result", 1, vpiNvcVarInteger, "8",
          "00000000000000000000000000001000");
   expect(4, "i", 0, vpiNvcVarInteger, "8", "00000000000000000000000000001000");
   expect(4, "nib", 0, vpiNvcVarLogic, "4'b10x1", "10x1");

   vpi_test_pass();
   return 0;
}

void vpi26_startup(void)
{
   vpi_test_cb(cbStartOfSimulation, start_of_sim, 0, NULL);
   vpi_test_cb(cbEndOfSimulation, end_of_sim, 0, NULL);
}
