// Test static function return variable persistence across calls and
// hierarchical access via func.func (IEEE 1800-2017 13.4.2)

module svstaticret1;
  function static string append_dots(int n);
    for (int i = 0; i < n; i++) begin
      append_dots = {append_dots, "."};
      append_dots[i] = "x";
    end
  endfunction

  function [31:0] step(input int inc);
    step = (step === 32'bx) ? inc : step + inc;
  endfunction

  wire [31:0] observed = step.step;

  initial begin
    automatic string s1 = append_dots(3);
    automatic string s2 = append_dots(4);
    if (s1 != "xxx") $fatal(1, "s1=%s exp xxx", s1);
    if (s2 != "xxxx...") $fatal(1, "s2=%s exp xxxx...", s2);

    if (step(10) !== 10) $fatal(1, "step1 mismatch");
    #1;
    if (observed !== 10) $fatal(1, "observed1=%0d exp 10", observed);
    if (step(5) !== 15) $fatal(1, "step2 mismatch");
    #1;
    if (observed !== 15) $fatal(1, "observed2=%0d exp 15", observed);

    $display("PASSED");
    $finish;
  end
endmodule
