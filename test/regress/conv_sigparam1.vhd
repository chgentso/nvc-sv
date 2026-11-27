entity conv_sigparam1 is
    port (x : in bit_vector(3 downto 0));
end entity;

architecture a of conv_sigparam1 is
begin
end architecture;

entity conv_sigparam1_tb is
end entity;

architecture test of conv_sigparam1_tb is
    function to_bv(signal d : bit_vector(3 downto 0)) return bit_vector is
    begin
        return d;
    end function;

    signal s : bit_vector(3 downto 0) := (others => '0');
begin
    u : entity work.conv_sigparam1
        port map (x => to_bv(s));
end architecture;
