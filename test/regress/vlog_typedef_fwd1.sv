package vlog_typedef_fwd1_pkg;
  typedef enum enum_t;
  typedef struct struct_t;
  typedef union union_t;
  typedef class ClsB;

  class ClsA;
    enum_t m_e;
    struct_t m_s;
    union_t m_u;
    ClsB m_b;
  endclass

  typedef enum { E_ZERO = 0, E_ONE = 1 } enum_t;
  typedef struct packed { int s; } struct_t;
  typedef union packed { int u; } union_t;

  class ClsB;
    int x = 42;
  endclass
endpackage

module vlog_typedef_fwd1;
  import vlog_typedef_fwd1_pkg::*;

  initial begin
    ClsA a = new();
    a.m_e = E_ONE;
    a.m_s.s = 123;
    a.m_u.u = 456;
    a.m_b = new();
    if (a.m_e != E_ONE || a.m_s.s != 123 || a.m_u.u != 456 || a.m_b.x != 42)
      $fatal(1, "forward typedef field mismatch");
    $display("PASSED");
    $finish;
  end
endmodule
