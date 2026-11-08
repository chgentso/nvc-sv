// Expected values: IEEE 1800-2017 13.4 (function argument and return types), 7.4 (packed arrays of enum),
// 6.19 (enum values A=0, B=1, C=2)
module svtfporttype1;
  typedef enum logic [1:0] {A, B, C} e_t;
  localparam int N = 3;
  function automatic e_t [N-1:0] rev(e_t [N-1:0] in);
    e_t [N-1:0] out;
    for (int i = 0; i < N; i++) out[i] = in[N-1-i];
    return out;
  endfunction
  e_t [N-1:0] x = {A, B, C}, y;
  int errs;
  initial begin
    y = rev(x);
    // x[2]=A x[1]=B x[0]=C; reversed: y[0]=A y[1]=B y[2]=C
    if (y[0] !== A || y[1] !== B || y[2] !== C) begin $error("rev %0d %0d %0d", y[0], y[1], y[2]); errs++; end
    if (y !== {C, B, A}) begin $error("rev vector"); errs++; end
    if (errs == 0) $display("PASSED");
    $finish;
  end
endmodule
