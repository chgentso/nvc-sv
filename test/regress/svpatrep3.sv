module svpatrep3;
  typedef struct packed {
    int a;
    bit [3:0][31:0] b4;
  } sp_t;

  typedef struct {
    int a;
    int b4[4];
  } su_t;

  sp_t ap[2][3];
  su_t au[2][3];

  initial begin
    ap = '{2{'{3{'{10, '{2{20, 30}}}}}}};
    au = '{2{'{3{'{10, '{2{20, 30}}}}}}};

    if (ap[0][0].a !== 10 || ap[0][0].b4[0] !== 30 || ap[0][0].b4[1] !== 20)
      $fatal(1, "ap[0][0] mismatch");
    if (ap[1][2].a !== 10 || ap[1][2].b4[2] !== 30 || ap[1][2].b4[3] !== 20)
      $fatal(1, "ap[1][2] mismatch");
    if (au[0][0].a !== 10 || au[0][0].b4[0] !== 20 || au[0][0].b4[1] !== 30)
      $fatal(1, "au[0][0] mismatch");
    if (au[1][2].a !== 10 || au[1][2].b4[2] !== 20 || au[1][2].b4[3] !== 30)
      $fatal(1, "au[1][2] mismatch");

    $display("PASSED");
    $finish;
  end
endmodule
