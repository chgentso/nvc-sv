module svnullcast1;
  class Base;
  endclass

  class Child extends Base;
  endclass

  Base b;
  Child c;
  Base d;
  int rc;

  initial begin
    c = new();
    b = null;
    rc = $cast(c, b);
    if (rc !== 1 || c != null) $fatal(1, "null source, rc=%0d", rc);

    c = new();
    rc = $cast(c, null);
    if (rc !== 1 || c != null) $fatal(1, "null literal, rc=%0d", rc);

    d = Base'(null);
    if (d != null) $fatal(1, "null type cast");

    if (!(null == null)) $fatal(1, "null == null");
    if (null != null) $fatal(1, "null != null");
    if ((null == null) && (d != null)) $fatal(1, "logical");
    $display("PASS");
  end
endmodule
