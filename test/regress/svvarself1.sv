// Expected values: IEEE 1800-2017 6.19.5.2 (first), 6.19.5.6 (name), 6.8 (declaration initialiser)
typedef enum {X0, X1, X2} en_t;
function automatic string first_name();
  en_t e = e.first;
  return e.name();
endfunction
module svvarself1;
  initial begin
    automatic string n = first_name();
    if (n != "X0") $error("first_name() = %s, expected X0", n);
    else $display("PASSED");
  end
endmodule
