// IEEE 1800-2017 6.7.1: the data type of a net must be 4-state integral or
// an array, union or structure of such.
module netdtype1;
  class Cls; endclass
  typedef real real_t;
  typedef struct packed { bit m_bit; } bad_t;
  typedef struct { logic m_bit; } ok_unpk_t;
  typedef struct packed { logic m_bit; } ok_t;
  typedef enum logic [1:0] { A, B } ok_enum_t;
  wire real_t bad_real;                         // Error
  wire Cls bad_class;                           // Error
  wire string bad_string;                       // Error
  wire bit bad_bit;                             // Error
  wire bit [7:0] bad_vec;                       // Error
  wire int bad_int;                             // Error
  wire bad_t bad_struct;                        // Error
  wire ok_unpk_t ok_unpk;                       // OK
  wire ok_t ok_struct;                          // OK
  wire ok_enum_t ok_enum;                       // OK
  wire logic [7:0] ok_logic;                    // OK
  wire integer ok_integer;                      // OK
  wire ok_wire;                                 // OK
  tri [3:0] ok_tri;                             // OK
endmodule
