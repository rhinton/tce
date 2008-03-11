/**
 * Architecture plugin generator for LLVM TCE backend.
 *
 * @author Veli-Pekka Jääskeläinen 2007 (vjaaskel@cs.tut.fi)
 *
 * @note rating: red
 */

#include <iostream>
#include <vector>
#include <map>
#include <fstream>
#include <algorithm>
#include "Machine.hh"
#include "ADFSerializer.hh"
#include "ControlUnit.hh"
#include "Operation.hh"
#include "HWOperation.hh"
#include "FUPort.hh"
#include "Conversion.hh"
#include "MachineCheckResults.hh"
#include "FullyConnectedCheck.hh"
#include "Bus.hh"
#include "Guard.hh"
#include "StringTools.hh"
#include "OperationPool.hh"
#include "OperationNode.hh"
#include "TerminalNode.hh"
#include "OperationDAG.hh"
#include "OperationDAGEdge.hh"
#include "OperationDAGSelector.hh"

/**
 * TCE Backend plugin source code generator.
 */
class TDGen {
public:
    TDGen(const TTAMachine::Machine& mach);
    void generateBackend(std::string& path);

private:
    bool writeRegisterInfo(std::ostream& o);
    void writeInstrInfo(std::ostream& o);
    void writeBackendCode(std::ostream& o);
    void writeTopLevelTD(std::ostream& o);
   
   
    enum RegType {
        GPR = 0,
        RESERVED,
        ARGUMENT,
        RESULT
    };

    struct TerminalDef {
        std::string registerPat;
        std::string registerDag;
        std::string immPat;
        std::string immDag;
    };

    struct RegInfo {
        std::string rf;
        unsigned idx;

        // Comparison operator for ordering in set.
        bool operator<(const RegInfo& other) const {
            if (rf < other.rf ||
                (rf == other.rf && idx < other.idx)) {

                return true;
            }

            return false;
        }
    };

    bool checkRequiredRegisters();
    void analyzeRegisters();

    void writeRegisterDef(
        std::ostream& o,
        const RegInfo& reg,
        const std::string regName,
        const std::string regTemplate,
        const std::string aliases,
        RegType type
        );

    void write64bitRegisterInfo(std::ostream& o);
    void write32bitRegisterInfo(std::ostream& o);
    void write16bitRegisterInfo(std::ostream& o);
    void write8bitRegisterInfo(std::ostream& o);
    void write1bitRegisterInfo(std::ostream& o);
    void writeRARegisterInfo(std::ostream& o);

    void writeOperationDef(std::ostream& o, Operation& op);
    void writeEmulationPattern(
        std::ostream& o,
        const Operation& op,
        const OperationDAG& dag);

    void writeCallDef(std::ostream& o);

    std::string llvmOperationPattern(const std::string& osalOperationName);

    std::string patOutputs(const Operation& op);
    std::string patInputs(const Operation& op, int immOp);

    std::string operandToString(
        const Operand& operand,
        bool match,
        bool immediate);


    std::string operationNodeToString(
        const Operation& op,
        const OperationDAG& dag,
        const OperationNode& node,
        int immOp,
        bool emulationPattern) throw (InvalidData);

    std::string dagNodeToString(
        const Operation& op,
        const OperationDAG& dag,
        const OperationDAGNode& node,
        int immOp,
        bool emulationPattern) throw (InvalidData);

    std::string operationPattern(
        const Operation& op,
        const OperationDAG& dag,
        int immOp);

    OperationDAG* createTrivialDAG(Operation& op);
    bool canBeImmediate(const OperationDAG& dag, const TerminalNode& node);

    const TTAMachine::Machine& mach_;

    // Current dwarf register number.
    unsigned dregNum_;

    // List of 1-bit registers in the target machine.
    std::vector<RegInfo> regs1bit_;
    // List of 8-bit registers in the target machine.
    std::vector<RegInfo> regs8bit_;
    // List of 16-bit registers in the target machine.
    std::vector<RegInfo> regs16bit_;
    // List of 32-bit registers in the target machine.
    std::vector<RegInfo> regs32bit_;
    // List of 64-bit registers in the target machine.
    std::vector<RegInfo> regs64bit_;

    ///  Map of generated llvm register names to
    /// physical register in the machine.
    std::map<std::string, RegInfo> regs_;

    std::vector<std::string> argRegNames_;
    std::vector<std::string> resRegNames_;
    std::vector<std::string> gprRegNames_;

    std::vector<std::pair<std::string, std::string> > opcodes_;

    unsigned static const REQUIRED_I32_REGS;

    std::set<RegInfo> guardedRegs_;
    bool fullyConnected_;
};


/**
 * tceplugingen main function.
 *
 * Generates plugin sourcecode files using TDGen.
 */
int main(int argc, char* argv[]) {
    
    if (argc != 3) {
        std::cout << "Usage: tceplugingen <adf> <path>" << std::endl;
        return EXIT_FAILURE;
    }

    char* adf = argv[1];
    std::string path = argv[2];

    TTAMachine::Machine* mach = NULL;

    try {
        mach = TTAMachine::Machine::loadFromADF(adf);
    } catch (Exception& e) {
        std::cerr << "Error loading " << adf << ":" << std::endl;
        std::cerr << e.errorMessage() << std::endl;
        return EXIT_FAILURE;
    }

    TDGen tg(*mach);
    try {
        tg.generateBackend(path);
    }
    catch (Exception e) {
        std::cerr << "Error occured while creating tce plugin: " 
            + e.errorMessage() << std::endl;
        return EXIT_FAILURE;
    }
    delete mach;
}



// SP, RES, KLUDGE, 3 operands?
unsigned const TDGen::REQUIRED_I32_REGS = 5;


/**
 * Constructor.
 *
 * @param mach Machine to generate plugin for.
 */
TDGen::TDGen(const TTAMachine::Machine& mach):
    mach_(mach), dregNum_(0), fullyConnected_(false) {

}

/**
 * Generates all files required to build a tce backend plugin
 * (excluding static plugin code included from include/llvm/TCE/).
 */
void
TDGen::generateBackend(std::string& path) {

    MachineCheckResults res;
    FullyConnectedCheck fc;
    fullyConnected_ = fc.check(mach_, res);

    std::ofstream regTD;
    regTD.open((path + "/GenRegisterInfo.td").c_str());
    writeRegisterInfo(regTD);
    regTD.close();

    std::ofstream instrTD;
    instrTD.open((path + "/GenInstrInfo.td").c_str());
    writeInstrInfo(instrTD);
    instrTD.close();

    std::ofstream pluginInc;
    pluginInc.open((path + "/Backend.inc").c_str());
    writeBackendCode(pluginInc);
    pluginInc.close();

    std::ofstream topLevelTD;
    topLevelTD.open((path + "/TCE.td").c_str());
    writeTopLevelTD(topLevelTD);
    topLevelTD.close();
}


/**
 * Writes .td definition of a single register to the output stream.
 *
 * @param o Output stream to write the definition to.
 * @param reg Information about the physical register.
 * @param regName Name for the register in the llvm framework.
 * @param regTemplate Base class for the register.
 * @param aliases Comma-separated list of aliases for this register.
 */
void
TDGen::writeRegisterDef(
    std::ostream& o,
    const RegInfo& reg,
    const std::string regName,
    const std::string regTemplate,
    const std::string aliases,
    RegType type
    ) {

    o << "def " << regName << " : " << regTemplate
       << "<\"" << reg.rf << "." << reg.idx
       << "\", [" << aliases << "]>, DwarfRegNum<"
       << dregNum_ << ">;" << std::endl;


    if (type == GPR) {
        gprRegNames_.push_back(regName);
    } else if (type == ARGUMENT) {
        argRegNames_.push_back(regName);
    } else if (type == RESULT) {
        resRegNames_.push_back(regName);
    } else {
        assert(type == RESERVED);
    }

    regs_[regName] = reg;

    dregNum_++;
}


/**
 * Writes .td definitions of all registers in the machine to an output stream.
 *
 * @param o Output stream for the .td definitions.
 * @return True, if the definitions were succesfully generated.
 */
bool
TDGen::writeRegisterInfo(std::ostream& o) {

    analyzeRegisters();

    if (!checkRequiredRegisters()) {
        return false;
    }

    assert(regs32bit_.size() >= REQUIRED_I32_REGS);

    o << "//" << std::endl;
    o << "// This is an automatically generated file!" << std::endl;
    o << "// Do not edit manually." << std::endl;
    o << "//" << std::endl;
    o << std::endl;

    // TODO: Move the following to a static include file?
    o << "class TCEReg<string n, list<Register> aliases> : "
      << "Register<n> {"
      << std::endl;

    o << "   let Namespace = \"TCE\";" << std::endl;
    o << "   let Aliases = aliases;" << std::endl;
    o << "}" << std::endl;

    o << "class Ri1<string n, list<Register> aliases> : TCEReg<n, aliases> {"
      << std::endl;
    o << "}" << std::endl;

    o << "class Ri8<string n, list<Register> aliases> : TCEReg<n, aliases> {"
      << std::endl;
    o << "  let Aliases = aliases;" << std::endl;
    o << "}" << std::endl;

    o << "class Ri16<string n, list<Register> aliases> : TCEReg<n, aliases> {"
      << std::endl;
    o << "}" << std::endl;

    o << "class Ri32<string n, list<Register> aliases> : TCEReg<n, aliases> {"
      << std::endl;
    o << "}" << std::endl;

    o << "class Rf32<string n, list<Register> aliases> : TCEReg<n, aliases> {"
      << std::endl;
    o << "}" << std::endl;

    o << "class Ri64<string n, list<Register> aliases> : TCEReg<n, aliases> {"
      << std::endl;
    o << "}" << std::endl;

    o << "class Rf64<string n, list<Register> aliases> : TCEReg<n, aliases> {"
      << std::endl;
    o << "}" << std::endl;

    o << std::endl;
   
    write64bitRegisterInfo(o);
    write32bitRegisterInfo(o);
    write16bitRegisterInfo(o);
    write8bitRegisterInfo(o);
    write1bitRegisterInfo(o);

    writeRARegisterInfo(o);
    return true;
}


/**
 * Iterates through all registers in the machine and adds register information
 * to the register sets.
 */
void
TDGen::analyzeRegisters() {

    const TTAMachine::Machine::BusNavigator busNav =
        mach_.busNavigator();

    // Check which registers have guards and put their info in
    // guardedRegs_ set.
    for (int i = 0; i < busNav.count(); i++) {
        const TTAMachine::Bus& bus = *busNav.item(i);
        for (int g = 0; g < bus.guardCount(); g++) {
            const TTAMachine::RegisterGuard* rg =
                dynamic_cast<const TTAMachine::RegisterGuard*>(bus.guard(g));

            if (rg != NULL) {
                RegInfo reg = {
                    rg->registerFile()->name(), rg->registerIndex() };

                guardedRegs_.insert(reg);
            }
        }
    }

    const TTAMachine::Machine::RegisterFileNavigator nav =
        mach_.registerFileNavigator();

    bool regsFound = true;
    int currentIdx = 0;
    while (regsFound) {
        regsFound = false;
        for (int i = 0; i < nav.count(); i++) {
            const TTAMachine::RegisterFile* rf = nav.item(i);
            unsigned width = rf->width();
            std::vector<RegInfo>* ri = NULL;
            if (width == 64) ri = &regs64bit_;
            else if (width == 32) ri = &regs32bit_;
            else if (width == 16) ri = &regs16bit_;
            else if (width == 8) ri = &regs8bit_;
            else if (width == 1) ri = &regs1bit_;
            else {
            std::cout << "Warning: ignoring " << width
                      << " bit rf '" << rf->name() << "'." << std::endl;
            }

            int lastIdx = rf->size();
            if (!fullyConnected_) {
                // If the machine is not fully connected,
                // preserve last register
                // of all registers for bypassing values.
                lastIdx--;
            }

            if (currentIdx < lastIdx) {
                RegInfo reg = { rf->name(), currentIdx };
                if (guardedRegs_.find(reg) == guardedRegs_.end()) {
                    // Skip registers with guard.
                    ri->push_back(reg);
                }
                regsFound = true;
            }
        }
        currentIdx++;
    }
}


/**
 * Writes 1-bit register definitions to the output stream.
 */
void
TDGen::write1bitRegisterInfo(std::ostream& o) {

    std::string i1regs;
    if (regs1bit_.size() < 1) {
        RegInfo reg = { "dummy", 0 };
        std::string name = "I1DUMMY";
        writeRegisterDef(o, reg, name, "Ri1", "", RESERVED);
        i1regs += name;
    } else {
        for (unsigned i = 0; i < regs1bit_.size(); i++) {
            std::string regName = "B" + Conversion::toString(i);
            if (i > 0) i1regs += ", ";
            i1regs += regName;
            writeRegisterDef(o, regs1bit_[i], regName, "Ri1", "", GPR);
        }
    }

    o << std::endl
      << "def I1Regs : RegisterClass<\"TCE\", [i1], 8, ["
      << i1regs << "]> {" << std::endl;
    o << " let Size=8;" << std::endl;
    o << " let Alignment=8;" << std::endl;
    o << "}" << std::endl;

}


/**
 * Writes 8-bit register definitions to the output stream.
 */
void
TDGen::write8bitRegisterInfo(std::ostream& o) {

    std::string i8regs;
    if (regs8bit_.size() < 1) {
        RegInfo reg = { "dummy", 0 };
        std::string name = "I8DUMMY";
        writeRegisterDef(o, reg, name, "Ri8", "", RESERVED);
        i8regs += name;
    } else {
        for (unsigned i = 0; i < regs8bit_.size(); i++) {
            std::string regName = "Q" + Conversion::toString(i);
            if (i > 0) i8regs += ", ";
            i8regs += regName;
            writeRegisterDef(o, regs8bit_[i], regName, "Ri8", "", GPR);
        }
    }

    o << std::endl
       << "def I8Regs : RegisterClass<\"TCE\", [i8], 8, ["
       << i8regs << "]>;" << std::endl
       << std::endl << std::endl << std::endl;
}


/**
 * Writes 16-bit register definitions to the output stream.
 */
void
TDGen::write16bitRegisterInfo(std::ostream& o) {

    std::string i16regs;
    if (regs16bit_.size() < 1) {
        RegInfo reg = { "dummy", 0 };
        std::string name = "I16DUMMY";
        writeRegisterDef(o, reg, name, "Ri16", "", RESERVED);
        i16regs += name;
    } else {
        for (unsigned i = 0; i < regs16bit_.size(); i++) {
            std::string regName = "H" + Conversion::toString(i);
            if (i > 0) i16regs += ", ";
            i16regs += regName;
            writeRegisterDef(o, regs16bit_[i], regName, "Ri16", "", GPR);
        }
    }

    o << std::endl
       << "def I16Regs : RegisterClass<\"TCE\", [i16], 16, ["
       << i16regs << "]>;" << std::endl
       << std::endl;
}


/**
 * Writes 32-bit register definitions to the output stream.
 */
void
TDGen::write32bitRegisterInfo(std::ostream& o) {

    // --- Hardcoded reserved registers. ---
    writeRegisterDef(o, regs32bit_[0], "SP", "Ri32", "", RESERVED);
    writeRegisterDef(o, regs32bit_[1], "IRES0", "Ri32", "", RESULT);
    writeRegisterDef(
        o, regs32bit_[2], "KLUDGE_REGISTER", "Ri32", "", RESERVED);

    // -------------------------------------
    
    std::string i32regs;
    for (unsigned i = 3; i < regs32bit_.size(); i++) {
        std::string regName = "I" + Conversion::toString(i);
        i32regs += regName + ", ";
        writeRegisterDef(o, regs32bit_[i], regName, "Ri32", "", GPR);
    }

    o << std::endl
      << "def I32Regs : RegisterClass<\"TCE\", [i32], 32, ["
      << i32regs << std::endl
      << "SP, IRES0, KLUDGE_REGISTER]> {"
      << std::endl;
    
    o << " let MethodProtos = [{" << std::endl
      << "  iterator allocation_order_end(const MachineFunction& mf) const;"
      << std::endl
      << " }];" << std::endl
      << " let MethodBodies = [{" << std::endl
      << "  I32RegsClass::iterator" << std::endl
      << "  I32RegsClass::allocation_order_end("
      << "   const MachineFunction& mf) const {" << std::endl
      << "   return end()-3; // Don't allocate special regs." << std::endl
      << "  }" << std::endl
      << " }];" << std::endl
      << "}" << std::endl;
    
    // --- Hardcoded reserved registers. ---
    writeRegisterDef(o, regs32bit_[0], "FSP", "Rf32", "SP", RESERVED);
    writeRegisterDef(o, regs32bit_[1], "FRES0", "Rf32", "IRES0", RESULT);
    writeRegisterDef(o, regs32bit_[2], "FKLUDGE", "Rf32", "KLUDGE_REGISTER",
                     ARGUMENT);
    // -------------------------------------
    std::string f32regs;
    for (unsigned i = 3; i < regs32bit_.size(); i++) {
        std::string regName = "F" + Conversion::toString(i);
        std::string aliasName = "I" + Conversion::toString(i);
        f32regs += regName + ", ";
        writeRegisterDef(o, regs32bit_[i], regName, "Rf32", aliasName, GPR);
    }

    o << std::endl
      << "def F32Regs : RegisterClass<\"TCE\", [f32], 32, ["
      << f32regs << std::endl
      << "FSP, FRES0, FKLUDGE]> {" << std::endl;
    
    o << " let MethodProtos = [{" << std::endl
      << "  iterator allocation_order_end(const MachineFunction& mf) const;"
      << std::endl
      << " }];" << std::endl
      << " let MethodBodies = [{" << std::endl
      << "  F32RegsClass::iterator" << std::endl
      << "  F32RegsClass::allocation_order_end("
      << "  const MachineFunction& mf) const {" << std::endl
      << "   return end()-3; // Don't allocate special registers." << std::endl
      << "  }"<< std::endl
      << " }];" << std::endl
      << "}" << std::endl;
}


/**
 * Writes 64-bit register definitions to the output stream.
 */
void
TDGen::write64bitRegisterInfo(std::ostream& o) {

    // --- Hardcoded reserved registers. ---
    std::string i64regs;
    if (regs64bit_.size() < 1) {
        RegInfo reg = { "dummy", 0 };
        writeRegisterDef(o, reg, "DIRES0", "Ri64", "", RESERVED);
        i64regs = "DIRES0";
    } else {
        writeRegisterDef(o, regs64bit_[0], "DIRES0", "Ri64", "", RESERVED);
        for (unsigned i = 1; i < regs64bit_.size(); i++) {
            std::string regName = "DI" + Conversion::toString(i);
            i64regs += regName;
            i64regs += ", ";
            writeRegisterDef(o, regs64bit_[i], regName, "Ri64", "", GPR);
        }
        i64regs += "DIRES0";
    }

    o << std::endl
      << "def I64Regs : RegisterClass<\"TCE\", [i64], 32, [" // DIRES
      << i64regs << "]> {"
      << std::endl;

    o << " let MethodProtos = [{" << std::endl
      << "  iterator allocation_order_end(const MachineFunction& mf) const;"
      << std::endl
      << " }];" << std::endl
      << " let MethodBodies = [{" << std::endl
      << "  I64RegsClass::iterator" << std::endl
      << "  I64RegsClass::allocation_order_end("
      << "  const MachineFunction& mf) const {" << std::endl
      << "   return end()-1; // Don't allocate special registers." << std::endl
      << "  }"<< std::endl
      << " }];" << std::endl
      << "}" << std::endl;

    std::string f64regs;
    if (regs64bit_.size() < 1) {
        RegInfo reg = { "dummy", 0 };
        writeRegisterDef(o, reg, "DRES0", "Rf64", "", RESERVED);
        f64regs = "DRES0";
    } else {
        writeRegisterDef(o, regs64bit_[0], "DRES0", "Ri64", "DIRES0", RESERVED);
        for (unsigned i = 1; i < regs64bit_.size(); i++) {
            std::string regName = "D" + Conversion::toString(i);
            std::string aliasName = "DI" + Conversion::toString(i);
            f64regs += regName;
            f64regs += ", ";
            writeRegisterDef(o, regs64bit_[i], regName, "Rf64", aliasName, GPR);
        }
        f64regs += "DRES0";
    }
    o << std::endl
      << "def F64Regs : RegisterClass<\"TCE\", [f64], 32, [" // DRES
      << f64regs << "]>{" << std::endl;

    o << " let MethodProtos = [{" << std::endl
      << "  iterator allocation_order_end(const MachineFunction& mf) const;"
      << std::endl
      << " }];" << std::endl
      << " let MethodBodies = [{" << std::endl
      << "  F64RegsClass::iterator" << std::endl
      << "  F64RegsClass::allocation_order_end("
      << "  const MachineFunction& mf) const {" << std::endl
      << "   return end()-1; // Don't allocate special registers." << std::endl
      << "  }"<< std::endl
      << " }];" << std::endl
      << "}" << std::endl;
}



/**
 * Writes return address register definition to the output stream.
 */
void
TDGen::writeRARegisterInfo(std::ostream& o) {
    o << "class Rra<string n> : TCEReg<n, []>;" << std::endl;
    o << "def RA : Rra<\"return-address\">, DwarfRegNum<513>;" << std::endl;
    o << "def RAReg : RegisterClass<\"TCE\", [i32], 32, [RA]>;" << std::endl;
}


/**
 * Checks that the target machine has required registers to build a usable
 * plugin.
 *
 * @return True if required registers were found, false if not.
 */
bool
TDGen::checkRequiredRegisters() {

    if (regs32bit_.size() < REQUIRED_I32_REGS) {
        std::cout << "Architecture doesn't meet the minimal requirements."
                  << std::endl;

        std::cout << regs32bit_.size()
                  << " 32 bit registers found." << std::endl;

        std::cout << "At least " << REQUIRED_I32_REGS
                  << " 32bit registers are needed for integers."
                  << std::endl;

        return false;
    }

    return true;
}


/**
 * Writes instruction .td definitions to the outputstream.
 */
void
TDGen::writeInstrInfo(std::ostream& os) {

    std::set<std::string> requiredOps;
    std::set<std::string> opNames;

    requiredOps.insert("ADD");
    requiredOps.insert("SUB");
    requiredOps.insert("MUL");
    requiredOps.insert("DIV");
    requiredOps.insert("DIVU");
    requiredOps.insert("DIV");
    requiredOps.insert("MOD");
    requiredOps.insert("MODU");

    requiredOps.insert("LDW");
    requiredOps.insert("LDH");
    requiredOps.insert("LDHU");
    requiredOps.insert("LDQ");
    requiredOps.insert("LDQU");
    requiredOps.insert("STW");
    requiredOps.insert("STH");
    requiredOps.insert("STQ");

    requiredOps.insert("SXHW");
    requiredOps.insert("SXQW");

    requiredOps.insert("AND");
    requiredOps.insert("XOR");
    requiredOps.insert("IOR");

    requiredOps.insert("SHL");
    requiredOps.insert("SHR");
    requiredOps.insert("SHRU");

    requiredOps.insert("EQ");
    requiredOps.insert("NE");
    requiredOps.insert("LT");
    requiredOps.insert("LTU");
    requiredOps.insert("LE");
    requiredOps.insert("LEU");
    requiredOps.insert("GT");
    requiredOps.insert("GTU");
    requiredOps.insert("GE");
    requiredOps.insert("GEU");


    const TTAMachine::Machine::FunctionUnitNavigator fuNav =
        mach_.functionUnitNavigator();

    OperationPool opPool;

    for (int i = 0; i < fuNav.count(); i++) {
        const TTAMachine::FunctionUnit* fu = fuNav.item(i);
        for (int o = 0; o < fu->operationCount(); o++) {
            const std::string opName = fu->operation(o)->name();
            opNames.insert(StringTools::stringToUpper(opName));
        }
    }

    std::set<std::string>::const_iterator iter = opNames.begin();
    for (; iter != opNames.end(); iter++) {
        std::set<std::string>::iterator r = requiredOps.find(*iter);
        if (r != requiredOps.end()) {
            requiredOps.erase(r);
        }
        Operation& op = opPool.operation(*iter);
        if (&op == &NullOperation::instance()) {
            // Unknown operation: skip
            continue;
        }
        writeOperationDef(os, op);
    }
    

    writeCallDef(os);

    os << std::endl;

    // Emulated operations,

    iter = requiredOps.begin();
    for (; iter != requiredOps.end(); iter++) {

        const Operation& op = opPool.operation(*iter);       

        if (&op == &NullOperation::instance()) {
            std::cerr << "Required OP not found: " << *iter << std::endl;
            assert(false);
        }

        const OperationDAGSelector::OperationDAGList emulationDAGs =
            OperationDAGSelector::findDags(op.name(), opNames);

        if (emulationDAGs.empty()) {
            std::cerr << "WARNING: Operation '" << *iter << "' not supported."
                      << std::endl;
        } else {
            writeEmulationPattern(os, op, emulationDAGs.smallestNodeCount());
        }
    }

    // TODO: Handle missing operations.
    /*
    iter = requiredOps.begin();
    for (; iter != requiredOps.end(); iter++) {
        Operation& op = opPool.operation(*iter);
        writeOperationDef(os, op);
    }
    */
}


/**
 * Writes .td pattern for the call instruction(s) to the output stream.
 */
void
TDGen::writeCallDef(std::ostream& o) {
    o << "let Uses = [";
    for (unsigned i = 0; i < argRegNames_.size(); i++) {
        if (i > 0) o << ", ";
        o << argRegNames_[i];
    }    
    o << "],";
    o << "hasDelaySlot = 1, isCall = 1,";
    o << "Defs = [";
    for (unsigned i = 0; i < resRegNames_.size(); i++) {
        if (i > 0) o << ", ";
        o << resRegNames_[i];
    }    
    for (unsigned i = 0; i < gprRegNames_.size(); i++) {
        o << ", " << gprRegNames_[i];
    }
    o << "] in {" << std::endl;
    o << "def CALL : InstTCE<\"call\", (outs), (ins calltarget:$dst),";
    o << "\"$dst -> call.1;\", []>;" << std::endl;

    o << "def CALL_MEMrr : InstTCE<";
    o << "\"call\", (outs), (ins MEMrr:$ptr),";
    o << "\"$ptr -> call.1;\", [(call ADDRrr:$ptr)]>;" << std::endl;
    
    o << "def CALL_MEMri : InstTCE<";
    o << "\"call\", (outs), (ins MEMri:$ptr),";
    o << "\"$ptr -> call.1;\", [(call ADDRri:$ptr)]>;" << std::endl;
    o << "}" << std::endl;

    o << "def : Pat<(call tglobaladdr:$dst), (CALL tglobaladdr:$dst)>;"
      << std::endl;

    o << "def : Pat<(call texternalsym:$dst), (CALL texternalsym:$dst)>;"
      << std::endl;

}

/**
 * Generates required function definitions for the backend plugin.
 *
 * @param o Output stream to write the c++ code to.
 */
void
TDGen::writeBackendCode(std::ostream& o) {

    // --- rfName() ---
    o << "std::string" << std::endl
      << "GeneratedTCEPlugin::rfName(unsigned dwarfRegNum) {" << std::endl;
    std::map<std::string, RegInfo>::const_iterator iter =
        regs_.begin();

    for (; iter != regs_.end(); iter++) {
        o << "  if (dwarfRegNum == TCE::" << (*iter).first
          << ") return \"" << (*iter).second.rf
          << "\";" << std::endl;
    }
    o << "  assert (false);" << std::endl
      << "  return \"\";" << std::endl
      << "}" << std::endl;


    // --- registerIndex() ---
    o << "unsigned" << std::endl
      << "GeneratedTCEPlugin::registerIndex(unsigned dwarfRegNum) {"
      << std::endl;

    iter = regs_.begin();
    for (; iter != regs_.end(); iter++) {
        o << "  if (dwarfRegNum == TCE::" << (*iter).first
          << ") return " << (*iter).second.idx
          << ";" << std::endl;
    }
    o << "  assert(false);" << std::endl
      << "  return 0;" << std::endl
      << "}" << std::endl;

    // --- createMachine() ---
    std::string adfXML;
    ADFSerializer serializer;
    serializer.setDestinationString(adfXML);
    serializer.writeMachine(mach_);
    o << "const std::string" << std::endl
      << "GeneratedTCEPlugin::adfXML_ =" << std::endl << "\"";
    for (unsigned int i = 0; i < adfXML.length(); i++) {
        if (adfXML[i] == '"') o << "\\\"";
        else if (adfXML[i] == '\n') o << "\"\n\"";
        else o << adfXML[i];
    }
    o << "\";" << std::endl;

    // --- dataASName() ---

#if 0
    const TTAMachine::Machine::AddressSpaceNavigator& nav =
        mach_.addressSpaceNavigator();

    std::string asName = "";
    for (int i = 0; i < nav.count(); i++) {
        if (mach_.controlUnit() == NULL ||
            nav.item(i) != mach_.controlUnit()->addressSpace()) {

            asName = nav.item(i)->name();
            break;
        }
    }
#endif
    const TTAMachine::Machine::FunctionUnitNavigator& nav =
        mach_.functionUnitNavigator();
    std::string asName = "";
    for (int i = 0; i < nav.count(); i++) {
        if (nav.item(i) != mach_.controlUnit() &&
            nav.item(i)->addressSpace() != NULL) {
            asName = nav.item(i)->addressSpace()->name();
        }
    }

    if (asName == "") {
        std::cerr << "ERROR: Couldn't determine data address space."
                  << std::endl;

        assert(false);
    }
    o << "std::string" << std::endl
      << "GeneratedTCEPlugin::dataASName() {" << std::endl;
    o << "  return \"" << asName << "\";" << std::endl;
    o << "}" << std::endl;

    // --- raPortDRegNum() ---
    o << "unsigned GeneratedTCEPlugin::raPortDRegNum() {" << std::endl;
    o << "  return TCE::RA;" << std::endl;
    o << "}" << std::endl;
}

/**
 * Writes a top-level .td file which includes generated .td definitions
 * to single file.
 *
 * @param o Output stream to write the .td to.
 */
void
TDGen::writeTopLevelTD(std::ostream& o) {
   o << "include \"Target.td\"" << std::endl;
   o << "include \"GenRegisterInfo.td\"" << std::endl;
   o << "include \"TCEInstrInfo.td\"" << std::endl;
   o << "def TCEInstrInfo : InstrInfo { }" << std::endl;
   o << "def TCE : Target { let InstructionSet = TCEInstrInfo; }" << std::endl;
}


/**
 * Writes operation definition in .td format to an output stream.
 *
 * @param o Output stream to write the definition to.
 * @param op Operation to write definition for.
 */
void
TDGen::writeOperationDef(
    std::ostream& o,
    Operation& op) {

    if (op.numberOfOutputs() > 1) {
        std::cerr << "WARNING: skipping operation '" << op.name()
                  << "' more than 1 output operands not yet supported."
                  << std::endl;

        return;
    }

    std::string attrs;
    if (op.readsMemory()) attrs += " isLoad = 1";
    if (op.writesMemory()) attrs += " isStore = 1";



    std::string suffix(op.numberOfInputs(), 'r');

    for (int immInput = 0;
         immInput < (op.numberOfInputs() + 1); immInput++) {

        std::string outputs, inputs, asmstr, pattern, patSuffix;
        try {
            outputs = "(outs" + patOutputs(op) + ")";
            inputs = "(ins " + patInputs(op, immInput) + ")";
            asmstr = "\"\"";
            patSuffix = suffix;
            if (immInput > 0) {
                patSuffix[immInput - 1] = 'i';
            }

            if (llvmOperationPattern(op.name()) != "" || op.dagCount() == 0) {
                OperationDAG* trivial = createTrivialDAG(op);
                pattern = operationPattern(op, *trivial, immInput);
                delete trivial;
            } else {
                pattern = operationPattern(op, op.dag(0), immInput);
            }
        } catch (InvalidData& e) {
            //std::cerr << "ERROR: " << e.errorMessage() << std::endl;
            continue;
        }

        if (attrs != "") {
            o << "let" << attrs << " in { " << std::endl;
        }

        o << "def " << StringTools::stringToUpper(op.name())
          << patSuffix << " : "
          << "InstTCE<\""<< op.name() << "\", "
          << outputs << ", "
          << inputs << ", "
          << asmstr << ", "
          << "[" << pattern << "]>;"
          << std::endl;

        if (attrs != "") {
            o << "}" << std::endl;
        }        
    }
}


/**
 * Writes operation emulation pattern in .td format to an output stream.
 *
 * @param o Output stream to write the definition to.
 * @param op Emulated operation.
 * @param dag Emulation pattern.
 */
void
TDGen::writeEmulationPattern(
    std::ostream& o,
    const Operation& op,
    const OperationDAG& dag) {

    std::string llvmPat = llvmOperationPattern(op.name());
    if (llvmPat == "") {
        assert(false && "Unknown operation to emulate.");
    }

    const OperationDAGNode* res = *(dag.endNodes().begin());
    const OperationNode* opNode = dynamic_cast<const OperationNode*>(res);
    if (opNode == NULL) {
        assert(dag.inDegree(*res) ==  1);
        const OperationDAGEdge& edge = dag.inEdge(*res, 0);
        res = dynamic_cast<OperationNode*>(&dag.tailNode(edge));
        assert(res != NULL);
    }

    boost::format match(llvmPat);
    for (int i = 0; i < op.numberOfInputs(); i++) {
        match % operandToString(op.operand(i + 1), true, 0);
    }

    o << "def : Pat<(" << match.str() << "), "
      << dagNodeToString(op, dag, *res, 0, true)
      << ">;" << std::endl;
}


/**
 * Returns llvm operation node .td format string as a boost::format string.
 *
 * Boost format parameters correspond to the operand strings.
 *
 * @param osalOperationName Base-operation name in OSAL.
 * @return Boost::format string of the operation node in llvm.
 */
std::string
TDGen::llvmOperationPattern(const std::string& osalOperationName) {

    const std::string opName = StringTools::stringToLower(osalOperationName);

    if (opName == "add") return "add %1%, %2%";
    if (opName == "sub") return "sub %1%, %2%";
    if (opName == "mul") return "mul %1%, %2%";
    if (opName == "div") return "sdiv %1%, %2%";
    if (opName == "divu") return "udiv %1%, %2%";
    if (opName == "mod") return "srem %1%, %2%";
    if (opName == "modu") return "urem %1%, %2%";

    if (opName == "shl") return "shl %1%, %2%";
    if (opName == "shr") return "sra %1%, %2%";
    if (opName == "shru") return "srl %1%, %2%";
    if (opName == "rotl") return "rotl %1%, %2%";
    if (opName == "rotr") return "rotr %1%, %2%";

    if (opName == "and") return "and %1%, %2%";
    if (opName == "ior") return "or %1%, %2%";
    if (opName == "xor") return "xor %1%, %2%";

    if (opName == "eq") return "seteq %1%, %2%";
    if (opName == "ne") return "setne %1%, %2%";
    if (opName == "lt") return "setlt %1%, %2%";
    if (opName == "le") return "setle %1%, %2%";
    if (opName == "gt") return "setgt %1%, %2%";
    if (opName == "ge") return "setge %1%, %2%";
    if (opName == "ltu") return "setult %1%, %2%";
    if (opName == "leu") return "setule %1%, %2%";
    if (opName == "gtu") return "setugt %1%, %2%";
    if (opName == "geu") return "setuge %1%, %2%";

    if (opName == "eqf") return "seteq %1%, %2%";
    if (opName == "nef") return "setne %1%, %2%";
    if (opName == "ltf") return "setlt %1%, %2%";
    if (opName == "lef") return "setle %1%, %2%";
    if (opName == "gtf") return "setgt %1%, %2%";
    if (opName == "gef") return "setge %1%, %2%";

    if (opName == "addf") return "fadd %1%, %2%";
    if (opName == "subf") return "fsub %1%, %2%";
    if (opName == "mulf") return "fmul %1%, %2%";
    if (opName == "divf") return "fdiv %1%, %2%";
    if (opName == "absf") return "fabs %1%";
    if (opName == "negf") return "fneg %1%";

    if (opName == "cif") return "sint_to_fp %1%";
    if (opName == "cfi") return "fp_to_sint %1%";

    if (opName == "ldq") return "sextloadi8 %1%";
    if (opName == "ldqu") return "zextloadi8 %1%";
    if (opName == "ldh") return "sextloadi16 %1%";
    if (opName == "ldhu") return "zextloadi16 %1%";
    if (opName == "ldw") return "load %1%";
    //if (opName == "ldd") return "load";

    if (opName == "stq") return "truncstorei8 %2%, %1%";
    if (opName == "sth") return "truncstorei16 %2%, %1%";
    if (opName == "stw") return "store %2%, %1%";
    //if (opName == "std") return "load";

    if (opName == "sxhw") return "sext_inreg %1%, i16";
    if (opName == "sxqw") return "sext_inreg %1%, i8";

    if (opName == "neg") return "ineg %1%";
    if (opName == "not") return "not %1%";

    // Unknown operation name.
    return "";
}

/**
 * Returns operation pattern in llvm .td format.
 *
 * @param op Operation to return pattern for.
 * @param dag Operation pattern's DAG.
 * @param  immOp Index of an operand to define as an immediate operand,
 *                or 0, if all operands should be in registers.
 *
 * @return Pattern string.
 */
std::string
TDGen::operationPattern(
    const Operation& op,
    const OperationDAG& dag,
    int immOp) {

    const OperationDAGNode& res = **(dag.endNodes().begin());
    return dagNodeToString(op, dag, res, immOp, false);
}

/**
 * Converts single OperationDAG node to llvm pattern fragment string.
 *
 * @param op Operation that the whole DAG is for.
 * @param dag Whole operation DAG.
 * @param node DAG node to return string for.
 * @param immOp Index of an operand to define as an immediate or 0 if none.
 * @param emulationPattern True, if the returned string should be in
 *                         emulation pattern format.
 * @return DAG node as a llvm .td string.
 */
std::string
TDGen::dagNodeToString(
    const Operation& op,
    const OperationDAG& dag,
    const OperationDAGNode& node,
    int immOp,
    bool emulationPattern) throw (InvalidData) {

    const OperationNode* oNode = dynamic_cast<const OperationNode*>(&node);
    if (oNode != NULL) {
        assert( dag.inDegree(*oNode) ==
                oNode->referencedOperation().numberOfInputs());

        return operationNodeToString(op, dag, *oNode, immOp, emulationPattern);
    }

    const TerminalNode* tNode = dynamic_cast<const TerminalNode*>(&node);
    if (tNode != NULL) {
        const Operand& operand = op.operand(tNode->operandIndex());
        bool imm = (operand.index() == immOp);
        if (dag.inDegree(*tNode) == 0) {
            // Input operand for the whole operation.
            assert(operand.isInput());

            if (imm && !canBeImmediate(dag, *tNode)) {
                std::string msg = "Invalid immediate operand.";
                throw InvalidData(__FILE__, __LINE__, __func__, msg);
            }

            return operandToString(operand, false, imm);
        } else {

            // Output operand for the whole operation.
            assert(dag.inDegree(*tNode) == 1);
            assert(op.operand(tNode->operandIndex()).isOutput());
            assert(operand.isOutput());

            
            const OperationDAGEdge& edge = dag.inEdge(node, 0);
            const OperationDAGNode& srcNode = dag.tailNode(edge);

            // Multiple-output operation nodes not supported in the middle
            // of dag:
            assert(dag.outDegree(srcNode) == 1);

            std::string pattern =
                "(set " + operandToString(operand, false, imm) + ", " +
                dagNodeToString(op, dag, srcNode, immOp, emulationPattern) +
                ")";
            
            return pattern;
        }
    }

    assert(false && "Unknown OperationDAG node type.");
}

/**
 * Converts OSAL dag operation node to llvm .td pattern fragment string.
 *
 * @param op Operation which this operation node is part of.
 * @param dag Parent DAG of the operation node.
 * @param node Node to convert to string.
 * @param immOp Index of an operand to define as immediate or 0 if none.
 * @param emulationPattern True, if the string should be in emulation pattern
 *                         format.
 */
std::string
TDGen::operationNodeToString(
    const Operation& op,
    const OperationDAG& dag,
    const OperationNode& node,
    int immOp,
    bool emulationPattern) throw (InvalidData) {

    const Operation& operation = node.referencedOperation();

    std::string operationPat;

    if (emulationPattern) {
        // FIXME: UGLYhhh
        operationPat = StringTools::stringToUpper(operation.name()) +
            string(operation.numberOfInputs(), 'r');

        for (int i = 0; i < operation.numberOfInputs(); i++) {
            if (i > 0) {
                operationPat += ", ";
            } else {
                operationPat += " ";
            }
            operationPat =
                operationPat + "%" + Conversion::toString(i + 1) + "%";
        }
    } else {
        operationPat= llvmOperationPattern(operation.name());
    }

    if (operationPat == "") {
        std::string msg = "Unknown operation node in dag: " + operation.name();
        throw (InvalidData(__FILE__, __LINE__, __func__, msg));
    }

    boost::format pattern(operationPat);

    int inputs = operation.numberOfInputs();
    int outputs = operation.numberOfOutputs();

    assert(outputs == 0 || outputs == 1);

    for (int i = 1; i < inputs + 1; i++) {
        bool ok = false;
        for (int e = 0; e < dag.inDegree(node); e++) {
            const OperationDAGEdge& edge = dag.inEdge(node, e);
            int dst = edge.dstOperand();
            if (dst == i) {
                ok = true;
                const OperationDAGNode& in = dag.tailNode(edge);
                pattern % dagNodeToString(
                    op, dag, in, immOp, emulationPattern);
            }
        }
    }
    return std::string("(") + pattern.str() + ")";
}

/**
 * Returns llvm .td format string for an OSAL operand.
 *
 * @param operand Operand to return string for.
 * @param match True, if the string should be in the matching pattern format.
 * @param immediate True, if the operand should be defined as an immediate.
 * @return Operand string to be used in a llvm .td pattern.
 */
std::string
TDGen::operandToString(
    const Operand& operand,
    bool match,
    bool immediate) {

    int idx = operand.index();

    if (operand.isAddress()) {
        if (immediate) {
            if (match) {
                return "MEMri:$op" + Conversion::toString(idx);
            } else {
                return "ADDRri:$op" + Conversion::toString(idx);
            }
        } else {
            if (match) {
                return  "MEMrr:$op" + Conversion::toString(idx);
            } else {
                return  "ADDRrr:$op" + Conversion::toString(idx);
            }
        }
    } else if (operand.type() == Operand::SINT_WORD ||
               operand.type() == Operand::UINT_WORD) {

        if (immediate) {
            if (match) {
                return "i32imm:$op" + Conversion::toString(idx);
            } else {
                return "imm:$op" + Conversion::toString(idx);
            }
        } else {
            return "I32Regs:$op" + Conversion::toString(idx);
        }
    } else if (operand.type() == Operand::FLOAT_WORD) {
        if (immediate) {
            // f32 immediates not implemented
            std::string msg = "f32 immediate operand not supported";
            throw (InvalidData(__FILE__, __LINE__, __func__, msg));
        } else {
            return "F32Regs:$op" + Conversion::toString(idx);
        }
    } else if (operand.type() == Operand::DOUBLE_WORD) {
        if (immediate) {
            // f64 immediates not implemetned
            std::string msg = "f64 immediate operand not supported";
            throw (InvalidData(__FILE__, __LINE__, __func__, msg));
        } else {
            return "F64Regs:$op" + Conversion::toString(idx);
        }
    } else {
        assert(false && "Unknown operand type.");
    }

}

/**
 * Returns llvm input definition list for an operation.
 *
 * @param op Operation to define inputs for.
 * @param immOp Index for an operand that should be defined as an immediate.
 * @return String defining operation inputs in llvm .td format.
 */
std::string
TDGen::patInputs(const Operation& op, int immOp) {
    std::string ins;
    for (int i = 0; i < op.numberOfInputs(); i++) {
        if (i > 0) {
            ins += ",";
        }
        bool imm = (op.operand(i+1).index() == immOp);
        ins += operandToString(op.operand(i + 1), true, imm);
    }
    return ins;
}


/**
 * Returns llvm output definition list for an operation.
 *
 * @param op Operation to define outputs for.
 * @return String defining operation outputs in llvm .td format.
 */
std::string
TDGen::patOutputs(const Operation& op) {
    std::string outs;
    if (op.numberOfOutputs() == 0) {
        return "";
    } else if (op.numberOfOutputs() == 1) {
        assert(op.operand(op.numberOfInputs() + 1).isOutput());
        outs += " ";
        outs += operandToString(
            op.operand(op.numberOfInputs() + 1), true, false);

    } else {
        assert(false);
    }
    return outs;
}

/**
 * Creates a dummy dag for an OSAL operation.
 *
 * @param op Operation to create OperationDAG for.
 * @return OperationDAG consisting of only one operation node referencing
 *         the given operation.
 */
OperationDAG*
TDGen::createTrivialDAG(Operation& op) {

    OperationDAG* dag = new OperationDAG(op.name());
    OperationNode* opNode = new OperationNode(op);
    dag->addNode(*opNode);

    for (int i = 0; i < op.numberOfInputs() + op.numberOfOutputs(); i++) {
        const Operand& operand = op.operand(i + 1);
        TerminalNode* t = new TerminalNode(operand.index());
        dag->addNode(*t);
        if (operand.isInput()) {
            OperationDAGEdge* e = new OperationDAGEdge(1, operand.index());
            dag->connectNodes(*t, *opNode, *e);
        } else {
            OperationDAGEdge* e = new OperationDAGEdge(operand.index(), 1);
            dag->connectNodes(*opNode, *t, *e);
        }
    }
    return dag;
}


/**
 * Returns true if the operand corresponding to the given TerminalNode in
 * an OperationDAG can be immediate in the llvm pattern.
 *
 * @param dag DAG of the whole operation.
 * @param node TerminalNode corresponding to the operand queried.
 */
bool
TDGen::canBeImmediate(
    const OperationDAG& dag, const TerminalNode& node) {

    if (dag.inDegree(node) != 0) {
        return false;
    }

    for (int i = 0; i < dag.outDegree(node); i++) {

        const OperationDAGEdge& edge = dag.outEdge(node, i);
        const OperationDAGNode& dstNode = dag.headNode(edge);
        const OperationNode* opNode =
            dynamic_cast<const OperationNode*>(&dstNode);

        const Operation& operation = opNode->referencedOperation();

        if (opNode == NULL) {
            return false;
        }

        if (!operation.operand(edge.dstOperand()).isAddress() &&
            operation.numberOfInputs() != 2) {

            // Only binops and addresses can have immediate operands for now.
            return false;
        }

        if (edge.dstOperand() == 1 &&
            operation.numberOfInputs() == 2 &&
            operation.canSwap(1, 2)) {

            return false;
        }

        // No stores with immediate value to store.
        if (edge.dstOperand() == 2 && operation.writesMemory()) {
            return false;
        }

        // FIXME: shifts and rotates require 8-bit immediate for shift amount
        // operand.
        if (edge.dstOperand() == 2 &&
            (StringTools::stringToLower(operation.name()) == "shr" ||
             StringTools::stringToLower(operation.name()) == "shru" ||
             StringTools::stringToLower(operation.name()) == "shl" ||
             StringTools::stringToLower(operation.name()) == "rotl" ||
             StringTools::stringToLower(operation.name()) == "rotr")) {

            return false;
        }
    }
    return true;
}
