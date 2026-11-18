// A string parameter selects a literal of an enumeration or boolean
// generic of a VHDL entity
module mixed58;
  parameter FAM = "virtex7";
  mixed58_e #(.C_FAMILY(FAM), .C_BOOL("TRUE"), .C_NAME("abc"), .C_EXPECT("virtex7 true abc")) u1 (.a(1'b0));
  mixed58_e #(.C_FAMILY(2), .C_BOOL(1), .C_EXPECT("artix7 true none")) u2 (.a(1'b0));
  mixed58_e #(.C_EXPECT("zynq false none")) u3 (.a(1'b0));
endmodule
