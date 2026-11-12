-- Inout port with conversion functions in both the formal and the actual
entity vcrash_inoutconv1_sub is
    port ( f : inout integer := 3 );
end entity;

architecture test of vcrash_inoutconv1_sub is
begin
end architecture;

-------------------------------------------------------------------------------

entity vcrash_inoutconv1 is
end entity;

architecture test of vcrash_inoutconv1 is
    type vec is array (natural range <>) of bit;
    subtype vec4 is vec(0 to 3);

    function to_vec (s : integer) return vec4 is
    begin
        return "1010";
    end function;

    function to_int (s : vec) return integer is
    begin
        return s'length - 1;
    end function;

    signal s : vec4 := "0000";
begin
    u: entity work.vcrash_inoutconv1_sub
        port map ( to_vec(f) => to_int(s) );

    process is
    begin
        wait for 1 ns;
        assert s = "1010";
        wait;
    end process;
end architecture;
