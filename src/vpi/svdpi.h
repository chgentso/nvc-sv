/*
 * SystemVerilog DPI-C types used by foreign code called from nvc
 */

#ifndef SVDPI_H
#define SVDPI_H

#ifdef __cplusplus
extern "C" {
#endif

typedef void *svScope;
typedef void *svOpenArrayHandle;
typedef unsigned char svBit;
typedef unsigned char svLogic;
typedef unsigned int svBitVecVal;

#ifndef VPI_VECVAL
#define VPI_VECVAL
typedef struct t_vpi_vecval {
   unsigned int aval;
   unsigned int bval;
} s_vpi_vecval, *p_vpi_vecval;
#endif

typedef s_vpi_vecval svLogicVecVal;

#define sv_0 0
#define sv_1 1
#define sv_z 2
#define sv_x 3

#define SV_PACKED_DATA_NELEMS(WIDTH) (((WIDTH) + 31) >> 5)

#define SV_MASK(N) (~(-1 << (N)))

#define SV_GET_UNSIGNED_BITS(VALUE, N) \
   ((N) == 32 ? (VALUE) : ((VALUE) & SV_MASK(N)))

#define SV_GET_SIGNED_BITS(VALUE, N) \
   ((N) == 32 ? (int)(VALUE) \
    : (((VALUE) & (1 << ((N) - 1))) ? ((VALUE) | ~SV_MASK(N)) \
       : ((VALUE) & SV_MASK(N))))

svScope svGetScope(void);
svScope svSetScope(svScope scope);
const char *svGetNameFromScope(svScope scope);
svScope svGetScopeFromName(const char *name);
int svPutUserData(svScope scope, void *userKey, void *userData);
void *svGetUserData(svScope scope, void *userKey);
int svGetCallerInfo(const char **fileName, int *lineNumber);
int svIsDisabledState(void);
void svAckDisabledState(void);
const char *svDpiVersion(void);

svBit svGetBitselBit(const svBitVecVal *s, int i);
svLogic svGetBitselLogic(const svLogicVecVal *s, int i);
void svPutBitselBit(svBitVecVal *d, int i, svBit s);
void svPutBitselLogic(svLogicVecVal *d, int i, svLogic s);
void svGetPartselBit(svBitVecVal *d, const svBitVecVal *s, int i, int w);
svBitVecVal svGetBits(const svBitVecVal *s, int i, int w);
void svGetPartselLogic(svLogicVecVal *d, const svLogicVecVal *s, int i, int w);
void svPutPartselBit(svBitVecVal *d, const svBitVecVal s, int i, int w);
void svPutPartselLogic(svLogicVecVal *d, const svLogicVecVal s, int i, int w);

int svLeft(const svOpenArrayHandle h, int d);
int svRight(const svOpenArrayHandle h, int d);
int svLow(const svOpenArrayHandle h, int d);
int svHigh(const svOpenArrayHandle h, int d);
int svIncrement(const svOpenArrayHandle h, int d);
int svSize(const svOpenArrayHandle h, int d);
int svDimensions(const svOpenArrayHandle h);
void *svGetArrayPtr(const svOpenArrayHandle h);
int svSizeOfArray(const svOpenArrayHandle h);
void *svGetArrElemPtr(const svOpenArrayHandle h, int indx1, ...);
void *svGetArrElemPtr1(const svOpenArrayHandle h, int indx1);
void *svGetArrElemPtr2(const svOpenArrayHandle h, int indx1, int indx2);
void *svGetArrElemPtr3(const svOpenArrayHandle h, int indx1, int indx2,
                       int indx3);

void svPutBitArrElemVecVal(const svOpenArrayHandle d, const svBitVecVal *s,
                           int indx1, ...);
void svPutBitArrElem1VecVal(const svOpenArrayHandle d, const svBitVecVal *s,
                            int indx1);
void svPutBitArrElem2VecVal(const svOpenArrayHandle d, const svBitVecVal *s,
                            int indx1, int indx2);
void svPutBitArrElem3VecVal(const svOpenArrayHandle d, const svBitVecVal *s,
                            int indx1, int indx2, int indx3);
void svPutLogicArrElemVecVal(const svOpenArrayHandle d,
                             const svLogicVecVal *s, int indx1, ...);
void svPutLogicArrElem1VecVal(const svOpenArrayHandle d,
                              const svLogicVecVal *s, int indx1);
void svPutLogicArrElem2VecVal(const svOpenArrayHandle d,
                              const svLogicVecVal *s, int indx1, int indx2);
void svPutLogicArrElem3VecVal(const svOpenArrayHandle d,
                              const svLogicVecVal *s, int indx1, int indx2,
                              int indx3);
void svGetBitArrElemVecVal(svBitVecVal *d, const svOpenArrayHandle s,
                           int indx1, ...);
void svGetBitArrElem1VecVal(svBitVecVal *d, const svOpenArrayHandle s,
                            int indx1);
void svGetBitArrElem2VecVal(svBitVecVal *d, const svOpenArrayHandle s,
                            int indx1, int indx2);
void svGetBitArrElem3VecVal(svBitVecVal *d, const svOpenArrayHandle s,
                            int indx1, int indx2, int indx3);
void svGetLogicArrElemVecVal(svLogicVecVal *d, const svOpenArrayHandle s,
                             int indx1, ...);
void svGetLogicArrElem1VecVal(svLogicVecVal *d, const svOpenArrayHandle s,
                              int indx1);
void svGetLogicArrElem2VecVal(svLogicVecVal *d, const svOpenArrayHandle s,
                              int indx1, int indx2);
void svGetLogicArrElem3VecVal(svLogicVecVal *d, const svOpenArrayHandle s,
                              int indx1, int indx2, int indx3);

svBit svGetBitArrElem(const svOpenArrayHandle s, int indx1, ...);
svBit svGetBitArrElem1(const svOpenArrayHandle s, int indx1);
svBit svGetBitArrElem2(const svOpenArrayHandle s, int indx1, int indx2);
svBit svGetBitArrElem3(const svOpenArrayHandle s, int indx1, int indx2,
                       int indx3);
svLogic svGetLogicArrElem(const svOpenArrayHandle s, int indx1, ...);
svLogic svGetLogicArrElem1(const svOpenArrayHandle s, int indx1);
svLogic svGetLogicArrElem2(const svOpenArrayHandle s, int indx1, int indx2);
svLogic svGetLogicArrElem3(const svOpenArrayHandle s, int indx1, int indx2,
                           int indx3);
void svPutBitArrElem(const svOpenArrayHandle d, svBit value, int indx1, ...);
void svPutBitArrElem1(const svOpenArrayHandle d, svBit value, int indx1);
void svPutBitArrElem2(const svOpenArrayHandle d, svBit value, int indx1,
                      int indx2);
void svPutBitArrElem3(const svOpenArrayHandle d, svBit value, int indx1,
                      int indx2, int indx3);
void svPutLogicArrElem(const svOpenArrayHandle d, svLogic value, int indx1,
                       ...);
void svPutLogicArrElem1(const svOpenArrayHandle d, svLogic value, int indx1);
void svPutLogicArrElem2(const svOpenArrayHandle d, svLogic value, int indx1,
                        int indx2);
void svPutLogicArrElem3(const svOpenArrayHandle d, svLogic value, int indx1,
                        int indx2, int indx3);

#ifdef __cplusplus
}
#endif

#endif   // SVDPI_H
