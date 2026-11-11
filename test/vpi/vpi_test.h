#ifndef _VPI_TEST_H
#define _VPI_TEST_H

#include "vpi_user.h"
#include "vpi_ext_nvc.h"

#include <stdbool.h>
#include <stddef.h>

// SystemVerilog object types and properties from sv_vpi_user.h
// (IEEE 1800-2017 Annex M) used by the tests
#ifndef vpiPackage
#define vpiPackage          600
#define vpiInterface        601
#define vpiInterfaceArray   603
#define vpiModport          606
#define vpiRefObj           608
#define vpiLongIntVar       610
#define vpiShortIntVar      611
#define vpiIntVar           612
#define vpiByteVar          614
#define vpiStringVar        616
#define vpiEnumVar          617
#define vpiStructVar        618
#define vpiUnionVar         619
#define vpiBitVar           620
#define vpiLogicVar         vpiReg
#define vpiArrayVar         vpiRegArray
#define vpiPackedArrayVar   623
#define vpiPacked           630
#define vpiActual           700
#define vpiMember           742
#define vpiInstance         745
#endif

// Report a failure, print FAILED and end the simulation
void vpi_test_fail(const char *file, int line, const char *fmt, ...)
   __attribute__((format(printf, 3, 4)));

// Print PASSED unless a check failed
void vpi_test_pass(void);

#define fail_if(x)                                                      \
   do {                                                                 \
      if (x)                                                            \
         vpi_test_fail(__FILE__, __LINE__, "'%s' is true", #x);         \
   } while (0)
#define fail_unless(x) fail_if(!(x))

#define check_error() __check_error(__FILE__, __LINE__)
#define check_handle(h) __check_handle((h), __FILE__, __LINE__)
#define check_string(v, s) __check_string((v), (s), __FILE__, __LINE__)
#define check_int(v, n) __check_int((v), (n), __FILE__, __LINE__)

void __check_error(const char *file, int lineno);
vpiHandle __check_handle(vpiHandle h, const char *file, int lineno);
void __check_string(const char *v, const char *s, const char *file,
                    int lineno);
void __check_int(PLI_INT64 v, PLI_INT64 n, const char *file, int lineno);

// The connections of a signal found by vpi_nvc_connections, collected
typedef struct {
   vpi_nvc_conn_t c;
   char           name[96];
   char           kind[32];
   char           scope[96];
   char           file[96];
   char           other[96];
} vpi_test_conn_t;

typedef struct {
   int             n;          // Drivers
   vpi_test_conn_t item[16];
   int             nloads;
   vpi_test_conn_t load[16];
} vpi_test_conns_t;

// Ask for the connections of a path (first and count as in the call, 0 for
// all), fail on an error and return the number found
int vpi_test_connections(vpi_test_conns_t *out, const char *path, int first,
                         int count);

// The signals of a process found by vpi_nvc_process_signals, collected
typedef struct {
   int  n;
   struct {
      char path[96];
      int  first, count, width, write;
   } item[16];
} vpi_test_procsigs_t;

// Fails if there is no such process
int vpi_test_process_signals(vpi_test_procsigs_t *out, const char *scope,
                             const char *name);

// The connection that concerns the line of a file whose name ends as given,
// or NULL
const vpi_test_conn_t *vpi_test_conn_at(const vpi_test_conns_t *cs,
                                        const char *file, int line);

// The load that concerns the line of a file, as vpi_test_conn_at
const vpi_test_conn_t *vpi_test_load_at(const vpi_test_conns_t *cs,
                                        const char *file, int line);

// Register a callback with no object and an optional relative delay
vpiHandle vpi_test_cb(PLI_INT32 reason, PLI_INT32 (*fn)(p_cb_data),
                      PLI_UINT64 delay, void *user);

// Look up an object by full name and fail if it does not exist
vpiHandle vpi_test_lookup(const char *name);

// Current simulation time in simulator time units
PLI_UINT64 vpi_test_now(void);

void vpi1_startup(void);
void vpi2_startup(void);
void vpi3_startup(void);
void vpi4_startup(void);
void vpi5_startup(void);
void vpi6_startup(void);
void vpi7_startup(void);
void vpi8_startup(void);
void vpi9_startup(void);
void vpi10_startup(void);
void vpi11_startup(void);
void vpi12_startup(void);
void vpi13_startup(void);
void vpi14_startup(void);
void vpi15_startup(void);
void vpi16_startup(void);
void vpi17_startup(void);
void vpi18_startup(void);
void vpi19_startup(void);
void vpi20_startup(void);
void vpi21_startup(void);
void vpi22_startup(void);
void vpi23_startup(void);
void vpi24_startup(void);

#endif  // _VPI_TEST_H
