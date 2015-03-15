SRC=$(wildcard *.cpp)
OBJ=$(patsubst %.cpp,%.o,$(SRC))
CXXFLAGS+=-I. -I.. -I../DAG -I/Users/huangqiwei/workspace/cppunit/include -I/usr/local/include
CXXFLAGS+=--std=c++11 -g -O2
LDLIBS=-L/Users/huangqiwei/workspace/cppunit/lib -lcppunit 
OBJDEP=../Node.o ../DAG/DAG.o

all: runtest

runtest: $(OBJ) $(OBJDEP)
	$(CXX) $^ $(LDLIBS) -o $@
	
clean:
	rm -rf $(wildcard *.o) $(OBJDEP) ./runtest
# DO NOT DELETE

TestCountable.o: TestCountable.hpp ../Countable.hpp
TestSingleton.o: TestSingleton.hpp ../Singleton.hpp
TestTransaction.o: TestTransaction.hpp ../Transaction.hpp
TestFactory.o: TestFactory.hpp ../Factory.hpp
TestNode.o: TestNode.hpp ../NodeVisitor.hpp ../DataWrapper.hpp ../LeafNode.hpp ../InnerNode.hpp ../Node.o
TestHqwTestCase.o: TestHqwTestCase.hpp ../TestCase.hpp ../TestCaseManager.hpp ../AutoRegister.hpp 
TestDAG.o: TestDAG.hpp ../DAG/DAG.h
TestDAGDemoTable.o: $(wildcard ../DAG/*.hpp)
TestUnionFind.o: TestUnionFind.hpp ../union-find.hpp
TestThreeSumZero.o: TestThreeSumZero.hpp ../ThreeSumZero.hpp
TestSort.o: TestSort.hpp ../sort.hpp
