#include "vpi_test.h"

#include <string.h>

#define IN   0
#define OVER 1
#define OUT  2
#define STOP 3

#define MAX_HITS 64

static const struct {
   int line;
   int depth;
   int mode;     // What the step that follows is
} script[] = {
   // Over a call of a function
   { 24, 1, OVER },
   { 25, 1, OVER },
   // Over a task that waits for two clock edges, with a breakpoint in it:
   // that stops in the task, and out of it from there returns to the caller
   { 26, 1, OVER },
   { 18, 2, OUT },
   // Over the same task without the breakpoint, the clock and the other
   // process run meanwhile
   { 27, 1, OVER },
   { 28, 1, OVER },
   { 29, 1, OVER },
   // Into the task, the wait is a step, and out of it after the edges
   { 30, 1, IN },
   { 17, 2, IN },
   { 18, 2, OUT },
   { 31, 1, STOP },
};

#define NSCRIPT ((int)(sizeof(script) / sizeof(script[0])))

static int  nhits;
static int  lines[MAX_HITS];
static int  depths[MAX_HITS];
static int  other;
static char first[128];

static void on_frame(const vpi_nvc_frame_t *f, void *user_data)
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

   // Only the first initial block is stepped, and the breakpoint is in it
   if (i == 0)
      strncpy(first, process, sizeof(first) - 1);
   else if (strcmp(first, process) != 0)
      other++;

   if (i >= NSCRIPT || script[i].line != line || script[i].depth != depth)
      return;   // Reported at the end

   if (script[i].mode == STOP) {
      check_int(vpi_nvc_stmt_step(0), 1);
      // Not stepping: nothing to limit
      check_int(vpi_nvc_stmt_step_depth(vpiNvcStepOver), 0);
   }
   else {
      if (i == 3)
         check_int(vpi_nvc_stmt_breakpoint_remove("vpi25.sv", 18), 1);
      check_int(vpi_nvc_stmt_step(1), 1);
      check_int(vpi_nvc_stmt_step_depth(7), 0);
      check_int(vpi_nvc_stmt_step_depth(script[i].mode), 1);
   }
}

static PLI_INT32 start_of_sim(p_cb_data cb)
{
   vpi_nvc_set_stmt_cb(on_stmt, NULL);

   check_int(vpi_nvc_stmt_breakpoint_add("vpi25.sv", 24), 1);
   check_int(vpi_nvc_stmt_breakpoint_add("vpi25.sv", 18), 1);

   // Not in a statement callback
   check_int(vpi_nvc_stmt_step_depth(vpiNvcStepOver), 0);
   return 0;
}

static PLI_INT32 end_of_sim(p_cb_data cb)
{
   vpi_nvc_set_stmt_cb(NULL, NULL);

   fail_unless(nhits <= MAX_HITS);
   check_int(other, 0);
   for (int i = 0; i < NSCRIPT; i++) {
      if (script[i].line != lines[i] || script[i].depth != depths[i])
         vpi_printf("hit %d: line %d depth %d, expected line %d depth %d\n",
                    i, lines[i], depths[i], script[i].line, script[i].depth);
      check_int(lines[i], script[i].line);
      check_int(depths[i], script[i].depth);
   }

   // Stepping ended with the last: no more stops
   check_int(nhits, NSCRIPT);

   vpi_test_pass();
   return 0;
}

void vpi25_startup(void)
{
   vpi_test_cb(cbStartOfSimulation, start_of_sim, 0, NULL);
   vpi_test_cb(cbEndOfSimulation, end_of_sim, 0, NULL);
}
