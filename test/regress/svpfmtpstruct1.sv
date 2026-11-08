// %p of packed structures and packed unions.
// IEEE 1800-2017 7.2.1: a packed structure is an integral value and can be
// used as a whole like a vector of the same width and signing (7.3.1: so can
// a packed union); 21.2.1.7: %p prints an integral value as such, in decimal.
// A packed structure or union is therefore printed as one number, the same as
// a vector with the same bits and signing, and the same as a packed array
// (see svpfmtpacked1), also as an element of an unpacked array, as a member
// of an unpacked structure and when it contains a nested packed structure.
module svpfmtpstruct1;
  typedef struct packed { bit [3:0] a; bit [3:0] b; } s_t;
  typedef struct packed signed { bit [3:0] a; bit [3:0] b; } ss_t;
  typedef struct packed { logic [3:0] a; logic [3:0] b; } l_t;
  typedef struct packed { s_t hi; bit [1:0][3:0] lo; } n_t;
  typedef union packed { bit [7:0] w; s_t s; } u_t;

  s_t s = 8'ha5;
  ss_t ss = 8'hf0;
  l_t l = 8'b1x00_0011;
  n_t n = 16'h1234;
  u_t u = 8'h12;
  s_t arr[2] = '{8'h12, 8'h34};
  struct { s_t m; int i; } st = '{8'h21, 7};

  bit [7:0] v1 = 8'ha5;
  logic [7:0] vx = 8'b1x00_0011;
  bit signed [7:0] vs = 8'hf0;
  int errors = 0;

  task check(string got, string want);
    if (got != want) begin
      $display("got '%s', expected '%s'", got, want);
      errors++;
    end
  endtask

  initial begin
    check($sformatf("%p", s), "165");
    check($sformatf("%p", s), $sformatf("%p", v1));
    check($sformatf("%0p", s), "165");
    check($sformatf("%p", ss), "-16");
    check($sformatf("%p", ss), $sformatf("%p", vs));
    check($sformatf("%p", l), $sformatf("%p", vx));
    check($sformatf("%p", n), "4660");
    check($sformatf("%p", u), "18");
    check($sformatf("%p", arr), "'{18, 52}");
    check($sformatf("%p", st), "'{m:33, i:7}");
    check($sformatf("%p", st.m), "33");
    if (errors == 0) $display("PASSED");
    else $error("%0d mismatches", errors);
  end
endmodule
