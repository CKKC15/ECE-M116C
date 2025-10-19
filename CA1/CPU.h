#include <iostream>
#include <bitset>
#include <stdio.h>
#include<stdlib.h>
#include <string>
#include <sstream>
using namespace std;


class Instruction { 
public:
	bitset<32> instr;//instruction
 	Instruction() : instr(0) {} // default constructor
	Instruction(bitset<32> bits) : instr(bits) {}
	bitset<7> getOpCode();
	bitset<3> getfunc3();
	bitset<7> getfunc7();
	int extractBits(int start, int length);
};

class Controller {
public:
	int regwrite, alusrc, branch, memre, memwr, memtoreg, aluop;
	Controller() = default;
	void setController(bitset<7> opcode);	
};

class ALU_Control { // must check funct3/func7 and generate 4bit ALUoperation
public:
	int fourbitout;
	ALU_Control() = default;
	void setALUControl(int aluop, Instruction myInst);
};

class ALU {
public:
	int zero, alu_res;
	void executeALU(int fourbit, int rs1, int rs2orimm, bool lui);
};

class CPU {
private:
	int dmemory[4096]; //data memory byte addressable in little endian fashion;
	unsigned long PC; //pc 

public:
	CPU();
	int rs1, rs2, rd, imm;
	Controller cpu_control;
	ALU_Control alu_control;
	
	unsigned long readPC();
	void incPC();
	Instruction fetchInstruction(char* instMem); // takes PC, instMem and returns Instruction object
	void updateValuesInstructionDecode(Instruction myInst);
};

// add other functions and objects here
