entity case_const1 is
end entity;

architecture test of case_const1 is
    type mode_t is (m_a, m_b, m_c);
    type mode_table_t is array (mode_t) of natural;

    constant tbl : mode_table_t := (m_a => 10, m_b => 20, m_c => 30);
    constant c_a : natural := tbl(m_a);
    constant c_b : natural := tbl(m_b);
    constant c_c : natural := tbl(m_c);

    function decode(x : natural) return natural is
    begin
        case x is
            when c_a => return 1;
            when c_b => return 2;
            when c_c => return 3;
            when others => return 0;
        end case;
    end function;
begin
    check : process is
    begin
        assert decode(10) = 1;
        assert decode(20) = 2;
        assert decode(30) = 3;
        assert decode(99) = 0;
        wait;
    end process;
end architecture;
