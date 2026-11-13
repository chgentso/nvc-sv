package bitstring2 is
    constant c1 : string := b"1Z-";       -- OK
    constant c2 : string := o"7U";        -- OK
    constant c3 : string := 0sb"";        -- OK
    constant c4 : string := 2sx"E";       -- OK
    constant e1 : string := b"2";         -- Error
    constant e2 : string := o"8";         -- Error
    constant e3 : string := sb"0129";     -- Error
    constant e4 : string := 0sx"0";       -- Error
end package;
