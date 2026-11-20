package attr27_pkg is
    type counter_t is protected
        procedure inc;
        impure function get return integer;
    end protected;
end package;

package body attr27_pkg is
    type counter_t is protected body
        variable count : integer := 0;
        procedure inc is
        begin
            count := count + 1;
        end procedure;
        impure function get return integer is
        begin
            return count;
        end function;
    end protected body;
end package body;

use work.attr27_pkg.all;

entity attr27 is
end entity;

architecture test of attr27 is
    type local_pt is protected
        impure function val return integer;
    end protected;

    type local_pt is protected body
        impure function val return integer is
        begin
            return 7;
        end function;
    end protected body;

    shared variable c : counter_t;
    shared variable l : local_pt;
begin
    p : process is
    begin
        assert c.get'simple_name = "get";
        assert c.inc'simple_name = "inc";
        assert c.get'instance_name = ":work:attr27_pkg:counter_t:get[return integer]:";
        assert c.get'path_name = ":work:attr27_pkg:counter_t:get[return integer]:";
        assert c.inc'instance_name = ":work:attr27_pkg:counter_t:inc[]:";
        assert l.val'simple_name = "val";
        assert l.val'path_name = ":attr27:local_pt:val[return integer]:";
        assert l.val'instance_name = ":attr27(test):local_pt:val[return integer]:";
        wait;
    end process;
end architecture;
