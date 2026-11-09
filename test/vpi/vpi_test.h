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

#endif  // _VPI_TEST_H
