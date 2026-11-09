#include "vpi_test.h"

#include <string.h>

static int count = 0;
static int severities[8];
static char texts[8][64];
static int lines[8];

static void on_diag(int severity, const char *text, const char *file,
                    int line, void *user_data)
{
   if (count < 8) {
      severities[count] = severity;
      strncpy(texts[count], text, sizeof(texts[count]) - 1);
      lines[count] = line;
   }
   count++;
}

static PLI_INT32 start_of_sim(p_cb_data cb)
{
   vpi_nvc_set_diag_cb(on_diag, NULL);
   return 0;
}

static PLI_INT32 end_of_sim(p_cb_data cb)
{
   vpi_nvc_set_diag_cb(NULL, NULL);

   check_int(count, 3);
   check_int(severities[0], vpiNvcNote);
   fail_if(strstr(texts[0], "just so you know") == NULL);
   check_int(lines[0], 12);
   check_int(severities[1], vpiNvcWarning);
   fail_if(strstr(texts[1], "careful") == NULL);
   check_int(lines[1], 14);
   check_int(severities[2], vpiNvcWarning);
   fail_if(strstr(texts[2], "again") == NULL);

   vpi_test_pass();
   return 0;
}

void vpi15_startup(void)
{
   vpi_test_cb(cbStartOfSimulation, start_of_sim, 0, NULL);
   vpi_test_cb(cbEndOfSimulation, end_of_sim, 0, NULL);
}
