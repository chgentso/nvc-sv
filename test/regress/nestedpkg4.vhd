entity nestedpkg4 is
end entity;

architecture test of nestedpkg4 is
    type counter_pt is protected
        procedure add(d : integer);
        impure function value return integer;
    end protected;

    type counter_pt is protected body
        variable base : integer := 0;

        package helper is
            variable calls : integer := 0;
            procedure bump(d : integer);
        end package;

        package body helper is
            procedure bump(d : integer) is
            begin
                calls := calls + 1;
                base := base + d;
            end procedure;
        end package body;

        procedure add(d : integer) is
        begin
            helper.bump(d);
        end procedure;

        impure function value return integer is
        begin
            return base * 100 + helper.calls;
        end function;
    end protected body;

    shared variable c1, c2 : counter_pt;

    package gen_local is
        generic (K : integer);
        function mul(x : integer) return integer;
    end package;

    package body gen_local is
        function mul(x : integer) return integer is
        begin
            return x * K;
        end function;
    end package body;

    package inst3 is new gen_local generic map (K => 3);
    package inst5 is new gen_local generic map (K => 5);

    function sub_with_pkg(x : integer) return integer is
        package lp is
            variable acc : integer := x;
            procedure step(d : integer);
        end package;
        package body lp is
            procedure step(d : integer) is
            begin
                acc := acc + d;
            end procedure;
        end package body;
    begin
        lp.step(4);
        lp.acc := lp.acc + 1;
        return lp.acc;
    end function;
begin
    process is
    begin
        c1.add(2);
        c1.add(3);
        c2.add(7);
        -- Verify c1 and c2 have independent nested package state
        assert c1.value = 502 report "c1=" & integer'image(c1.value);
        assert c2.value = 701 report "c2=" & integer'image(c2.value);

        -- Verify subprogram nested package re-initializes fresh on each call
        assert sub_with_pkg(10) = 15;
        assert sub_with_pkg(20) = 25;
        assert sub_with_pkg(0) = 5;

        -- Verify local nested generic package instantiations
        assert inst3.mul(4) = 12;
        assert inst5.mul(4) = 20;
        wait;
    end process;
end architecture;
