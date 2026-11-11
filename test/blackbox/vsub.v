module vsub (input d, output q);
  cellinv u_inv (.a(d), .y(q));
endmodule
