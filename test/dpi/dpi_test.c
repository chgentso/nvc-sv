#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "svdpi.h"

int dpi_add(int a, int b)
{
   return a + b;
}

long long dpi_ladd(long long a, long long b)
{
   return a + b;
}

unsigned int dpi_uadd(unsigned int a, unsigned int b)
{
   return a + b;
}

signed char dpi_neg8(signed char a)
{
   return -a;
}

short dpi_neg16(short a)
{
   return -a;
}

unsigned char dpi_inc8(unsigned char a)
{
   return a + 1;
}

double dpi_scale(double a, double b)
{
   return a * b;
}

float dpi_halve(float a)
{
   return a / 2;
}

svBit dpi_not(svBit a)
{
   return !a;
}

svLogic dpi_lnot(svLogic a)
{
   switch (a) {
   case sv_0: return sv_1;
   case sv_1: return sv_0;
   default: return sv_x;
   }
}

void dpi_divmod(int a, int b, int *q, int *r)
{
   *q = a / b;
   *r = a % b;
}

void dpi_inc(int *x)
{
   (*x)++;
}

int dpi_popcount(const svBitVecVal *v)
{
   return __builtin_popcount(v[0]) + __builtin_popcount(v[1] & 0xff);
}

void dpi_bv_rev(const svBitVecVal *in, svBitVecVal *out)
{
   out[0] = ~in[0];
   out[1] = ~in[1] & 0xff;
}

void dpi_lv_swap(const svLogicVecVal *in, svLogicVecVal *out)
{
   // Swap the value and unknown bits of an eight bit logic vector
   out->aval = in->bval & 0xff;
   out->bval = in->aval & 0xff;
}

int dpi_lv_known(const svLogicVecVal *in)
{
   return (in->bval & 0xff) == 0;
}

void dpi_lv_inout(svLogicVecVal *v)
{
   v->aval ^= 0xff;
}

void dpi_wide(const svBitVecVal *in, svBitVecVal *out)
{
   out[0] = in[1];
   out[1] = in[0];
}

const char *dpi_greet(const char *name)
{
   static char buf[64];
   snprintf(buf, sizeof(buf), "hello %s", name);
   return buf;
}

int dpi_strlen(const char *s)
{
   return strlen(s);
}

void dpi_upper(const char *in, const char **out)
{
   static char buf[64];
   size_t i;
   for (i = 0; in[i] && i < sizeof(buf) - 1; i++)
      buf[i] = in[i] >= 'a' && in[i] <= 'z' ? in[i] - 32 : in[i];
   buf[i] = 0;
   *out = buf;
}

void *dpi_new(int n)
{
   int *p = malloc(sizeof(int));
   *p = n;
   return p;
}

int dpi_get(void *h)
{
   return *(int *)h;
}

void dpi_set(void *h, int n)
{
   *(int *)h = n;
}

void dpi_free(void *h)
{
   free(h);
}

int dpi_counter(void)
{
   static int count;
   return ++count;
}

int dpi_task_sum(int a, int b, int *sum)
{
   *sum = a + b;
   return 0;
}

void dpi_lwide(const svLogicVecVal *in, svLogicVecVal *out)
{
   // Rotate the words of a 100 bit logic vector and set the top unknown
   for (int i = 0; i < 4; i++)
      out[i] = in[(i + 1) % 4];

   out[3].aval &= 0xf;
   out[3].bval &= 0xf;
   out[0].bval |= 0x80000000;
}

int dpi_bwide(const svBitVecVal *in, svBitVecVal *inout)
{
   int count = 0;
   for (int i = 0; i < 4; i++) {
      count += __builtin_popcount(in[i]);
      inout[i] += in[i];
   }

   return count;
}

void dpi_lw_inout(svLogicVecVal *v)
{
   v[1].aval = ~v[1].aval;
}

extern int sv_bump(int delta);
#ifdef _WIN32
#define sv_bump (*(int (*)(int delta))vpi_nvc_get_export("sv_bump"))
#endif
extern void sv_store(int value, int *old);
#ifdef _WIN32
#define sv_store (*(void (*)(int value, int *old))vpi_nvc_get_export("sv_store"))
#endif
extern int sv_task(int a, int b, int *result);
#ifdef _WIN32
#define sv_task (*(int (*)(int a, int b, int *result))vpi_nvc_get_export("sv_task"))
#endif
extern long long sv_wide(long long a, unsigned char b);
#ifdef _WIN32
#define sv_wide (*(long long (*)(long long a, unsigned char b))vpi_nvc_get_export("sv_wide"))
#endif
extern svLogic sv_lnot(svLogic a);
#ifdef _WIN32
#define sv_lnot (*(svLogic (*)(svLogic a))vpi_nvc_get_export("sv_lnot"))
#endif
extern void sv_lvec(const svLogicVecVal *in, svLogicVecVal *out);
#ifdef _WIN32
#define sv_lvec (*(void (*)(const svLogicVecVal *in, svLogicVecVal *out))vpi_nvc_get_export("sv_lvec"))
#endif
extern const char *sv_greet(const char *name, const char **upper);
#ifdef _WIN32
#define sv_greet (*(const char * (*)(const char *name, const char **upper))vpi_nvc_get_export("sv_greet"))
#endif
extern int pkg_twice(int a);
#ifdef _WIN32
#define pkg_twice (*(int (*)(int a))vpi_nvc_get_export("pkg_twice"))
#endif

int dpi_call_bump(int delta)
{
   return sv_bump(delta);
}

int dpi_call_in(const char *path, int delta)
{
   svScope scope = svGetScopeFromName(path);
   if (scope == NULL)
      return -1;

   svScope old = svSetScope(scope);
   int r = sv_bump(delta);
   svSetScope(old);
   return r;
}

const char *dpi_where(void)
{
   const char *name = svGetNameFromScope(svGetScope());
   return name ? name : "(none)";
}

int dpi_mix(int a, int b, int *result)
{
   int old = 0, r = 0;
   sv_store(a, &old);
   sv_task(a, b, &r);
   *result = old * 1000 + r;
   return 0;
}

long long dpi_call_wide(void)
{
   return sv_wide(0x100000000LL, 7);
}

int dpi_call_logic(void)
{
   return sv_lnot(sv_0) | (sv_lnot(sv_x) << 4) | (sv_lnot(sv_1) << 8);
}

void dpi_call_lvec(void)
{
   svLogicVecVal in[2] = { { 0x55, 0x0f }, { 0x1, 0x0 } };
   svLogicVecVal out[2] = { { 0, 0 }, { 0, 0 } };
   sv_lvec(in, out);
   printf("lvec %08x/%08x %08x/%08x\n", out[0].aval, out[0].bval,
          out[1].aval, out[1].bval);
}

void dpi_call_str(void)
{
   const char *up = NULL;
   const char *r = sv_greet("world", &up);
   printf("greet %s %s\n", r, up);
}

int dpi_call_pkg(int a)
{
   return pkg_twice(a);
}

void *dpi_user(void *key, void *data)
{
   svScope scope = svGetScope();
   void *old = svGetUserData(scope, key);
   svPutUserData(scope, key, data);
   return old;
}

extern int sv_alias_c(int a);
#ifdef _WIN32
#define sv_alias_c (*(int (*)(int a))vpi_nvc_get_export("sv_alias_c"))
#endif

int dpi_call_alias(int a)
{
   return sv_alias_c(a) + 1;
}

int dpi_userdata(int value)
{
   static int key;
   int *old = svGetUserData(svGetScope(), &key);
   int result = old ? *old : -1;
   int *copy = malloc(sizeof(int));
   *copy = value;
   svPutUserData(svGetScope(), &key, copy);
   return result;
}
