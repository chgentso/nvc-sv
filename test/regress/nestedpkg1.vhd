entity nestedpkg1_sub is
    generic (BASE : integer);
    port (o : out integer);
    package ent_pkg is
        constant BIAS : integer;
        function add_bias(x : integer) return integer;
    end package;
    package body ent_pkg is
        constant BIAS : integer := 3;
        function add_bias(x : integer) return integer is
        begin
            return x + BIAS;
        end function;
    end package body;
end entity;

architecture rtl of nestedpkg1_sub is
    package p1 is
        function sum_res(v : integer_vector) return integer;
        subtype res_int is sum_res integer;
        signal local_s : res_int := 0;

        package p2 is
            constant SCALE : integer;
            function scale_up(x : integer) return integer;
        end package;
    end package;

    package body p1 is
        function sum_res(v : integer_vector) return integer is
            variable acc : integer := 0;
        begin
            for i in v'range loop
                acc := acc + v(i);
            end loop;
            return acc;
        end function;

        package body p2 is
            constant SCALE : integer := 2;
            function scale_up(x : integer) return integer is
            begin
                return x * SCALE + BASE;
            end function;
        end package body;
    end package body;
begin
    p1.local_s <= ent_pkg.add_bias(BASE);
    p1.local_s <= p1.p2.scale_up(4);
    o <= p1.local_s;
end architecture;

entity nestedpkg1 is
end entity;

architecture test of nestedpkg1 is
    signal o1, o2 : integer;
begin
    u1: entity work.nestedpkg1_sub generic map (BASE => 10) port map (o => o1);
    u2: entity work.nestedpkg1_sub generic map (BASE => 20) port map (o => o2);

    g: for i in 1 to 2 generate
        package gp is
            constant K : integer := i * 100;
            function f return integer;
        end package;
        package body gp is
            function f return integer is
            begin
                return K + i;
            end function;
        end package body;
    begin
        process is
        begin
            assert gp.f = i * 101;
            wait;
        end process;
    end generate;

    check: process is
    begin
        wait for 1 ns;
        -- u1: (10 + 3) + (4 * 2 + 10) = 13 + 18 = 31
        -- u2: (20 + 3) + (4 * 2 + 20) = 23 + 28 = 51
        assert o1 = 31 report "o1=" & integer'image(o1);
        assert o2 = 51 report "o2=" & integer'image(o2);
        wait;
    end process;
end architecture;
