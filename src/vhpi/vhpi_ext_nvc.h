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

#ifndef VHPI_EXT_NVC_H
#define VHPI_EXT_NVC_H

#ifdef VHPI_USER_H
#error This file must be included before vhpi_user.h
#endif

#define VHPIEXTEND_CLASSES ,                    \
   vhpiVerilogModuleK = 2001

#define VHPIEXTEND_INT_PROPERTIES ,             \
   vhpiRandomSeedP = 1100,                      \
   vhpiNvcStateP = 1101

#define VHPIEXTEND_PHYS_PROPERTIES ,            \
   vhpiNvcWakeTimeP = 1659

// Value of vhpiNvcStateP of a process: a combination of these flags.  A
// process with none of them is suspended in a wait statement with a
// sensitivity to signals.
#define vhpiNvcProcRunning  0x01   // The process is executing
#define vhpiNvcProcRunnable 0x02   // It will run in the current delta cycle
#define vhpiNvcProcTimeout  0x04   // It has a time out: vhpiNvcWakeTimeP
#define vhpiNvcProcEnded    0x08   // It has finished (Verilog only)

#endif  // VHPI_EXT_NVC_H
