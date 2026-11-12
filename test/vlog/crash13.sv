class msg;
endclass

interface intf;
  msg m;
endinterface

module top;
  intf i ();
  initial begin
    top.i.m = new;
  end
endmodule
