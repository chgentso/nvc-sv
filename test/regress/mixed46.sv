module vm (input logic en, input logic d, inout wire [1:0] io, inout wire s,
           output wire [1:0] rd, output wire rs);
  assign io = en ? {2{d}} : 2'bzz;
  assign s = en ? d : 1'bz;
  assign rd = io;
  assign rs = s;
endmodule
