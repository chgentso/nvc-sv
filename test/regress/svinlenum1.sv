// Expected values: IEEE 1800-2017 18.7 (randomize() with inline constraints), 18.3 (rand enum variable),
// 6.19.5.6 (name()). The only value of kind_e satisfying exception != kA && exception != kC is kB.
// (The member is named like the VHDL-style word "exception"; it is an ordinary identifier in SystemVerilog.)
class illegal;
  typedef enum {kA, kB, kC} kind_e;
  rand kind_e exception;
endclass
module svinlenum1;
  illegal ill;
  int err = 0;
  initial begin
    ill = new;
    for (int i = 0; i < 20; i++) begin
      if (!ill.randomize() with { exception != kA; exception != kC; }) begin err++; $error("randomize failed"); end
      else if (ill.exception != illegal::kB || ill.exception.name() != "kB") begin
        err++; $error("exception=%s expected kB", ill.exception.name());
      end
    end
    if (err == 0) $display("PASSED");
  end
endmodule
