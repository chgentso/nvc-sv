// Self-checking: bounded mailboxes.
// Expected values: IEEE 1800-2017 15.4 (mailbox), 15.4.1 (new(bound)), 15.4.2 (put blocks while full), 15.4.3
// (try_put returns 0 when full), 15.4.5 (get blocks while empty; FIFO order), 15.4.8 (try_peek copies the
// oldest message without removing it), 15.4.? (num). Producer puts 0..4 into a mailbox of bound 2; the consumer
// starts at t=10 and takes one message every 10 ns. Times in ns (explicit timescale).
`timescale 1ns/1ns
`define CHK(g, e) if ((g) !== (e)) begin errs++; $error("check failed at line %0d", `__LINE__); end
module svmbox1;
   mailbox #(int) mb;
   int v; int errs;
   int pt[5], gt[5], gv[5];
   int r1, r2, r3, rp, vp;
   initial begin
      mb = new(2);
      fork
         for (int i = 0; i < 5; i++) begin
            mb.put(i);
            pt[i] = $time;
         end
         begin
            #10;
            for (int i = 0; i < 5; i++) begin
               mb.get(v);
               gv[i] = v; gt[i] = $time;
               #10;
            end
         end
      join
      `CHK($time, 60)
      for (int i = 0; i < 5; i++) begin
         `CHK(gv[i], i)                       // FIFO order
         `CHK(gt[i], 10 * (i + 1))            // one get every 10 ns from t = 10
      end
      // put i completes when a slot is free: 0 and 1 at once, then each after one get
      `CHK(pt[0], 0) `CHK(pt[1], 0) `CHK(pt[2], 10) `CHK(pt[3], 20) `CHK(pt[4], 30)
      `CHK(mb.num(), 0)
      r1 = mb.try_put(7); r2 = mb.try_put(8); r3 = mb.try_put(9);
      `CHK(r1, 1) `CHK(r2, 1) `CHK(r3, 0)
      `CHK(mb.num(), 2)
      rp = mb.try_peek(vp);
      `CHK(rp, 1) `CHK(vp, 7)
      `CHK(mb.num(), 2)                       // peek does not remove
      if (errs == 0) $display("PASSED");
      $finish;
   end
endmodule
