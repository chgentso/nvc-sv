// Expected values: IEEE 1800-2017 8.10 / 13.4.2 (class methods are automatic: `which` is re-initialised on each
// call), 11.4.2 (pre-decrement, index evaluated once), 11.4.11 (conditional operator), 25.9 (read through a virtual
// interface), 6.9.1 (net initialiser)
package agp;
  class agcfg;
    virtual ifc vif;
    bit is_dev;
    bit lastbit;
    int lastwhich;
    task t();
      bit data[8];
      int which = 8;
      data[--which] = is_dev ? vif.sio[1] : vif.sio[0];
      lastbit = data[7];
      lastwhich = which;
    endtask
  endclass
endpackage
interface ifc;
  wire [3:0] sio = 4'b0010;
endinterface
module svvifcondsel1;
  ifc i();
  int errs = 0;
  initial begin
    automatic agp::agcfg c = new;
    #1;
    c.vif = i;
    c.is_dev = 1;
    c.t();
    if (c.lastbit !== 1'b1 || c.lastwhich !== 7) begin errs++; $error("is_dev=1: %b %0d, expected 1 7", c.lastbit, c.lastwhich); end
    c.is_dev = 0;
    c.t();
    if (c.lastbit !== 1'b0 || c.lastwhich !== 7) begin errs++; $error("is_dev=0: %b %0d, expected 0 7", c.lastbit, c.lastwhich); end
    if (errs == 0) $display("PASSED");
  end
endmodule
