// $bits applied to handle-like and string types.
// IEEE 1800-2017 20.6.2 defines $bits only for fixed-size bit-stream types;
// the size of a class handle, event, chandle, string or built-in class
// (mailbox/semaphore) is not specified by the LRM (6.19, 8.4, 15.4, 15.3, 6.16).
// Only LRM-implied properties are checked: the calls are accepted, the result
// is a non-negative value, and two handles of the same
// kind (class handles, here the user class P, mailbox and semaphore which are
// all class types) have a size independent of the class they refer to.
// Expected values: IEEE 1800-2017 20.6.2, 8.4
module svbitshandle1;
  class P; int x; endclass
  class Q; int x, y, z; endclass
  P a; Q a2; event e; chandle ch; string s = "abc"; mailbox mb; semaphore sm;
  int errors = 0;
  initial begin
    automatic int ba = $bits(a), be = $bits(e), bc = $bits(ch), bs = $bits(s);
    automatic int bm = $bits(mb), bsm = $bits(sm), ba2 = $bits(a2);
    if (ba < 0 || be < 0 || bc < 0 || bs < 0 || bm < 0 || bsm < 0) begin
      $error("negative $bits result"); errors++;
    end
    // a class handle has the same size whatever class it refers to
    if (ba != ba2) begin $error("$bits of handles to different classes differ %0d %0d", ba, ba2); errors++; end
    if (ba != bm || ba != bsm) begin
      $error("$bits of class handles differ: P=%0d mailbox=%0d semaphore=%0d", ba, bm, bsm); errors++;
    end
    // $bits of a class type name is accepted (value not specified by the LRM)
    if ($bits(P) < 0) begin $error("negative $bits(P)"); errors++; end
    if (errors == 0) $display("PASSED");
  end
endmodule
