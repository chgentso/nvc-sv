//
//  Copyright (C) 2026  Nick Gasson
//
//  This program is free software: you can redistribute it and/or modify
//  it under the terms of the GNU General Public License as published by
//  the Free Software Foundation, either version 3 of the License, or
//  (at your option) any later version.
//
//  This program is distributed in the hope that it will be useful,
//  but WITHOUT ANY WARRANTY; without even the implied warranty of
//  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//  GNU General Public License for more details.
//
//  You should have received a copy of the GNU General Public License
//  along with this program.  If not, see <http://www.gnu.org/licenses/>.
//

#ifndef VPI_EXT_NVC_H
#define VPI_EXT_NVC_H

// Object types outside the range used by the IEEE 1800 headers

// Instance of a VHDL entity in a Verilog design: it has a name and a
// scope but its contents are only visible through VHPI
#define vpiForeignInst 1001

#endif  // VPI_EXT_NVC_H
