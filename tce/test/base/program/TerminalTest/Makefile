DIST_OBJECTS = Terminal.o TerminalImmediate.o TerminalRegister.o \
	TerminalFUPort.o TerminalAddress.o \
	AnnotatedInstructionElement.o ProgramAnnotation.o 

MACH_OBJECTS = *.o
TOOL_OBJECTS = *.o
OSAL_OBJECTS = *.o
APPLIBS_MACH_OBJS = ResourceVector.o ResourceVectorSet.o

TOP_SRCDIR = ../../../..

EXTRA_LINKER_FLAGS = ${XERCES_LDFLAGS} ${BOOST_LDFLAGS} ${SQLITE_LD_FLAGS} \
                     -ldl
include ${TOP_SRCDIR}/test/Makefile_configure_settings
EXTRA_COMPILER_FLAGS=${LLVM_CPPFLAGS}
include ${TOP_SRCDIR}/test/Makefile_test.defs
