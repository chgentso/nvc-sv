// IEEE 1800-2017 13.5.2: a ref argument is passed by reference, so while a
// task waits, a write to the actual by another process is seen through the
// formal, and a write through the formal is seen at once by others.  The
// actual here is a module variable, an unpacked array element and a class
// property.
class svrefwait1_c;
  int prop;
endclass

module svrefwait1;
  int v;
  int arr[4];
  svrefwait1_c obj;
  int seen1, seen2;

  task automatic watch(ref int r, output int bef, output int aft);
    bef = r;
    #10;                    // Others write the actual meanwhile
    aft = r;
    r = r + 100;            // Must be visible outside immediately
  endtask

  task automatic wait_change(ref int r, input int from);
    wait (r != from);       // Waits on the actual through the reference
  endtask

  initial begin
    int b, a;
    obj = new;

    // Module variable
    v = 1;
    fork
      watch(v, b, a);
      begin #5 v = 2; end
      begin #15 seen1 = v; end
    join
    if (b != 1 || a != 2 || v != 102 || seen1 != 102)
      $fatal(1, "variable: b=%0d a=%0d v=%0d seen=%0d", b, a, v, seen1);

    // Array element
    arr[2] = 7;
    fork
      watch(arr[2], b, a);
      begin #5 arr[2] = 8; arr[1] = 55; end
      begin #15 seen1 = arr[2]; end
    join
    if (b != 7 || a != 8 || arr[2] != 108 || seen1 != 108 || arr[1] != 55)
      $fatal(1, "element: b=%0d a=%0d arr[2]=%0d seen=%0d", b, a, arr[2],
             seen1);

    // Class property
    obj.prop = 20;
    fork
      watch(obj.prop, b, a);
      begin #5 obj.prop = 21; end
      begin #15 seen1 = obj.prop; end
    join
    if (b != 20 || a != 21 || obj.prop != 121 || seen1 != 121)
      $fatal(1, "property: b=%0d a=%0d prop=%0d seen=%0d", b, a, obj.prop,
             seen1);

    // Waiting on the actual through the ref wakes on an outside write
    v = 0;
    fork
      begin wait_change(v, 0); seen2 = $time; end
      begin #7 v = 3; end
    join
    if (seen2 != 52)
      $fatal(1, "wait through ref woke at %0d", seen2);

    $display("PASSED");
  end
endmodule
