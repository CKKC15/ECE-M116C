#include "CPU.h"

#include <iostream>
#include <bitset>
#include <stdio.h>
#include<stdlib.h>
#include <string>
#include<fstream>
#include <sstream>
using namespace std;

/*
Add all the required standard and developed libraries here
*/

/*
Put/Define any helper function/definitions you need here
*/
int main(int argc, char* argv[])
{
	/* This is the front end of your project.
	You need to first read the instructions that are stored in a file and load them into an instruction memory.
	*/

	/* Each cell should store 1 byte. You can define the memory either dynamically, or define it as a fixed size with size 4KB (i.e., 4096 lines). Each instruction is 32 bits (i.e., 4 lines, saved in little-endian mode).
	Each line in the input file is stored as an hex and is 1 byte (each four lines are one instruction). You need to read the file line by line and store it into the memory. You may need a mechanism to convert these values to bits so that you can read opcodes, operands, etc.
	*/

	char instMem[4096];


	if (argc < 2) {
		//cout << "No file name entered. Exiting...";
		return -1;
	}

	ifstream infile(argv[1]); //open the file
	if (!(infile.is_open() && infile.good())) {
		cout<<"error opening file\n";
		return 0; 
	}
	string line; 
	int i = 0;
	while (infile) {
			infile>>line;
			stringstream line2(line);
			char x; 
			line2>>x;
			instMem[i] = x; // be careful about hex
			i++;
			line2>>x;
			instMem[i] = x; // be careful about hex
			//cout<<instMem[i]<<endl;
			i++;
		}
	int maxPC= i/8; 

	/* Instantiate your CPU object here.  CPU class is the main class in this project that defines different components of the processor.
	CPU class also has different functions for each stage (e.g., fetching an instruction, decoding, etc.).
	*/

	CPU myCPU;  // call the approriate constructor here to initialize the processor...  
	// make sure to create a variable for PC and resets it to zero (e.g., unsigned int PC = 0); 

	/* OPTIONAL: Instantiate your Instruction object here. */
	Instruction myInst; 

	bool done = true;
	while (done == true) // processor's main loop. Each iteration is equal to one clock cycle.  
	{
		//fetch instruction from instMem and PC
		myInst = myCPU.fetchInstruction(instMem);
		// cout << myInst.instr << endl;
		if (myInst.getOpCode().to_string() == "0000000"){
			break;
		}

		// decode
		// cout << myInst.getOpCode() << endl;
		myCPU.cpu_control.setController(myInst.getOpCode()); // set the controller using opcode

		// update rs1, rs2, rd, imm values as necessary and according to control rules
		myCPU.updateValuesInstructionDecode(myInst);

		// execute ALU_Control and ALU
		myCPU.alu_control.setALUControl(myCPU.cpu_control.aluop, myInst); // get the 4-bit code to ALU
		
		int secondvalue;
		// logic of mux to choose rs2 or imm
		if (myCPU.cpu_control.alusrc == 1){
			secondvalue = myCPU.imm;
			//cout << "second (imm): " << secondvalue << endl;
		}
		else{
			secondvalue = myCPU.regfile[myCPU.rs2];
			//cout << "second (rs2): " << secondvalue << endl;
		}
		int firstvalue = myCPU.regfile[myCPU.rs1];
		//cout << "first: " << firstvalue << endl;
		myCPU.alu.executeALU(myCPU.alu_control.fourbitout, firstvalue, secondvalue, myCPU.cpu_control.islui);

		// mem
		// do branching section here
		if (myCPU.cpu_control.branch == 1){
			// if BNE
			if (myCPU.cpu_control.regwrite != 1){
				if (myCPU.alu.zero == 1){
					// they are equal, so dont branch BNE
					myCPU.incPC();
				}
				else{ // gotta branch to label which is immediate
					int decrement = myCPU.imm << 1;
					//cout << "decrement: " << decrement << endl;
					myCPU.setPC(myCPU.readPC() + decrement/4);
				}
			}
			else{ //JALR
				// write address to jump back to to reg rd
				if (myCPU.rd != 0){ // only if not x0
					myCPU.regfile[myCPU.rd] = 4*myCPU.readPC() + 4;	
				}
				//cout << "JALR PC + 1: " << myCPU.readPC() + 1 << endl;
				// jump to reg[rs1] + offset [31:1], 1'b0
				uint32_t address = myCPU.alu.alu_res & ~1;
				myCPU.setPC(address/4);
			}
			//cout << endl;
			continue; // skip rest of loop
		}
		

		
		// if mem write is true (STORE WORD)
		if (myCPU.cpu_control.memwr == 1){
			// check func3: if 010 then SW
			if (myInst.getfunc3().to_string() == "010"){
				myCPU.storeword(myCPU.alu.alu_res, myCPU.regfile[myCPU.rs2]);
			}
			else{ // SH
				myCPU.storehalf(myCPU.alu.alu_res, myCPU.regfile[myCPU.rs2]);
			}
			//cout << "Stored: " << myCPU.regfile[myCPU.rs2] << " in address " << myCPU.alu.alu_res << endl;
		}
		
		int resToWriteBack;
		// if mem to reg is 0 take alu result
		if (myCPU.cpu_control.memtoreg == 0){
			resToWriteBack = myCPU.alu.alu_res;
		}
		else{ // take data that was read
			if (myCPU.cpu_control.memre == 1){ // (LOAD WORD)
				// check func3: if 010 then LW
				if (myInst.getfunc3().to_string() == "010"){
					resToWriteBack = myCPU.loadword(myCPU.alu.alu_res);
				}
				else{
					resToWriteBack = myCPU.loadbyteunsigned(myCPU.alu.alu_res);
				}
				//cout << "Loaded: " << resToWriteBack << " into register " << myCPU.rd << endl;
			}
		}

		// write back (can't write to x0)
		if (myCPU.cpu_control.regwrite == 1 && myCPU.rd != 0){
			myCPU.regfile[myCPU.rd] = resToWriteBack;
			//cout << "RES: " << resToWriteBack << endl;
		}
		//cout << endl;
		myCPU.incPC();
		if (myCPU.readPC() > maxPC)
			break;
	}
	int a0 =myCPU.regfile[10];
	int a1 =myCPU.regfile[11];  
	// print the results (you should replace a0 and a1 with your own variables that point to a0 and a1)
	cout << "(" << a0 << "," << a1 << ")" << endl;
	//cout << instMem[0] << ", " << instMem[1] << endl;
	//cout << i << " " << maxPC << endl;
	return 0;

}