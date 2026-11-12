typedef class cls_b;

class cls_a #(parameter P = 12);
  cls_b #(P + 1) b;
endclass

class cls_b #(parameter P = 12);
  cls_a #(P + 1) a;
endclass

module top;
  cls_a #(.P(15)) c;
endmodule
