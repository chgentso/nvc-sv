`timescale 1ns/1ns
// Verilog module instantiated from VHDL with its parameters overridden by
// the VHDL component's generic map (string, integer and 32-bit vector values).
// Expected values: IEEE 1800-2017 6.20.2 (a parameter value override replaces
// the default), 21.2.1 (%s, %h formatting is not needed: the values are compared
// directly), 11.4.5 (equality).  The generic map in mixed45.vhd gives
// BTP_N => "6.0", BTP_O => "2.0", CDR_N => 7, EBA_RESET => x"00001000"; the
// mapping of VHDL generics onto Verilog parameters follows the design of the
// mixed-language support (VHDL string -> string parameter, integer -> integer
// parameter, std_logic_vector -> vector parameter).
module mixed45_v #(parameter BTP_N = "5.0", parameter BTP_O = "1.0", parameter CDR_N = 5, parameter [31:0] EBA_RESET = 32'h0)
  (input clk, output reg [7:0] q);
  integer errors = 0;
  initial begin
    #1;
    if (BTP_N != "6.0") begin $display("FAILED: BTP_N = %s, expected 6.0", BTP_N); errors = errors + 1; end
    if (BTP_O != "2.0") begin $display("FAILED: BTP_O = %s, expected 2.0", BTP_O); errors = errors + 1; end
    if (CDR_N !== 7) begin $display("FAILED: CDR_N = %0d, expected 7", CDR_N); errors = errors + 1; end
    if (EBA_RESET !== 32'h00001000) begin $display("FAILED: EBA_RESET = %h, expected 00001000", EBA_RESET); errors = errors + 1; end
    q = 8'h5a;
    if (errors == 0) $display("PASSED");
  end
endmodule
