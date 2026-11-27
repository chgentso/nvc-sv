interface sv_iface_port_arr1_if;
  logic [7:0] data;
  modport master(output data);
  modport slave(input data);
endinterface

module sv_iface_port_arr1_sub(sv_iface_port_arr1_if.slave bus[2]);
  wire [7:0] d0 = bus[0].data;
  wire [7:0] d1 = bus[1].data;
endmodule

module sv_iface_port_arr1;
  sv_iface_port_arr1_if arr[2]();
  sv_iface_port_arr1_sub sub(.bus(arr));

  initial begin
    arr[0].data = 8'hA0;
    arr[1].data = 8'hA1;
    #1;
    if (sub.bus[0].data !== 8'hA0 || sub.bus[1].data !== 8'hA1)
      $fatal(1, "got %h %h", sub.bus[0].data, sub.bus[1].data);
    $display("PASSED");
  end
endmodule
