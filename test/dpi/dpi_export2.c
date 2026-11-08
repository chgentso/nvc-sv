//
// C side of the DPI export tests svdpiexp2 .. svdpiexp6 (IEEE 1800-2017
// 35.5.3, 35.6, 35.7, 35.9, Annex H.9 / H.10).
//

#include <stdio.h>
#include <stdlib.h>

#include "svdpi.h"

////////////////////////////////////////////////////////////////////////////////
// svdpiexp2: exported tasks which consume time, called from imported
// context tasks

extern int sv_delay(int n, int *now);
extern int sv_wait_ev(int *value);
extern int sv_nodelay(int a, int *b);

// Calls the exported task 'count' times with delay 'step'; the export
// returns the simulation time after its delay, which must increase by
// 'step' each time.  The number of inconsistencies found goes to *bad (the
// return value of an imported task is its disable status, 35.9).
int dpi_tdelay(int step, int count, int *last, int *bad_out)
{
   int bad = 0, prev = -1;
   for (int i = 0; i < count; i++) {
      int now = -1;
      if (sv_delay(step, &now) != 0)
         bad++;
      if (prev >= 0 && now != prev + step)
         bad++;
      prev = now;
   }
   *last = prev;
   *bad_out = bad;
   return 0;
}

// Blocks in the exported task until an event delivers a value
int dpi_twait(int *value)
{
   *value = -1;
   return sv_wait_ev(value);
}

// An exported task which does not consume time, mixed with one which does
int dpi_tmix(int a, int *r)
{
   int b = 0, now = 0;
   sv_nodelay(a, &b);
   sv_delay(1, &now);
   *r = b * 1000 + now;
   return 0;
}

////////////////////////////////////////////////////////////////////////////////
// svdpiexp3: disabling an imported task blocked in an exported task (35.9)

extern int sv_block(void);

static int dis_ret = -1, dis_state = -1, dis_after_ack = -1;
static int nodis_ret = -1, nodis_state = -1;

int dpi_blocking(void)
{
   const int r = sv_block();
   if (r) {
      dis_ret = r;
      dis_state = svIsDisabledState();
      svAckDisabledState();
      dis_after_ack = svIsDisabledState();
      return 1;   // Acknowledge the disable (35.9)
   }
   else {
      nodis_ret = r;
      nodis_state = svIsDisabledState();
      return 0;
   }
}

// Packs what dpi_blocking saw: 100 * export return + 10 * disabled state
// + state after acknowledging, for the disabled and the normal call
int dpi_dis_log(int which)
{
   if (which == 0)
      return 100 * dis_ret + 10 * dis_state + dis_after_ack;
   else
      return 100 * nodis_ret + 10 * nodis_state;
}

////////////////////////////////////////////////////////////////////////////////
// svdpiexp4: exported functions of a package, called during and after
// static initialisation

extern int pk2_scale(int a);
extern int pk2_base(void);

int dpi_pk2_scale(int a)
{
   return pk2_scale(a);
}

int dpi_pk2_base(void)
{
   return pk2_base();
}

////////////////////////////////////////////////////////////////////////////////
// svdpiexp5: a non-context import calling an export (35.5.3)

extern int sv_nc_target(int a);

int dpi_noctx_call(int a)
{
   return sv_nc_target(a);
}

////////////////////////////////////////////////////////////////////////////////
// svdpiexp6: an imported function calling an exported task (35.7)

extern int sv_ftask(int a);

int dpi_func_calls_task(int a)
{
   sv_ftask(a);
   return a;
}
