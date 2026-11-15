clas k;
  int v;
endclass

module linterr22;
  static k a = new, b = new;
  k q[$];
  initial begin
    a = b;
    q.push_back(a);
  end
endmodule
