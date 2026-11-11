#include "vhpi_test.h"

#include <stdio.h>
#include <string.h>
#include <strings.h>

// Depth-aware stepping of the nvc VPI extension (vpi_ext_nvc.h)
typedef void (*stmt_fn_t)(const char *file, int line, const char *process,
                          const char *scope, void *user_data);
void vpi_nvc_set_stmt_cb(stmt_fn_t fn, void *user_data);
int vpi_nvc_stmt_breakpoint_add(const char *file, int line);
int vpi_nvc_stmt_breakpoint_remove(const char *file, int line);
int vpi_nvc_stmt_step(int enable);
int vpi_nvc_stmt_step_depth(int mode);

typedef struct {
   int         index;
   const char *kind;
   const char *name;
   const char *symbol;
   const char *file;
   int         line;
} frame_t;

typedef void (*frame_fn_t)(const frame_t *frame, void *user_data);
int vpi_nvc_get_frames(frame_fn_t fn, void *user_data);

#define check_int(a, b) fail_unless((long)(a) == (long)(b))

#define IN   0
#define OVER 1
#define OUT  2
#define STOP 3

#define MAX_HITS 64

static struct {
   int line;
   int depth;
   int mode;     // What the step that follows is
} script[] = {
   // Over a call of a function and of a recursive function
   { 54, 1, OVER },
   { 55, 1, OVER },
   { 56, 1, OVER },
   // Into outer, twice and inc, and out of them one at a time
   { 57, 1, IN },
   { 38, 2, IN },
   { 23, 3, IN },
   { 17, 4, OUT },
   { 24, 3, OUT },
   { 39, 2, OUT },
   // Over a procedure that waits: the breakpoint inside it stops, and out
   // of it from there returns to the caller
   { 58, 1, OVER },
   { 45, 2, OUT },
   // Over the same procedure without the breakpoint: the other process runs
   // meanwhile and is not stopped in
   { 59, 1, OVER },
   // Out at the process itself is a step over
   { 60, 1, OUT },
   { 61, 1, OVER },
   { 62, 1, STOP },
};

#define NSCRIPT ((int)(sizeof(script) / sizeof(script[0])))

static int nhits;
static int lines[MAX_HITS];
static int depths[MAX_HITS];
static int bad;

static void on_frame(const frame_t *f, void *user_data)
{
   (*(int *)user_data)++;
}

static void on_stmt(const char *file, int line, const char *process,
                    const char *scope, void *user_data)
{
   fail_if(process == NULL);

   int depth = 0;
   const int n = vpi_nvc_get_frames(on_frame, &depth);
   check_int(n, depth);

   const int i = nhits++;
   if (i >= MAX_HITS)
      return;

   lines[i] = line;
   depths[i] = depth;

   // Only the process p is ever stepped, the breakpoint is in it as well
   if (strcasecmp(process + strlen(process) - 2, ":p") != 0)
      bad++;

   if (i >= NSCRIPT)
      return;

   if (script[i].line != line || script[i].depth != depth)
      return;   // Reported at the end; carry on in the same way

   switch (script[i].mode) {
   case STOP:
      check_int(vpi_nvc_stmt_step(0), 1);
      // Not stepping: nothing to limit
      check_int(vpi_nvc_stmt_step_depth(OVER), 0);
      break;
   default:
      if (line == 45)
         check_int(vpi_nvc_stmt_breakpoint_remove("vhpi34.vhd", 45), 1);
      check_int(vpi_nvc_stmt_step(1), 1);
      check_int(vpi_nvc_stmt_step_depth(7), 0);
      check_int(vpi_nvc_stmt_step_depth(-1), 0);
      check_int(vpi_nvc_stmt_step_depth(script[i].mode), 1);
      break;
   }
}

static void start_of_sim(const vhpiCbDataT *cb_data)
{
   vpi_nvc_set_stmt_cb(on_stmt, NULL);

   // The code has to be generated with --debug-stmts
   check_int(vpi_nvc_stmt_breakpoint_add("vhpi34.vhd", 54), 1);
   check_int(vpi_nvc_stmt_breakpoint_add("vhpi34.vhd", 45), 1);

   // Not in a statement callback
   check_int(vpi_nvc_stmt_step_depth(OVER), 0);
}

static void end_of_sim(const vhpiCbDataT *cb_data)
{
   vpi_nvc_set_stmt_cb(NULL, NULL);

   fail_unless(nhits <= MAX_HITS);
   check_int(bad, 0);
   for (int i = 0; i < NSCRIPT; i++) {
      if (script[i].line != lines[i] || script[i].depth != depths[i])
         printf("hit %d: line %d depth %d, expected line %d depth %d\n",
                i, lines[i], depths[i], script[i].line, script[i].depth);
      check_int(lines[i], script[i].line);
      check_int(depths[i], script[i].depth);
   }

   // Stepping ended with the last: no more stops
   check_int(nhits, NSCRIPT);
}

void vhpi34_startup(void)
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
