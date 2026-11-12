-- Generic map in a configuration refers to a package that is not
-- otherwise used by the design
package vcrash_config1_types is
    type vec4 is array (0 to 3) of integer;
end package;

use work.vcrash_config1_types.all;

package vcrash_config1_const is
    constant c : vec4 := (others => 5);
end package;

-------------------------------------------------------------------------------

use work.vcrash_config1_types.all;

entity vcrash_config1_check is
    generic ( i : vec4 );
end entity;

architecture test of vcrash_config1_check is
begin
    process is
    begin
        assert i(0) = 5 report integer'image(i(0));
        assert i(3) = 5;
        wait;
    end process;
end architecture;

-------------------------------------------------------------------------------

entity vcrash_config1_top is
end entity;

architecture test of vcrash_config1_top is
    component check is
    end component;
begin
    u: check;
end architecture;

-------------------------------------------------------------------------------

use work.vcrash_config1_const.all;

configuration vcrash_config1_conf of vcrash_config1_top is
    for test
        for u : check
            use entity work.vcrash_config1_check
                generic map ( i => c );
        end for;
    end for;
end configuration;

-------------------------------------------------------------------------------

entity vcrash_config1 is
end entity;

architecture test of vcrash_config1 is
begin
    u: configuration work.vcrash_config1_conf;
end architecture;
