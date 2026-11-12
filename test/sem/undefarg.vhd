package undefarg is
    type int_array is array (natural range <>) of integer;
    function check (v : int_array; x : integer) return boolean;
    function test (v : int_array; u : missing_t) return boolean;  -- Error
end package;

package body undefarg is
    function check (v : int_array; x : integer) return boolean is
    begin
        return true;
    end function;

    function test (v : int_array; u : missing_t) return boolean is  -- Error
    begin
        -- The undefined type of U makes the overload resolution of CHECK
        -- skip solving its first argument
        if check(v(1)(2), u) then  -- Error
            return false;
        end if;
        return true;
    end function;
end package body;
