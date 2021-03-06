/*
    Copyright (c) 2002-2009 Tampere University of Technology.

    This file is part of TTA-Based Codesign Environment (TCE).

    Permission is hereby granted, free of charge, to any person obtaining a
    copy of this software and associated documentation files (the "Software"),
    to deal in the Software without restriction, including without limitation
    the rights to use, copy, modify, merge, publish, distribute, sublicense,
    and/or sell copies of the Software, and to permit persons to whom the
    Software is furnished to do so, subject to the following conditions:

    The above copyright notice and this permission notice shall be included in
    all copies or substantial portions of the Software.

    THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
    IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
    FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
    THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
    LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
    FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
    DEALINGS IN THE SOFTWARE.
 */
/**
 * Generates OpenCL C extension functions and definitions for the
 * (custom) operations in the given ADF.
 *
 * @author Pekka Jääskeläinen 2009 (pjaaskel-no.spam-cs.tut.fi)
 *
 * @note rating: red
 */

#include <iostream>
#include <fstream>
#include <set>
#include <assert.h>
#include <boost/format.hpp>

#include "OperationPool.hh"
#include "Operation.hh"
#include "Conversion.hh"
#include "OperationIndex.hh"
#include "Operand.hh"
#include "Machine.hh"
#include "StringTools.hh"
#include "HWOperation.hh"


/**
 * Returns a C type string for the given operand type.
 */
std::string
operandTypeCString(const Operand& operand) {
    switch (operand.type()) {
    default:
    case Operand::SINT_WORD:
        return "int";
        break;
    case Operand::UINT_WORD:
        return "unsigned";
        break;
    case Operand::FLOAT_WORD:
        return"float";
        break;
    case Operand::DOUBLE_WORD:
        return "double";
        break;
    }
    return "unsigned"; // a good guess ;)
}
/**
 * Writes the macros that can be written to the tceoclext.h header file to
 * stdout.
 *
 * Following macros are produced for all operations found in the
 * given machine (for example, in case of ADDSUB):
 * 
 * #define cl_TCE_ADDSUB
 * #define clADDSUBTCE _TCE_ADDSUB
 *
 * Thus, it's possible to figure out whether ADDSUB is implemented in the
 * machine using the first define and actually call the ADDSUB with the
 * latter.
 */
void 
generateHeader(std::ostream& stream, const TTAMachine::Machine& machine) {
    std::set<std::string> opNames;

    const TTAMachine::Machine::FunctionUnitNavigator fuNav =
        machine.functionUnitNavigator();

    for (int i = 0; i < fuNav.count(); i++) {
        const TTAMachine::FunctionUnit* fu = fuNav.item(i);
        for (int o = 0; o < fu->operationCount(); o++) {
            const std::string opName = fu->operation(o)->name();
            opNames.insert(StringTools::stringToUpper(opName));
        }
    }
    stream 
        << "// automatically generated by tceoclextgen" << std::endl
        << std::endl;

    for (std::set<std::string>::const_iterator i = opNames.begin(); 
         i != opNames.end(); ++i) {
        std::string name = (*i);
        stream 
            << (boost::format(
                    "#define cl_TCE_%s\n"
                    "#define cl%sTCE _TCE_%s\n\n") 
                % name % name % name).str();
    }
}

int main(int argc, char* argv[]) {
    
    if (argc != 2) {
        std::cout << "Usage: tceoclextgen ADF" << std::endl;
        return EXIT_FAILURE;
    }

    try {
        TTAMachine::Machine* machine = 
            TTAMachine::Machine::loadFromADF(argv[1]);
        generateHeader(std::cout, *machine);

        delete machine; machine = NULL;
    } catch (const Exception& e) {
        std::cerr << e.errorMessage() << std::endl;
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}
