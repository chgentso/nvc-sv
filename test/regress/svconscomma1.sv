// Self-checking. Expected values: IEEE 1800-2017 18.5 (a constraint block
// holds constraint expressions; when the if condition holds all expressions of
// its constraint set apply together). Accepted as an extension: a comma
// after a constraint expression in a set is read as the separator.
module svconscomma1;
  class item;
    rand bit [7:0] a, b;
    bit en = 1;
    constraint k_c {
      if (en) {
        a == 8'd3,
        b == 8'd4
      };
    }
  endclass
  item it = new;
  int errs = 0;
  initial begin
    repeat (3) begin
      if (!it.randomize()) begin errs++; $error("randomize failed"); end
      else if (it.a != 3 || it.b != 4) begin errs++; $error("a=%0d b=%0d", it.a, it.b); end
    end
    if (errs == 0) $display("PASSED");
  end
endmodule
