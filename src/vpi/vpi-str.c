//
//  Copyright (C) 2024  Nick Gasson
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

#include "util.h"
#include "vpi/vpi-priv.h"
#include "vpi/sv_vpi_user.h"
#include "vpi/vpi_ext_nvc.h"

// LCOV_EXCL_START /////////////////////////////////////////////////////////////

static const char *vpi_fallback_str(int value)
{
   static char buf[16];
   checked_sprintf(buf, sizeof(buf), "%d", value);
   return buf;
}

const char *vpi_type_str(PLI_INT32 type)
{
   switch (type) {
   case vpiSysFuncCall: return "vpiSysFuncCall";
   case vpiSysTaskCall: return "vpiSysTaskCall";
   case vpiIterator: return "vpiIterator";
   case vpiCallback: return "vpiCallback";
   case vpiConstant: return "vpiConstant";
   case vpiOperation: return "vpiOperation";
   case vpiModule: return "vpiModule";
   case vpiScope: return "vpiScope";
   case vpiPort: return "vpiPort";
   case vpiNet: return "vpiNet";
   case vpiNetBit: return "vpiNetBit";
   case vpiNetArray: return "vpiNetArray";
   case vpiReg: return "vpiReg";
   case vpiRegBit: return "vpiRegBit";
   case vpiRegArray: return "vpiRegArray";
   case vpiMemory: return "vpiMemory";
   case vpiMemoryWord: return "vpiMemoryWord";
   case vpiIntegerVar: return "vpiIntegerVar";
   case vpiTimeVar: return "vpiTimeVar";
   case vpiRealVar: return "vpiRealVar";
   case vpiNamedEvent: return "vpiNamedEvent";
   case vpiNamedBegin: return "vpiNamedBegin";
   case vpiNamedFork: return "vpiNamedFork";
   case vpiGenScope: return "vpiGenScope";
   case vpiGenScopeArray: return "vpiGenScopeArray";
   case vpiParameter: return "vpiParameter";
   case vpiRange: return "vpiRange";
   case vpiModuleArray: return "vpiModuleArray";
   case vpiPackage: return "vpiPackage";
   case vpiInterface: return "vpiInterface";
   case vpiProgram: return "vpiProgram";
   case vpiInterfaceArray: return "vpiInterfaceArray";
   case vpiModport: return "vpiModport";
   case vpiRefObj: return "vpiRefObj";
   case vpiLongIntVar: return "vpiLongIntVar";
   case vpiShortIntVar: return "vpiShortIntVar";
   case vpiIntVar: return "vpiIntVar";
   case vpiShortRealVar: return "vpiShortRealVar";
   case vpiByteVar: return "vpiByteVar";
   case vpiStringVar: return "vpiStringVar";
   case vpiEnumVar: return "vpiEnumVar";
   case vpiStructVar: return "vpiStructVar";
   case vpiUnionVar: return "vpiUnionVar";
   case vpiBitVar: return "vpiBitVar";
   case vpiChandleVar: return "vpiChandleVar";
   case vpiPackedArrayVar: return "vpiPackedArrayVar";
   case vpiForeignInst: return "vpiForeignInst";
   default: return vpi_fallback_str(type);
   }
}

const char *vpi_method_str(PLI_INT32 type)
{
   switch (type) {
   case vpiArgument: return "vpiArgument";
   case vpiSysTfCall: return "vpiSysTfCall";
   default: return vpi_fallback_str(type);
   }
}

const char *vpi_property_str(PLI_INT32 property)
{
   switch (property) {
   case vpiType: return "vpiType";
   case vpiConstType: return "vpiConstType";
   case vpiSize: return "vpiSize";
   default: return vpi_fallback_str(property);
   }
}

const char *vpi_op_type_str(PLI_INT32 subtype)
{
   switch (subtype) {
   case vpiNullOp: return "vpiNullOp";
   default: return vpi_fallback_str(subtype);
   }
}

// LCOV_EXCL_STOP //////////////////////////////////////////////////////////////
