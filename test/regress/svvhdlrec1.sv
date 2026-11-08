// Expected values: IEEE 1800-2017 10.9.2 (structure assignment patterns), 6.7 (nets), 9.4.1; IEEE 1076 for the VHDL
// body (o.cyc <= i.ack, o.adr <= i.dat(3 downto 0)). The mapping of VHDL record ports is implementation-defined
// (cross-language, not checked beyond the values below).
// First instance: ack=1, dat=5a -> cyc=1, adr=a. Second instance: ack=0, dat=c3 -> cyc=0, adr=3.
import svvhdlrec1_pkg::*;

module svvhdlrec1;
   int errs = 0;
   wire t_wb_out dummy = '{ack: 1'b1, err: 1'b0, dat: 8'h5a};
   wire t_wb_in  res;
   svvhdlrec1_dut u(.i(dummy), .o(res));
   wire t_wb_out dummy2 = '{ack: 1'b0, err: 1'b1, dat: 8'hc3};
   wire t_wb_in  res2;
   svvhdlrec1_dut u2(.i(dummy2), .o(res2));
   initial begin
     #1;
     if (res2.cyc !== 1'b0 || res2.adr !== 4'h3) begin errs++; $error("second: cyc=%b adr=%h, expected 0 3", res2.cyc, res2.adr); end
     if (res.cyc !== 1'b1 || res.adr !== 4'ha) begin errs++; $error("first: cyc=%b adr=%h, expected 1 a", res.cyc, res.adr); end
     if (errs == 0) $display("PASSED");
   end
endmodule
