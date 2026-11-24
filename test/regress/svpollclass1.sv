// Class static variables inside parameterized classes are stored in memory
// rather than signals and must still wake wait() pollers (IEEE 1800-2017 8.9).
package svpollclass1_pkg;
  class box #(type T = int);
    static int ready = 0;
    static task wait_ready();
      wait (ready == 1);
    endtask
  endclass
endpackage

module svpollclass1;
  import svpollclass1_pkg::*;
  int woke = 0;

  initial begin
    fork
      begin
        box#(int)::wait_ready();
        woke = 1;
      end
      begin
        #5 box#(int)::ready = 1;
      end
    join
    if (woke != 1)
      $fatal(1, "wait on parameterized class static variable did not wake");
    $display("PASSED");
    $finish;
  end
endmodule
