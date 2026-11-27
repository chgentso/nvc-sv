-- A postponed process cannot cause a delta cycle, also with a transaction
-- that does not change the value of the driver
entity signal40 is
end entity;

architecture test of signal40 is
    signal s : bit_vector(3 downto 0) := "0000";
    signal t : bit := '0';
begin

    t <= '1' after 1 ns;

    post: postponed process (t) is
    begin
        if t = '1' then
            s <= "0000";
        end if;
    end process;

end architecture;
