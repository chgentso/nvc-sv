entity psl_next_e1 is
end entity;

architecture test of psl_next_e1 is
    signal clk : bit := '0';
    signal a, b, c, d, e : bit := '0';
begin
    process
        constant seq_a : bit_vector(0 to 9) := "0100000000";
        constant seq_b : bit_vector(0 to 9) := "0000100000"; -- +3
        constant seq_c : bit_vector(0 to 9) := "0000010000"; -- +4
        constant seq_d : bit_vector(0 to 9) := "0000001000"; -- +5
        constant seq_e : bit_vector(0 to 9) := "0001000000"; -- +2 (too early)
    begin
        for i in 0 to 9 loop
            a <= seq_a(i);
            b <= seq_b(i);
            c <= seq_c(i);
            d <= seq_d(i);
            e <= seq_e(i);
            wait for 5 ns;
            clk <= '1';
            wait for 5 ns;
            clk <= '0';
        end loop;
        wait;
    end process;

    default clock is rising_edge(clk);

    p_lo  : assert always (a -> next_e[3 to 5] (b)) report "p_lo failed";
    p_mid : assert always (a -> next_e[3 to 5] (c)) report "p_mid failed";
    p_hi  : assert always (a -> next_e[3 to 5] (d)) report "p_hi failed";
    p_eq  : assert always (a -> next_e[3 to 3] (b)) report "p_eq failed";
end architecture;
