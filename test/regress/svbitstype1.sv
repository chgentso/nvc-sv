// $bits applied to types, type() references, variables, selects and expressions (self-checking).
// Expected values: IEEE 1800-2017 20.6.2 ($bits), 6.23 (type operator), 7.4.5/7.4.1 (array element counts
// multiply), 7.2 (struct width: sum of members, unpacked too), 11.6.1 (expression width: max of the operand
// widths, an unsized literal being 32 bits), 5.9 (a string literal has 8 bits per character).
// Reference model, in the order of the checks: arr_t 3*8=24; ar 24; type(ar) 24; vec_t 2*2*4=16; vv 16;
// type(vv) 16; s_t 32+2*8=48; s 48; p_t 8+4=12; type(p) 12; arr2 4*5*32=640; arr2[1] 160; arr2[1][2] 32;
// ar[1] 8; byte_t 8; type(bt) 8; bt+1 32; s.b 16; p.b 4; int 32; longint 64; logic[5:2] 4; "abcd" 32;
// $bits(bt)*2 16; {bt,bt} 16.
module svbitstype1;
  typedef bit [7:0] byte_t;
  typedef byte_t arr_t[3];
  typedef logic [3:0] vec_t [2][2];
  typedef struct { int a; byte_t b[2]; } s_t;
  typedef struct packed { byte_t a; bit [3:0] b; } p_t;
  arr_t ar; vec_t vv; s_t s; p_t p; int q[$]; int d[]; string str = "abc";
  byte_t bt; int arr2 [4][5];
  int got [0:24];
  int expv [0:24] = '{24, 24, 24, 16, 16, 16, 48, 48, 12, 12, 640, 160, 32, 8, 8, 8, 32, 16, 4, 32, 64, 4, 32, 16, 16};
  int errs = 0;
  initial begin
    got[0]  = $bits(arr_t);
    got[1]  = $bits(ar);
    got[2]  = $bits(type(ar));
    got[3]  = $bits(vec_t);
    got[4]  = $bits(vv);
    got[5]  = $bits(type(vv));
    got[6]  = $bits(s_t);
    got[7]  = $bits(s);
    got[8]  = $bits(p_t);
    got[9]  = $bits(type(p));
    got[10] = $bits(arr2);
    got[11] = $bits(arr2[1]);
    got[12] = $bits(arr2[1][2]);
    got[13] = $bits(ar[1]);
    got[14] = $bits(byte_t);
    got[15] = $bits(type(bt));
    got[16] = $bits(bt + 1);
    got[17] = $bits(s.b);
    got[18] = $bits(p.b);
    got[19] = $bits(int);
    got[20] = $bits(longint);
    got[21] = $bits(logic [5:2]);
    got[22] = $bits("abcd");
    got[23] = $bits(bt) * 2;
    got[24] = $bits({bt, bt});
    for (int i = 0; i <= 24; i++)
      if (got[i] != expv[i]) begin errs++; $error("check %0d: $bits = %0d, expected %0d", i, got[i], expv[i]); end
    if (errs == 0) $display("PASSED");
  end
endmodule
