package nestedpkg2_outer is
    package inner is
        constant DEF_K : integer;
        constant PUB_K : integer := 7;
        signal sig : integer := 11;
        function compute(x : integer) return integer;

        package deep is
            constant DEEP_K : integer;
            function inc(x : integer) return integer;
        end package;
    end package;

    package mapped is
        generic (N : integer := 2);
        generic map (N => 5);
        constant M_VAL : integer := N * 3;
        function get_m return integer;
    end package;

    function after_nested(x : integer) return integer;
end package;

package body nestedpkg2_outer is
    package body inner is
        constant DEF_K : integer := 13;

        function compute(x : integer) return integer is
        begin
            return x + DEF_K + PUB_K;
        end function;

        package body deep is
            constant DEEP_K : integer := 4;
            function inc(x : integer) return integer is
            begin
                return x + DEEP_K;
            end function;
        end package body;
    end package body;

    package body mapped is
        function get_m return integer is
        begin
            return M_VAL + N;
        end function;
    end package body;

    function after_nested(x : integer) return integer is
    begin
        return x + inner.DEF_K + inner.deep.DEEP_K;
    end function;
end package body;

use work.nestedpkg2_outer.all;

entity nestedpkg2 is
end entity;

architecture test of nestedpkg2 is
begin
    process is
    begin
        assert inner.DEF_K = 13;
        assert inner.PUB_K = 7;
        assert inner.sig = 11;
        assert inner.compute(5) = 25;
        assert inner.deep.DEEP_K = 4;
        assert inner.deep.inc(6) = 10;
        assert mapped.M_VAL = 15;
        assert mapped.get_m = 20;
        assert after_nested(1) = 18;

        inner.sig <= 99;
        wait for 1 ns;
        assert inner.sig = 99;
        wait;
    end process;
end architecture;
