module mem_leaf(input logic clk, input logic we, input logic [19:0] addr,
                input logic [7:0] din, output logic [7:0] dout,
                input logic [7:0] ram [2**20]);
   logic [7:0] ram_local [2**20];
   logic [7:0] tiny [4];
   always @(posedge clk) begin
      if (we) ram_local[addr] <= din;
      dout <= ram_local[addr];
      tiny[addr[0]] <= din;
   end
endmodule

module mem1(input logic clk, input logic we, input logic [19:0] addr,
            input logic [7:0] din, output logic [7:0] dout);
   logic [7:0] mem [2**20];
   logic [7:0] tab [3:0][0:511][0:255];
   logic [7:0] q, unused;
   always @(posedge clk) begin
      if (we) mem[addr] <= din;
      q <= mem[addr] ^ tab[0][0][0];
   end
   assign dout = q;
   mem_leaf u_leaf(.clk(clk), .we(we), .addr(addr), .din(din), .dout(unused),
                   .ram(mem));
endmodule
