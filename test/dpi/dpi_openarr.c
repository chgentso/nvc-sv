//
// C side of the DPI open array tests svdpioa1 .. svdpioa6 (IEEE 1800-2017
// 35.5.6, 35.5.6.1, Annex H.7, H.10, H.12).  Each function checks what it
// can on its own and counts failures in oa_fail, which the SV side reads
// with oa_errors() at the end; values are also returned to SV so that SV
// can check them against its own reference computation.
//
// Conventions relied on (Annex H.12):
//  - dimension 0 is the packed dimension of a bit/logic vector element,
//    unpacked dimensions are numbered 1, 2, ... from the left;
//  - svIncrement(h, d) is 1 when svLeft(h, d) >= svRight(h, d), else -1,
//    so walking from the left bound to the right bound steps by
//    -svIncrement(h, d);
//  - index arguments of the element access functions are the SV indices of
//    the actual argument;
//  - svGetArrayPtr gives the elements of an unpacked array in C order, the
//    leftmost SV element first (normalised [0:n-1] range, H.7.5).
//

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "svdpi.h"

static int oa_fail;

#define OA_CHECK(c) do {                                                \
      if (!(c)) {                                                       \
         printf("%s:%d: check failed: %s\n", __FILE__, __LINE__, #c);   \
         oa_fail++;                                                     \
      }                                                                 \
   } while (0)

int oa_errors(void)
{
   return oa_fail;
}

static int oa_query(const svOpenArrayHandle h, int d, int what)
{
   switch (what) {
   case 0: return svLeft(h, d);
   case 1: return svRight(h, d);
   case 2: return svLow(h, d);
   case 3: return svHigh(h, d);
   case 4: return svIncrement(h, d);
   case 5: return svSize(h, d);
   case 6: return svDimensions(h);
   default: return -9999;
   }
}

// Consistency of the query functions for dimension d
static void oa_check_dim(const svOpenArrayHandle h, int d)
{
   const int l = svLeft(h, d), r = svRight(h, d);
   OA_CHECK(svLow(h, d) == (l < r ? l : r));
   OA_CHECK(svHigh(h, d) == (l < r ? r : l));
   OA_CHECK(svIncrement(h, d) == (l >= r ? 1 : -1));
   OA_CHECK(svSize(h, d) == (l < r ? r - l : l - r) + 1);
}

// Index of the element at position p (0 = leftmost) of dimension d
static int oa_index(const svOpenArrayHandle h, int d, int p)
{
   return svLeft(h, d) - p * svIncrement(h, d);
}

////////////////////////////////////////////////////////////////////////////////
// svdpioa1: input open arrays of int, byte, longint, real

int oa_q_int(const svOpenArrayHandle h, int d, int what)
{
   return oa_query(h, d, what);
}

int oa_q_byte(const svOpenArrayHandle h, int d, int what)
{
   return oa_query(h, d, what);
}

// Weighted sum: sum over positions p of (p + 1) * element
long long oa_wsum_int(const svOpenArrayHandle h)
{
   oa_check_dim(h, 1);

   const int n = svSize(h, 1);
   const int *base = svGetArrayPtr(h);
   OA_CHECK(base != NULL);
   OA_CHECK(svSizeOfArray(h) == n * (int)sizeof(int));

   long long sum = 0;
   for (int p = 0; p < n; p++) {
      const int i = oa_index(h, 1, p);
      const int *e1 = svGetArrElemPtr1(h, i);
      const int *e = svGetArrElemPtr(h, i);
      OA_CHECK(e1 != NULL && e1 == e);
      if (e1 == NULL)
         continue;
      if (base != NULL)
         OA_CHECK(base[p] == *e1);
      sum += (long long)(p + 1) * *e1;
   }

   return sum;
}

long long oa_wsum_byte(const svOpenArrayHandle h)
{
   oa_check_dim(h, 1);

   const int n = svSize(h, 1);
   const signed char *base = svGetArrayPtr(h);
   OA_CHECK(base != NULL);
   OA_CHECK(svSizeOfArray(h) == n);

   long long sum = 0;
   for (int p = 0; p < n; p++) {
      const signed char *e = svGetArrElemPtr1(h, oa_index(h, 1, p));
      OA_CHECK(e != NULL);
      if (e == NULL)
         continue;
      if (base != NULL)
         OA_CHECK(base[p] == *e);
      sum += (long long)(p + 1) * *e;
   }

   return sum;
}

long long oa_wsum_long(const svOpenArrayHandle h)
{
   oa_check_dim(h, 1);

   const int n = svSize(h, 1);
   OA_CHECK(svSizeOfArray(h) == n * (int)sizeof(long long));

   long long sum = 0;
   for (int p = 0; p < n; p++) {
      const long long *e = svGetArrElemPtr1(h, oa_index(h, 1, p));
      OA_CHECK(e != NULL);
      if (e != NULL)
         sum += (long long)(p + 1) * *e;
   }

   return sum;
}

double oa_wsum_real(const svOpenArrayHandle h)
{
   oa_check_dim(h, 1);

   const int n = svSize(h, 1);
   OA_CHECK(svSizeOfArray(h) == n * (int)sizeof(double));

   double sum = 0;
   for (int p = 0; p < n; p++) {
      const double *e = svGetArrElemPtr1(h, oa_index(h, 1, p));
      OA_CHECK(e != NULL);
      if (e != NULL)
         sum += (p + 1) * *e;
   }

   return sum;
}

////////////////////////////////////////////////////////////////////////////////
// svdpioa2: input open arrays with packed bit/logic elements

int oa_q_b8(const svOpenArrayHandle h, int d, int what)
{
   return oa_query(h, d, what);
}

int oa_q_l12(const svOpenArrayHandle h, int d, int what)
{
   return oa_query(h, d, what);
}

int oa_q_bit(const svOpenArrayHandle h, int d, int what)
{
   return oa_query(h, d, what);
}

long long oa_wsum_b8(const svOpenArrayHandle h)
{
   OA_CHECK(svDimensions(h) == 2);
   oa_check_dim(h, 0);
   oa_check_dim(h, 1);
   OA_CHECK(svSize(h, 0) == 8);

   long long sum = 0;
   for (int p = 0; p < svSize(h, 1); p++) {
      const int i = oa_index(h, 1, p);
      svBitVecVal v1 = 0xdeadbe00, v2 = 0xdeadbe00;
      svGetBitArrElem1VecVal(&v1, h, i);
      svGetBitArrElemVecVal(&v2, h, i);
      OA_CHECK((v1 & 0xff) == (v2 & 0xff));
      sum += (long long)(p + 1) * (v1 & 0xff);
   }

   return sum;
}

// Each element contributes (p + 1) * (aval + (bval << 16)) of its 12 bits
long long oa_wsum_l12(const svOpenArrayHandle h)
{
   OA_CHECK(svDimensions(h) == 2);
   oa_check_dim(h, 1);
   OA_CHECK(svSize(h, 0) == 12);

   long long sum = 0;
   for (int p = 0; p < svSize(h, 1); p++) {
      const int i = oa_index(h, 1, p);
      svLogicVecVal v1, v2;
      svGetLogicArrElem1VecVal(&v1, h, i);
      svGetLogicArrElemVecVal(&v2, h, i);
      OA_CHECK((v1.aval & 0xfff) == (v2.aval & 0xfff));
      OA_CHECK((v1.bval & 0xfff) == (v2.bval & 0xfff));
      sum += (long long)(p + 1)
         * ((v1.aval & 0xfff) + ((long long)(v1.bval & 0xfff) << 16));
   }

   return sum;
}

// 40 bit logic elements: two words each, the upper word has 8 valid bits;
// element p contributes (p + 1) * (a0 + 3*a1 + 5*b0 + 7*b1)
long long oa_wsum_l40(const svOpenArrayHandle h)
{
   OA_CHECK(svDimensions(h) == 2);
   OA_CHECK(svSize(h, 0) == 40);

   long long sum = 0;
   for (int p = 0; p < svSize(h, 1); p++) {
      svLogicVecVal v[SV_PACKED_DATA_NELEMS(40)];
      svGetLogicArrElem1VecVal(v, h, oa_index(h, 1, p));
      sum += (long long)(p + 1)
         * ((long long)v[0].aval + 3LL * (v[1].aval & 0xff)
            + 5LL * v[0].bval + 7LL * (v[1].bval & 0xff));
   }

   return sum;
}

// 70 bit elements: element p contributes (p + 1) * (w0 + 3*w1 + 5*w2)
long long oa_wsum_b70(const svOpenArrayHandle h)
{
   OA_CHECK(svDimensions(h) == 2);
   OA_CHECK(svLeft(h, 0) == 69 && svRight(h, 0) == 0);

   long long sum = 0;
   for (int p = 0; p < svSize(h, 1); p++) {
      svBitVecVal v[SV_PACKED_DATA_NELEMS(70)];
      svGetBitArrElemVecVal(v, h, oa_index(h, 1, p));
      sum += (long long)(p + 1)
         * ((long long)v[0] + 3LL * v[1] + 5LL * (v[2] & 0x3f));
   }

   return sum;
}

// Scalar bit elements: bit p of the result is the element at position p
int oa_bits(const svOpenArrayHandle h)
{
   OA_CHECK(svDimensions(h) == 1);
   oa_check_dim(h, 1);

   int mask = 0;
   for (int p = 0; p < svSize(h, 1); p++) {
      const int i = oa_index(h, 1, p);
      const svBit b = svGetBitArrElem1(h, i);
      OA_CHECK(b == svGetBitArrElem(h, i));
      OA_CHECK(b == 0 || b == 1);
      mask |= (b & 1) << p;
   }

   return mask;
}

// Scalar logic elements: bits 2p+1:2p of the result hold the svLogic
// encoding (sv_0, sv_1, sv_z, sv_x) of the element at position p
int oa_logics(const svOpenArrayHandle h)
{
   OA_CHECK(svDimensions(h) == 1);

   int mask = 0;
   for (int p = 0; p < svSize(h, 1); p++) {
      const int i = oa_index(h, 1, p);
      const svLogic l = svGetLogicArrElem1(h, i);
      OA_CHECK(l == svGetLogicArrElem(h, i));
      OA_CHECK(l <= sv_x);
      mask |= (l & 3) << (2 * p);
   }

   return mask;
}

////////////////////////////////////////////////////////////////////////////////
// svdpioa3: multi-dimensional unpacked open arrays

int oa_q_int2(const svOpenArrayHandle h, int d, int what)
{
   return oa_query(h, d, what);
}

int oa_q_b8x3(const svOpenArrayHandle h, int d, int what)
{
   return oa_query(h, d, what);
}

// Row-major position p = p1 * n2 + p2 counted from the left bounds;
// result is sum of (p + 1) * element
long long oa_wsum_int2(const svOpenArrayHandle h)
{
   oa_check_dim(h, 1);
   oa_check_dim(h, 2);

   const int n1 = svSize(h, 1), n2 = svSize(h, 2);
   const int *base = svGetArrayPtr(h);
   OA_CHECK(base != NULL);
   OA_CHECK(svSizeOfArray(h) == n1 * n2 * (int)sizeof(int));

   long long sum = 0;
   for (int p1 = 0; p1 < n1; p1++) {
      for (int p2 = 0; p2 < n2; p2++) {
         const int i = oa_index(h, 1, p1), j = oa_index(h, 2, p2);
         const int *e = svGetArrElemPtr2(h, i, j);
         OA_CHECK(e != NULL && e == svGetArrElemPtr(h, i, j));
         if (e == NULL)
            continue;
         const int p = p1 * n2 + p2;
         if (base != NULL)
            OA_CHECK(base[p] == *e);
         sum += (long long)(p + 1) * *e;
      }
   }

   return sum;
}

long long oa_wsum_b8x3(const svOpenArrayHandle h)
{
   OA_CHECK(svDimensions(h) == 4);
   OA_CHECK(svSize(h, 0) == 8);
   for (int d = 1; d <= 3; d++)
      oa_check_dim(h, d);

   const int n1 = svSize(h, 1), n2 = svSize(h, 2), n3 = svSize(h, 3);

   long long sum = 0;
   for (int p1 = 0; p1 < n1; p1++) {
      for (int p2 = 0; p2 < n2; p2++) {
         for (int p3 = 0; p3 < n3; p3++) {
            const int i = oa_index(h, 1, p1);
            const int j = oa_index(h, 2, p2);
            const int k = oa_index(h, 3, p3);
            svBitVecVal v3 = 0, v = 0;
            svGetBitArrElem3VecVal(&v3, h, i, j, k);
            svGetBitArrElemVecVal(&v, h, i, j, k);
            OA_CHECK((v3 & 0xff) == (v & 0xff));
            const int p = (p1 * n2 + p2) * n3 + p3;
            sum += (long long)(p + 1) * (v3 & 0xff);
         }
      }
   }

   return sum;
}

// Two-dimensional array of scalar logic: bits 2p+1:2p hold element p
int oa_logics2(const svOpenArrayHandle h)
{
   OA_CHECK(svDimensions(h) == 2);

   const int n2 = svSize(h, 2);
   int mask = 0;
   for (int p1 = 0; p1 < svSize(h, 1); p1++) {
      for (int p2 = 0; p2 < n2; p2++) {
         const int i = oa_index(h, 1, p1), j = oa_index(h, 2, p2);
         const svLogic l = svGetLogicArrElem2(h, i, j);
         OA_CHECK(l == svGetLogicArrElem(h, i, j));
         mask |= (l & 3) << (2 * (p1 * n2 + p2));
      }
   }

   return mask;
}

////////////////////////////////////////////////////////////////////////////////
// svdpioa4: output and inout open arrays

// Element at position p becomes base + p * p
void oa_fill_int(const svOpenArrayHandle h, int base)
{
   oa_check_dim(h, 1);
   for (int p = 0; p < svSize(h, 1); p++) {
      int *e = svGetArrElemPtr1(h, oa_index(h, 1, p));
      OA_CHECK(e != NULL);
      if (e != NULL)
         *e = base + p * p;
   }
}

// Element at position p becomes 2 * old + p; returns the number of elements
int oa_scale_int(const svOpenArrayHandle h)
{
   for (int p = 0; p < svSize(h, 1); p++) {
      int *e = svGetArrElemPtr1(h, oa_index(h, 1, p));
      OA_CHECK(e != NULL);
      if (e != NULL)
         *e = 2 * *e + p;
   }
   return svSize(h, 1);
}

// Element at position p becomes 8'ha0 + p
void oa_fill_b8(const svOpenArrayHandle h)
{
   OA_CHECK(svSize(h, 0) == 8);
   for (int p = 0; p < svSize(h, 1); p++) {
      const svBitVecVal v = 0xa0 + p;
      svPutBitArrElem1VecVal(h, &v, oa_index(h, 1, p));
   }
}

// The value (aval) word of every 4 bit element is inverted, so 0 and 1
// swap and x and z swap; the bval word is kept
void oa_inv_l4(const svOpenArrayHandle h)
{
   OA_CHECK(svSize(h, 0) == 4);
   for (int p = 0; p < svSize(h, 1); p++) {
      const int i = oa_index(h, 1, p);
      svLogicVecVal v;
      svGetLogicArrElem1VecVal(&v, h, i);
      v.aval = ~v.aval & 0xf;
      v.bval &= 0xf;
      if (p & 1)
         svPutLogicArrElemVecVal(h, &v, i);
      else
         svPutLogicArrElem1VecVal(h, &v, i);
   }
}

// Scalar bit outputs: element at position p becomes p & 1
void oa_fill_bit(const svOpenArrayHandle h)
{
   for (int p = 0; p < svSize(h, 1); p++) {
      if (p & 1)
         svPutBitArrElem(h, p & 1, oa_index(h, 1, p));
      else
         svPutBitArrElem1(h, p & 1, oa_index(h, 1, p));
   }
}

// Scalar logic outputs: element at position p becomes svLogic value p % 4,
// i.e. 0, 1, z, x, 0, ...
void oa_fill_logic(const svOpenArrayHandle h)
{
   for (int p = 0; p < svSize(h, 1); p++)
      svPutLogicArrElem1(h, p % 4, oa_index(h, 1, p));
}

// Two-dimensional output: element at positions (p1, p2) becomes
// 10 * p1 + p2
void oa_fill_int2(const svOpenArrayHandle h)
{
   for (int p1 = 0; p1 < svSize(h, 1); p1++) {
      for (int p2 = 0; p2 < svSize(h, 2); p2++) {
         int *e = svGetArrElemPtr2(h, oa_index(h, 1, p1), oa_index(h, 2, p2));
         OA_CHECK(e != NULL);
         if (e != NULL)
            *e = 10 * p1 + p2;
      }
   }
}

// 70 bit inout elements: the three words are rotated (w0 <- w1 <- w2' <-
// w0 truncated to 6 bits), done through the variadic access functions
void oa_rot_b70(const svOpenArrayHandle h)
{
   for (int p = 0; p < svSize(h, 1); p++) {
      const int i = oa_index(h, 1, p);
      svBitVecVal v[3], r[3];
      svGetBitArrElemVecVal(v, h, i);
      r[0] = v[1];
      r[1] = v[2] & 0x3f;
      r[2] = v[0] & 0x3f;
      svPutBitArrElemVecVal(h, r, i);
   }
}

// Imported task: element at position p becomes n - p
int oa_task_fill(const svOpenArrayHandle h, int n)
{
   for (int p = 0; p < svSize(h, 1); p++) {
      int *e = svGetArrElemPtr1(h, oa_index(h, 1, p));
      OA_CHECK(e != NULL);
      if (e != NULL)
         *e = n - p;
   }
   return 0;
}

////////////////////////////////////////////////////////////////////////////////
// svdpioa5: open packed dimension

int oa_q_pk(const svOpenArrayHandle h, int d, int what)
{
   return oa_query(h, d, what);
}

int oa_q_lpk(const svOpenArrayHandle h, int d, int what)
{
   return oa_query(h, d, what);
}

int oa_q_pku(const svOpenArrayHandle h, int d, int what)
{
   return oa_query(h, d, what);
}

// Number of one bits of a bit vector of any width
int oa_pk_pop(const svOpenArrayHandle h)
{
   OA_CHECK(svDimensions(h) == 1);
   oa_check_dim(h, 0);

   const int width = svSize(h, 0);
   const svBitVecVal *v = svGetArrayPtr(h);
   OA_CHECK(v != NULL);
   if (v == NULL)
      return -1;

   int count = 0;
   for (int w = 0; w < SV_PACKED_DATA_NELEMS(width); w++) {
      svBitVecVal word = v[w];
      if (w == SV_PACKED_DATA_NELEMS(width) - 1 && width % 32 != 0)
         word &= (1u << (width % 32)) - 1;
      count += __builtin_popcount(word);
   }

   return count;
}

// Logic vector of any width: returns 1000 * (number of x/z bits) + number
// of 1 bits
int oa_lpk_count(const svOpenArrayHandle h)
{
   OA_CHECK(svDimensions(h) == 1);

   const int width = svSize(h, 0);
   const svLogicVecVal *v = svGetArrayPtr(h);
   OA_CHECK(v != NULL);
   if (v == NULL)
      return -1;

   int ones = 0, unknown = 0;
   for (int w = 0; w < SV_PACKED_DATA_NELEMS(width); w++) {
      svBitVecVal mask = ~0u;
      if (w == SV_PACKED_DATA_NELEMS(width) - 1 && width % 32 != 0)
         mask = (1u << (width % 32)) - 1;
      ones += __builtin_popcount(v[w].aval & ~v[w].bval & mask);
      unknown += __builtin_popcount(v[w].bval & mask);
   }

   return 1000 * unknown + ones;
}

// Output bit vector of any width: bit k becomes k % 3 == 0
void oa_pk_fill(const svOpenArrayHandle h)
{
   const int width = svSize(h, 0);
   svBitVecVal *v = svGetArrayPtr(h);
   OA_CHECK(v != NULL);
   if (v == NULL)
      return;

   for (int w = 0; w < SV_PACKED_DATA_NELEMS(width); w++)
      v[w] = 0;
   for (int k = 0; k < width; k++) {
      if (k % 3 == 0)
         v[k / 32] |= 1u << (k % 32);
   }
}

// Inout logic vector of any width: every bit is inverted (x and z swap)
void oa_lpk_inv(const svOpenArrayHandle h)
{
   const int width = svSize(h, 0);
   svLogicVecVal *v = svGetArrayPtr(h);
   OA_CHECK(v != NULL);
   if (v == NULL)
      return;

   for (int w = 0; w < SV_PACKED_DATA_NELEMS(width); w++) {
      svBitVecVal mask = ~0u;
      if (w == SV_PACKED_DATA_NELEMS(width) - 1 && width % 32 != 0)
         mask = (1u << (width % 32)) - 1;
      v[w].aval = ~v[w].aval & mask;
      v[w].bval &= mask;
   }
}

// Open packed and open unpacked dimension: element p contributes
// (p + 1) * value (elements up to 32 bits wide)
long long oa_pku_wsum(const svOpenArrayHandle h)
{
   OA_CHECK(svDimensions(h) == 2);
   const int width = svSize(h, 0);
   OA_CHECK(width <= 32);

   long long sum = 0;
   for (int p = 0; p < svSize(h, 1); p++) {
      svBitVecVal v = 0;
      svGetBitArrElem1VecVal(&v, h, oa_index(h, 1, p));
      if (width < 32)
         v &= (1u << width) - 1;
      sum += (long long)(p + 1) * v;
   }

   return sum;
}

////////////////////////////////////////////////////////////////////////////////
// svdpioa6: sized (not open) unpacked array formals, passed as C arrays
// (H.7.5): element 0 of the C array is the leftmost SV element

long long fx_wsum4(const int *x)
{
   long long sum = 0;
   for (int p = 0; p < 4; p++)
      sum += (long long)(p + 1) * x[p];
   return sum;
}

long long fx_wsum23(const int *m)
{
   long long sum = 0;
   for (int p = 0; p < 6; p++)
      sum += (long long)(p + 1) * m[p];
   return sum;
}

long long fx_bytes(const signed char *b)
{
   long long sum = 0;
   for (int p = 0; p < 4; p++)
      sum += (long long)(p + 1) * b[p];
   return sum;
}

// Element p of the output becomes 8'h30 + p
void fx_out_b8(svBitVecVal *y)
{
   for (int p = 0; p < 3; p++)
      y[p] = 0x30 + p;
}

// Two 4 bit logic elements, value words inverted
void fx_inv_l4(svLogicVecVal *z)
{
   for (int p = 0; p < 2; p++) {
      z[p].aval = ~z[p].aval & 0xf;
      z[p].bval &= 0xf;
   }
}

// Two 40 bit elements of two words each: returns the popcount of all bits
// and writes element p's word sum into sums[p] (output int sums[2])
int fx_pop40(const svBitVecVal *w, int *sums)
{
   int count = 0;
   for (int p = 0; p < 2; p++) {
      const svBitVecVal lo = w[2 * p], hi = w[2 * p + 1] & 0xff;
      count += __builtin_popcount(lo) + __builtin_popcount(hi);
      sums[p] = (int)(lo + hi);
   }
   return count;
}
