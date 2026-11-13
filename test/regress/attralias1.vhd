entity attralias1 is
end entity;

architecture test of attralias1 is
    attribute pin : integer;
    signal s : bit_vector(0 to 3);
    alias w : bit_vector(0 to 3) is s;
    attribute pin of w : signal is 5;
begin

    p: process is
    begin
        assert s'pin = 5;
        assert w'pin = 5;
        wait;
    end process;

end architecture;
