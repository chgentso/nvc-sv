entity ename_unbound1 is
end entity;

architecture test of ename_unbound1 is
    component missing_comp is
        port (q : out bit);
    end component;
    signal q : bit;
begin
    u1 : missing_comp port map (q => q);

    p : process is
        alias a is << signal u1.q : bit >>;
    begin
        assert a = '0';
        wait;
    end process;
end architecture;
