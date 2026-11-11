#include "vpi_test.h"

#include <string.h>

#define MAX_FRAMES 8

typedef struct {
   int  line;
   int  nframes;
   struct {
      char kind[16];
      char name[64];
      char file[256];
      int  line;
   } frames[MAX_FRAMES];
} hit_t;

static hit_t hits[16];
static int nhits;

static void on_frame(const vpi_nvc_frame_t *f, void *user_data)
{
   hit_t *h = user_data;
   fail_if(f->index != h->nframes);
   if (h->nframes < MAX_FRAMES) {
      strncpy(h->frames[h->nframes].kind, f->kind, 15);
      strncpy(h->frames[h->nframes].name, f->name, 63);
      strncpy(h->frames[h->nframes].file, f->file ?: "", 255);
      h->frames[h->nframes].line = f->line;
   }
   h->nframes++;
}

static void on_stmt(const char *file, int line, const char *process,
                    const char *scope, void *user_data)
{
   fail_if(process == NULL);
   if (nhits < 16) {
      hits[nhits].line = line;
      const int n = vpi_nvc_get_frames(on_frame, &(hits[nhits]));
      check_int(n, hits[nhits].nframes);
   }
   nhits++;
}

static PLI_INT32 start_of_sim(p_cb_data cb)
{
   vpi_nvc_set_stmt_cb(on_stmt, NULL);

   check_int(vpi_nvc_stmt_breakpoint_add("vpi24.sv", 11), 1);
   check_int(vpi_nvc_stmt_breakpoint_add("vpi24.sv", 16), 1);

   PLI_INT32 nearest = -1;
   check_int(vpi_nvc_stmt_site_query("vpi24.sv", 11, &nearest), 3);
   check_int(nearest, 11);

   // later has not been called yet, but its statements are known
   check_int(vpi_nvc_stmt_site_query("regress/vpi24.sv", 16, &nearest), 3);
   check_int(nearest, 16);

   // Not statements: the next one
   check_int(vpi_nvc_stmt_site_query("vpi24.sv", 12, &nearest), 2);
   check_int(nearest, 16);
   check_int(vpi_nvc_stmt_site_query("vpi24.sv", 14, &nearest), 2);
   check_int(nearest, 16);
   check_int(vpi_nvc_stmt_site_query("vpi24.sv", 24, &nearest), 2);
   check_int(nearest, 26);
   check_int(vpi_nvc_stmt_site_query("vpi24.sv", 99, &nearest), 2);
   check_int(nearest, 0);
   check_int(vpi_nvc_stmt_site_query("other.sv", 11, &nearest), 0);
   return 0;
}

static PLI_INT32 end_of_sim(p_cb_data cb)
{
   vpi_nvc_set_stmt_cb(NULL, NULL);

   check_int(nhits, 3);

   // Called in a task called in the initial block: the callers are at
   // their calls
   check_int(hits[0].line, 11);
   check_int(hits[0].nframes, 3);
   fail_if(strcmp(hits[0].frames[0].kind, "function"));
   fail_if(strcmp(hits[0].frames[0].name, "sq"));
   check_int(hits[0].frames[0].line, 11);
   fail_if(strcmp(hits[0].frames[1].kind, "task"));
   fail_if(strcmp(hits[0].frames[1].name, "work"));
   check_int(hits[0].frames[1].line, 21);
   fail_if(strcmp(hits[0].frames[2].kind, "initial"));
   fail_if(strstr(hits[0].frames[2].name, "initial") == NULL);
   check_int(hits[0].frames[2].line, 27);
   fail_if(strstr(hits[0].frames[0].file, "vpi24.sv") == NULL);

   check_int(hits[1].line, 11);
   check_int(hits[1].nframes, 3);
   check_int(hits[1].frames[2].line, 29);

   check_int(hits[2].line, 16);
   check_int(hits[2].nframes, 2);
   fail_if(strcmp(hits[2].frames[0].name, "later"));
   check_int(hits[2].frames[1].line, 30);

   vpi_test_pass();
   return 0;
}

void vpi24_startup(void)
{
   vpi_test_cb(cbStartOfSimulation, start_of_sim, 0, NULL);
   vpi_test_cb(cbEndOfSimulation, end_of_sim, 0, NULL);
}
