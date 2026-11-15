clas k;
  int v;
endclass

module linterr22;
  automatic k a = new, b = new;
  k q[$];
  initial begin
    a = b;
    q.push_back(a);
  end
endmodule
