// A virtual interface of an interface with an inout port and a string-valued parameter, used
// through a package typedef.
// Expected values: IEEE 1800-2017 25.9, 6.6 (an undriven net reads z), 13.5.3 (default argument
// values), 6.16 (string literal as a bit vector parameter).
interface pins_if #(parameter int Width = 1, parameter bit [8*4-1:0] PullStrength = "Pull") (inout [Width-1:0] pins);
  logic [Width-1:0] pins_o;
  function automatic void drive_pin(int idx = 0, logic val);
    pins_o[idx] = val;
  endfunction
endinterface
package pkg;
  parameter int NUM = 8;
  typedef virtual pins_if #(NUM) intr_vif;
endpackage
class cfg_c;
  pkg::intr_vif vif;
endclass
module svvifstrparam1;
  wire [7:0] w;
  pins_if #(8) intr_if(w);
  int errors = 0;
  initial begin
    automatic cfg_c cfg = new;
    cfg.vif = intr_if;
    cfg.vif.drive_pin(3, 1'b1);
    #1;
    if (cfg.vif.pins[3] !== 1'bz) begin $error("pins[3]=%b exp z", cfg.vif.pins[3]); errors++; end
    if (cfg.vif.pins_o[3] !== 1'b1) begin $error("pins_o[3]=%b exp 1", cfg.vif.pins_o[3]); errors++; end
    if (cfg.vif.pins !== 8'bzzzzzzzz) begin $error("pins=%b exp zzzzzzzz", cfg.vif.pins); errors++; end
    if (cfg.vif.PullStrength !== "Pull") begin $error("PullStrength=%h", cfg.vif.PullStrength); errors++; end
    if (errors == 0) $display("PASSED");
  end
endmodule
