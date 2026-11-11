entity drivers1 is
end entity;

architecture test of drivers1 is
    type bv4 is array (3 downto 0) of bit;

    function resolve (v : bit_vector) return bit is
    begin
        for i in v'range loop
            if v(i) = '1' then
                return '1';
            end if;
        end loop;
        return '0';
    end function;

    subtype rbit is resolve bit;

    signal x    : bit_vector(3 downto 0);
    signal r    : rbit;
    signal z    : bit_vector(1 downto 0);
    signal u    : bit;                      -- no driver
    signal i    : integer;
begin
    p1: process is
    begin
        x(1 downto 0) <= "10";
        wait;
    end process;

    p2: process is
    begin
        x(3 downto 2) <= "01";
        i <= 5;
        wait;
    end process;

    pa: process is
    begin
        r <= '1';
        wait;
    end process;

    pb: process is
    begin
        r <= '0';
        wait;
    end process;

    b: block is
        port ( o : out bit_vector(1 downto 0);
               n : in  bit_vector(1 downto 0) );
        port map ( o => z, n => x(3 downto 2) );
    begin
        o <= "11";
    end block;
end architecture;
