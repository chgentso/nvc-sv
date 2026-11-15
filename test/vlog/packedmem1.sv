// IEEE 1800-2017 7.2.1, 7.3.1: the members of a packed structure or union
// must be integral or packed.
module packedmem1;
  class Cls; endclass
  typedef struct { bit a; } unpk_t;
  typedef struct packed { logic [3:0] a; bit b; } ok_t;
  typedef struct packed {
    real r;                                     // Error
    string s;                                   // Error
    event e;                                    // Error
    unpk_t sp;                                  // Error
    int ua[2];                                  // Error
    Cls c;                                      // Error
    ok_t okm;                                   // OK
    logic [7:0][3:0] okv;                       // OK
  } bad_t;
  typedef union packed {
    shortreal sr;                               // Error
    logic [31:0] ok;                            // OK
  } bad_u;
  typedef union { real r; logic [31:0] bits; } ok_u;   // OK
  bad_t s;
  bad_u u;
endmodule
