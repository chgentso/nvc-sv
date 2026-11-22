module svcondstruct1;
  typedef struct {
    logic en;
    int   val;
  } drv_t;

  function automatic drv_t mk_a();
    drv_t r;
    r.en = 1'b1;
    r.val = 11;
    return r;
  endfunction

  function automatic drv_t mk_b();
    drv_t r;
    r.en = 1'b0;
    r.val = 22;
    return r;
  endfunction

  logic sel;
  drv_t d;

  always_comb begin
    d = sel ? mk_a() : mk_b();
  end

  initial begin
    sel = 1'b1;
    #1;
    if (d.en !== 1'b1 || d.val !== 11)
      $fatal(1, "sel=1 got en=%b val=%0d", d.en, d.val);
    sel = 1'b0;
    #1;
    if (d.en !== 1'b0 || d.val !== 22)
      $fatal(1, "sel=0 got en=%b val=%0d", d.en, d.val);
    $display("PASSED");
    $finish;
  end
endmodule
