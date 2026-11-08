// Expected values: IEEE 1800-2017 13.4 (function return value converted to the return type), 6.12 (real, realtime),
// 6.19 (enum), 12.5 (case), 11.8 (integer converted to real by assignment/return)
module svretrealint1;
  typedef enum int {A=1, B=2, C=3} e_t;
  int err = 0;
  function automatic real f(e_t e);
    case (e)
      A: return 104166.667;
      B: return 1000;
      default: return 7;
    endcase
  endfunction
  function automatic realtime g(int x);
    return x;
  endfunction
  initial begin
    if (f(A) != 104166.667) begin $error("f(A)"); err++; end
    if (f(B) != 1000.0) begin $error("f(B)"); err++; end
    if (f(C) != 7.0) begin $error("f(C)"); err++; end
    if (g(5) != 5.0) begin $error("g(5)"); err++; end
    if (err == 0) $display("PASSED");
  end
endmodule
