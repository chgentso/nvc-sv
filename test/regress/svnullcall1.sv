// Method called on a null handle
class idc;
  int v = 5;
  function void clear(); v = 0; endfunction
endclass
module svnullcall1;
  idc p;
  initial begin
    $display("before");
    p.clear();
    $display("after");
  end
endmodule
