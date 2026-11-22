class A; rand bit [15:0] x;
 constraint c { x dist {[0:9]:=1,[10:19]:=2,[20:29]:=3,[30:39]:=4,[40:49]:=5,[50:59]:=6,[60:69]:=7,[70:79]:=8,[80:89]:=9,[90:99]:=10,[100:199]:/5,[200:299]:/5,[300:399]:/5,[400:499]:/5,1000:=3,2000:=3}; }
 function new;  endfunction endclass
module top; A a; int n, f; initial begin a = new; f = 0; void'($value$plusargs("N=%d", n));
 repeat (n) if (!a.randomize()) f++; $display("RES fails=%0d/%0d", f, n); $finish; end endmodule
