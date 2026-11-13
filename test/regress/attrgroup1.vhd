entity attrgroup1 is
end entity;

architecture test of attrgroup1 is
    group pair is (signal, signal);
    group labels is (label <>);
    signal s1, s2 : bit;
    group g1 : pair (s1, s2);
    group g2 : labels (l1, l2);
    attribute n : integer;
    attribute n of g1 : group is 1;
    attribute n of g2 : group is 2;
begin

    l1: process is
    begin
        assert g1'n = 1;
        assert g2'n = 2;
        wait;
    end process;

    l2: s2 <= s1;

end architecture;
