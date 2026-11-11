#include "vhpi_test.h"

#include <string.h>
#include <strings.h>

// The statement breakpoints and call stacks of the nvc VPI extension
// (vpi_ext_nvc.h)
typedef void (*stmt_fn_t)(const char *file, int line, const char *process,
                          const char *scope, void *user_data);
void vpi_nvc_set_stmt_cb(stmt_fn_t fn, void *user_data);
int vpi_nvc_stmt_breakpoint_add(const char *file, int line);
int vpi_nvc_stmt_step(int enable);
int vpi_nvc_stmt_step_process(void);
int vpi_nvc_stmt_site_query(const char *file, int line, int *nearest);

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

typedef void (*diag_fn_t)(int severity, const char *text, const char *file,
                          int line, void *user_data);
void vpi_nvc_set_diag_cb(diag_fn_t fn, void *user_data);

#define check_int(a, b) fail_unless((long)(a) == (long)(b))
#define check_str(a, b) fail_unless(strcasecmp((a), (b)) == 0)

#define MAX_FRAMES 8
#define MAX_HITS   64

typedef struct {
   int  line;
   int  nframes;
   char process[32];
   int  user;
   struct {
      char kind[16];
      char name[32];
      char file[256];
      int  line;
   } frames[MAX_FRAMES];
} hit_t;

static hit_t hits[MAX_HITS];
static int   nhits;
static hit_t dhits[4];
static int   ndiags;
static int   phase2;

static int ends_with(const char *s, const char *suffix)
{
   const size_t slen = strlen(s), tlen = strlen(suffix);
   return slen >= tlen && strcasecmp(s + slen - tlen, suffix) == 0;
}

static void on_frame(const frame_t *f, void *user_data)
{
   hit_t *h = user_data;
   fail_unless(f->index == h->nframes);
   fail_if(f->symbol == NULL);
   if (h->nframes < MAX_FRAMES) {
      strncpy(h->frames[h->nframes].kind, f->kind, 15);
      strncpy(h->frames[h->nframes].name, f->name, 31);
      strncpy(h->frames[h->nframes].file, f->file ?: "", 255);
      h->frames[h->nframes].line = f->line;
   }
   h->nframes++;
}

static void on_diag(int severity, const char *text, const char *file,
                    int line, void *user_data)
{
   if (ndiags < 4) {
      dhits[ndiags].line = line;
      const int n = vpi_nvc_get_frames(on_frame, &(dhits[ndiags]));
      check_int(n, dhits[ndiags].nframes);
   }
   ndiags++;
}

static void on_stmt(const char *file, int line, const char *process,
                    const char *scope, void *user_data)
{
   // Stepping in one process never stops outside a process: the function
   // that evaluates the condition of a wait for the scheduler is not run by
   // one, the call of it in the process itself is
   fail_if(process == NULL);
   fail_unless(phase2 > 0 || ends_with(process, ":p"));

   if (nhits < MAX_HITS) {
      hit_t *h = &hits[nhits];
      h->line = line;
      strncpy(h->process, process, sizeof(h->process) - 1);
      h->user = strstr(file, "vhpi33.vhd") != NULL;
      const int n = vpi_nvc_get_frames(on_frame, h);
      check_int(n, h->nframes);
   }
   nhits++;

   if (nhits == 1) {
      check_int(vpi_nvc_stmt_step(1), 1);
      check_int(vpi_nvc_stmt_step_process(), 1);
   }
   else if (line == 43 && strstr(file, "vhpi33.vhd") != NULL) {
      // Step in any process now: q runs, the scheduler evaluates the wait
      // of p, and that is not a stop
      check_int(vpi_nvc_stmt_step(1), 1);
      phase2 = nhits;
   }
   else if (line == 44 && strstr(file, "vhpi33.vhd") != NULL)
      check_int(vpi_nvc_stmt_step(0), 1);
}

static void start_of_sim(const vhpiCbDataT *cb_data)
{
   vpi_nvc_set_stmt_cb(on_stmt, NULL);
   vpi_nvc_set_diag_cb(on_diag, NULL);

   // The code has to be generated with --debug-stmts
   check_int(vpi_nvc_stmt_breakpoint_add("vhpi33.vhd", 21), 1);

   int nearest;

   // A line with a statement
   check_int(vpi_nvc_stmt_site_query("vhpi33.vhd", 21, &nearest), 3);
   check_int(nearest, 21);
   check_int(vpi_nvc_stmt_site_query("regress/vhpi33.vhd", 41, &nearest), 3);
   check_int(nearest, 41);

   // A declaration, a comment and a blank line: the next statement
   check_int(vpi_nvc_stmt_site_query("vhpi33.vhd", 19, &nearest), 2);
   check_int(nearest, 21);
   check_int(vpi_nvc_stmt_site_query("vhpi33.vhd", 31, &nearest), 2);
   check_int(nearest, 36);
   check_int(vpi_nvc_stmt_site_query("vhpi33.vhd", 33, &nearest), 2);
   check_int(nearest, 36);
   check_int(vpi_nvc_stmt_site_query("vhpi33.vhd", 37, &nearest), 2);
   check_int(nearest, 40);

   // Past the last statement, and another file
   check_int(vpi_nvc_stmt_site_query("vhpi33.vhd", 99, &nearest), 2);
   check_int(nearest, 0);
   check_int(vpi_nvc_stmt_site_query("other.vhd", 21, &nearest), 0);
   check_int(nearest, 0);
}

static void end_of_sim(const vhpiCbDataT *cb_data)
{
   vpi_nvc_set_stmt_cb(NULL, NULL);

   // The frames of a report have the line of the statement, in the
   // subprogram and in the process
   check_int(ndiags, 2);
   check_int(dhits[0].line, 77);
   check_int(dhits[0].nframes, 1);
   check_str(dhits[0].frames[0].kind, "process");
   check_int(dhits[0].frames[0].line, 77);
   check_int(dhits[1].line, 71);
   check_int(dhits[1].nframes, 2);
   check_str(dhits[1].frames[0].name, "f");
   check_int(dhits[1].frames[0].line, 71);
   check_int(dhits[1].frames[1].line, 78);

   // Outside a callback there is no stack
   check_int(vpi_nvc_get_frames(on_frame, NULL), -1);

   // The statement callback of the statement in the innermost subprogram: the
   // callers are at the calls
   check_int(hits[0].line, 21);
   check_int(hits[0].nframes, 3);
   check_str(hits[0].frames[0].kind, "function");
   check_str(hits[0].frames[0].name, "INNER");
   check_int(hits[0].frames[0].line, 21);
   check_str(hits[0].frames[1].kind, "procedure");
   check_str(hits[0].frames[1].name, "OUTER");
   check_int(hits[0].frames[1].line, 28);
   check_str(hits[0].frames[2].kind, "process");
   check_str(hits[0].frames[2].name, ":vhpi33:p");
   check_int(hits[0].frames[2].line, 41);
   fail_if(strstr(hits[0].frames[0].file, "vhpi33.vhd") == NULL);
   fail_if(strstr(hits[0].frames[2].file, "vhpi33.vhd") == NULL);

   // Stepping in the process: the next statements, in order
   check_int(hits[1].line, 22);
   check_int(hits[1].nframes, 3);
   check_int(hits[1].frames[0].line, 22);
   check_int(hits[1].frames[1].line, 28);

   check_int(hits[2].line, 29);
   check_int(hits[2].nframes, 2);
   check_str(hits[2].frames[0].name, "OUTER");
   check_int(hits[2].frames[0].line, 29);
   check_int(hits[2].frames[1].line, 41);

   check_int(hits[3].line, 42);
   check_int(hits[3].nframes, 1);
   check_int(hits[3].frames[0].line, 42);

   // The condition of the wait is evaluated by rising_edge in the library,
   // called from that process (not by the scheduler: that would have no
   // process and the callback would not be called); it has the wait as caller
   fail_unless(phase2 > 5 && phase2 <= MAX_HITS);
   for (int i = 4; i < phase2 - 1; i++) {
      fail_if(hits[i].user);
      fail_unless(hits[i].nframes >= 2);
      const int outer = hits[i].nframes - 1;
      check_str(hits[i].frames[outer].kind, "process");
      check_int(hits[i].frames[outer].line, 42);
   }
   check_str(hits[4].frames[0].name, "RISING_EDGE");

   check_int(hits[phase2 - 1].line, 43);
   check_int(hits[phase2 - 1].nframes, 1);
   fail_unless(hits[phase2 - 1].user);

   // Stepping in all processes: the others run between, the waits of p are not
   // stops (the callback is never called without a process)
   int nq = 0;
   for (int i = 0; i < phase2 - 1; i++)
      fail_if(ends_with(hits[i].process, ":q"));
   for (int i = phase2; i < nhits && i < MAX_HITS; i++)
      nq += !ends_with(hits[i].process, ":p");
   fail_unless(nq >= 1);

   const int last = nhits - 1;
   check_int(hits[last].line, 44);
   check_int(hits[last].nframes, 1);
   fail_unless(hits[last].user);
}

void vhpi33_startup(void)
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
