class C; int m[string]; int q[$];
  task automatic run(); foreach (m[k]) begin #1; q.push_back(m[k]); end endtask
endclass
module svblock2;
  C c = new; string keys[$]; string k; int m[string];
  initial begin
    c.m["a"]=1; c.m["b"]=2; c.run();
    if (c.q.size()!=2 || c.q[1]!=2) $display("FAILED q"); 
    m["p"]=1; m["q"]=2;
    if (m.first(k)) do begin #1; keys.push_back(k); end while (m.next(k));
    if (keys.size()!=2 || keys[1]!="q") $display("FAILED keys");
    fork begin automatic string j; #1; if (m.last(j)) begin #1; if (j!="q") $display("FAILED j"); end end join
    $display("PASSED");
  end
endmodule
