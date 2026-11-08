-- IEEE 1850-2010 5.2.3.9 and 5.2.3.10: nondet() returns a value from its
-- value set and nondet_vector(N, set) returns an N-bit vector whose
-- value is in the value set.  Every check below holds for any choice
-- of the non-deterministic value.

entity psl29 is
end entity;

architecture test of psl29 is
    signal clk : bit := '0';
    signal a   : bit;
    constant C : integer := 10;

    default clock is rising_edge(clk);
begin

    stim: process is
    begin
        for i in 0 to 15 loop
            a <= bit'val(i mod 2);
            wait for 5 ns;
            clk <= '1';
            wait for 3 ns;
            clk <= '0';
            wait for 2 ns;
        end loop;
        wait;
    end process;

    -- All must hold
    n1: assert always nondet({7}) = 7;
    n2: assert always nondet({2, 4 to 6}) /= 3;
    n3: assert always nondet({2, 4 to 6}) >= 2 and nondet({2, 4 to 6}) <= 6;
    n4: assert always nondet({C to C + 2}) >= C;
    n5: assert always nondet({C to C + 2}) <= C + 2;
    n6: assert always (a = '1') -> (nondet(boolean) or true);
    n7: assert always {a; nondet({1}) = 1} |=> (a = '1');

    v1: assert always countones(nondet_vector(4, {5})) = 2;
    v2: assert always countones(nondet_vector(5, {31})) = 5;
    v3: assert always countones(nondet_vector(8, {0})) = 0;
    v4: assert always onehot(nondet_vector(4, {1, 2, 4, 8}));
    v5: assert always onehot0(nondet_vector(3, {0, 1, 2, 4}));
    v6: assert always countones(nondet_vector(4, boolean)) <= 4;
    v7: assert always countones(nondet_vector(C - 6, {3 to 3})) = 2;

end architecture;
