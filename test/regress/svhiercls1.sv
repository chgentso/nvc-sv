// Expected values: IEEE 1800-2017 8.4 (class objects, null handle comparison), 8.7 (constructors),
// 23.6 (hierarchical names: a handle variable in another module can be compared with null and dereferenced).
class q;
  int n = 3;
  function int get_count(); return n; endfunction
endclass
module sub;
  q rx_queue;
  initial rx_queue = new;
endmodule
module svhiercls1;
  sub EMU();
  int err = 0;
  initial begin
    #1;
    if (EMU.rx_queue != null && EMU.rx_queue.get_count()) begin
      if (EMU.rx_queue.get_count() != 3) begin err++; $error("count %0d", EMU.rx_queue.get_count()); end
    end else begin err++; $error("handle null or count zero"); end
    if (err == 0) $display("PASSED");
  end
endmodule
