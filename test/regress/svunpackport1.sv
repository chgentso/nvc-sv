// Ports of unpacked aggregate types: an output variable port of an
// unpacked structure type with an unpacked array member, and input and
// output ports that are unpacked arrays of unpacked structures, written
// procedurally and by continuous assignment, element by element and as
// a whole.  A port may have any data type that a variable may have.
// IEEE 1800-2017 23.2.2.3 (rules for determining port kind, data type
// and direction), 23.3.3.3 (port connection rules for variables),
// 7.2 (structures), 7.4 (unpacked arrays), 7.6 (array assignments).

typedef struct {
   logic       a;
   logic [7:0] b[0:2];
} svunpackport1_rec;

typedef struct {
   logic [3:0] x;
   int         y;
} svunpackport1_pair;

module svunpackport1_rec_out (output svunpackport1_rec r);
   initial begin
      r.a = 1'b1;
      r.b[0] = 8'h10;
      r.b[1] = 8'h21;
      r.b[2] = 8'h32;
   end
endmodule

module svunpackport1_arr (input  svunpackport1_rec  i[0:1],
                          output svunpackport1_rec  o[0:1],
                          output svunpackport1_pair p[1:3]);
   assign o = i;

   initial begin
      p[1].x = 4'h1;
      p[1].y = -1;
      p[3].x = 4'hc;
      p[3].y = 300;
   end
endmodule

module svunpackport1;
   svunpackport1_rec  r;
   svunpackport1_rec  ai[0:1], ao[0:1];
   svunpackport1_pair pp[1:3];

   svunpackport1_rec_out u_rec (.r(r));
   svunpackport1_arr     u_arr (.i(ai), .o(ao), .p(pp));

   initial begin
      ai[0].a = 1'b0;
      ai[0].b = '{8'haa, 8'hbb, 8'hcc};
      ai[1].a = 1'b1;
      ai[1].b[2] = 8'h5a;

      #1;
      if (r.a !== 1'b1) $fatal(1, "r.a = %b", r.a);
      if (r.b[0] !== 8'h10 || r.b[1] !== 8'h21 || r.b[2] !== 8'h32)
        $fatal(1, "r.b = %p", r.b);

      if (ao[0].a !== 1'b0) $fatal(1, "ao[0].a = %b", ao[0].a);
      if (ao[0].b[0] !== 8'haa || ao[0].b[1] !== 8'hbb
          || ao[0].b[2] !== 8'hcc)
        $fatal(1, "ao[0].b = %p", ao[0].b);
      if (ao[1].a !== 1'b1 || ao[1].b[2] !== 8'h5a)
        $fatal(1, "ao[1] = %p", ao[1]);
      if (ao[1].b[0] !== 8'hxx) $fatal(1, "ao[1].b[0] = %h", ao[1].b[0]);

      if (pp[1].x !== 4'h1 || pp[1].y !== -1) $fatal(1, "pp[1] = %p", pp[1]);
      if (pp[3].x !== 4'hc || pp[3].y !== 300) $fatal(1, "pp[3] = %p", pp[3]);
      if (pp[2].x !== 4'hx || pp[2].y !== 0) $fatal(1, "pp[2] = %p", pp[2]);

      // A later change of an element propagates through the port
      ai[1].b[0] = 8'h01;
      #1;
      if (ao[1].b[0] !== 8'h01) $fatal(1, "ao[1].b[0] = %h", ao[1].b[0]);

      $display("PASSED");
   end
endmodule
