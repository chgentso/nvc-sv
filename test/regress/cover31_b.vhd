entity cover31_b is
end entity;

architecture test of cover31_b is
    signal y : bit_vector(2 downto 0);
begin

    -- Same label as in another entity but in a different file
    g: for i in 0 to 2 generate
        process is
        begin
            y(i) <= '1';
            wait for 1 ns;
            y(i) <= '0';
            wait;
        end process;
    end generate;

end architecture;
