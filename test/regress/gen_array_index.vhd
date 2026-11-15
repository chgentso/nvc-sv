entity gen_array_index is
end entity;

architecture test of gen_array_index is
    type grid is array (natural range <>, natural range <>) of bit;
    subtype idx2 is natural range 0 to 1;
    subtype idx3 is natural range 0 to 2;
    type fixed is array (idx2, idx3) of bit;

    function cells_fixed_op generic (type m_t is array (idx2, idx3) of bit)
        parameter (m : m_t) return natural is
    begin
        return m'length(1) * m'length(2);
    end function;

    function cells generic (type m_t is array (natural range <>, natural range <>) of bit)
        parameter (m : m_t) return natural is
    begin
        return m'length(1) * m'length(2);
    end function;

    function cells_fixed is new cells_fixed_op generic map (m_t => fixed);
    function cells_grid is new cells generic map (m_t => grid);
begin
    process is
        constant f6 : fixed := (others => (others => '1'));
        constant g4 : grid(0 to 1, 0 to 1) := (others => (others => '1'));
    begin
        assert cells_fixed(f6) = 6;
        assert cells_grid(g4) = 4;
        wait;
    end process;
end architecture;
