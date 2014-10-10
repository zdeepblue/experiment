SRC=$(wildcard *.cpp)
OBJ=$(patsubst %.cpp,%.o,$(SRC))
CXXFLAGS=-I. -I.. -I../DAG
LDLIBS=-lcppunit
OBJDEP=../Node.o ../DAG/DAG.o
CC=g++

runtest: $(OBJ) $(OBJDEP)
	$(CC) $^ $(LDLIBS) -o $@
	
# DO NOT DELETE

TestCountable.o: TestCountable.hpp ../Countable.hpp
TestSingleton.o: TestSingleton.hpp ../Singleton.hpp
TestTransaction.o: TestTransaction.hpp ../Transaction.hpp
TestFactory.o: TestFactory.hpp ../Factory.hpp
TestNode.o: TestNode.hpp ../NodeVisitor.hpp ../DataWrapper.hpp ../LeafNode.hpp ../InnerNode.hpp ../Node.o
TestHqwTestCase.o: TestHqwTestCase.hpp ../TestCase.hpp ../TestCaseManager.hpp ../AutoRegister.hpp 
TestDAG.o: TestDAG.hpp ../DAG/DAG.h
TestDAGDemoTable.o: $(wildcard ../DAG/*.hpp)

