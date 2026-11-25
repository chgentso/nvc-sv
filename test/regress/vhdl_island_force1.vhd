entity vhdl_island_force1 is
end entity;

architecture test of vhdl_island_force1 is
    signal a, b, s, o, q, qn : bit := '0';
begin
    s  <= a;
    o  <= not s;

    p_q : process (b) is
    begin
        q <= '0';
    end process;
    qn <= not q;

    p_check : process is
    begin
        wait for 1 ns;
        assert s = '0' and o = '1' and q = '0' and qn = '1' severity failure;

        -- Force / release on a signal with concurrent driver and consumer
        s <= force '1';
        assert s = '0' severity failure;
        wait for 0 ns;
        assert s'active severity failure;
        assert s = '1' severity failure;
        wait for 0 ns;
        assert o = '0' severity failure;

        wait for 1 ns;
        s <= release;
        wait for 0 ns;
        assert s'active severity failure;
        assert s = '0' severity failure;
        wait for 0 ns;
        assert o = '1' severity failure;

        -- 'active on a same-value transaction driven by a combinational process
        wait for 1 ns;
        b <= '1';
        wait for 0 ns;
        assert b'active severity failure;
        wait for 0 ns;
        assert q'active severity failure;
        assert q = '0' severity failure;
        wait;
    end process;
end architecture;
