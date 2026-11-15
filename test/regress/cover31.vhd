entity cover31_a is
end entity;

architecture test of cover31_a is
    signal x : bit_vector(1 downto 0);
begin

    g: for i in 0 to 1 generate
        process is
        begin
            x(i) <= '1';
            wait for 1 ns;
            wait;
        end process;
    end generate;

end architecture;

-------------------------------------------------------------------------------

entity cover31 is
end entity;

architecture test of cover31 is
begin

    u: entity work.cover31_a;
    v: entity work.cover31_b;

end architecture;
