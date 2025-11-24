CXXFLAGS := -g -Wall -std=c++0x -lm
#CXXFLAGS := -g -Wall -lm
CXX=g++
SRC=procsim.cpp procsim_driver.cpp
PROCSIM=./procsim
R=2
J=3
K=2
L=1
F=4

build:
	$(CXX) $(CXXFLAGS) $(SRC) -o procsim

run:
	$(PROCSIM) -r$R -f$F -j$J -k$K -l$L < traces/hmmer.100k.trace 

clean:
	rm -f procsim *.o

# ./procsim -r 2 -f 4 -j 3 -k 2 -l 1 < traces/gcc_test.trace > my_gcc_test.output
# diff my_gcc_test.output output1.1/gcc_test.output
test:
	$(PROCSIM) -r$(R) -f$(F) -j$(J) -k$(K) -l$(L) < traces/gcc.100k.trace  > my_gcc_100k.output
	diff -u my_gcc_100k.output output1.1/gcc.output | head -n 20