#include "CPU.h"

CPU::CPU()
{
	PC = 0; //set PC to 0
	for (int i = 0; i < 4096; i++) //copy instrMEM
	{
		dmemory[i] = (0);
	}
}

unsigned long CPU::readPC()
{
	return PC;
}

void CPU::incPC()
{
	PC++;
}

Instruction CPU::fetchInstruction(char* instMem)
{
	// Each instruction = 8 hex chars (32 bits)
    int startIndex = PC * 8;  

    string hexStr = "";

    // Collect 8 characters in the correct little endianness order
    for (int byte = 3; byte >= 0; --byte) {
        int idx = startIndex + byte * 2;
        hexStr += instMem[idx];
        hexStr += instMem[idx + 1];
    }
	cout << hexStr << endl;

    // Convert hex string -> unsigned integer
    unsigned int value = 0;
    stringstream ss;
    ss << std::hex << hexStr;
    ss >> value;

    // Create Instruction object from that integer
    bitset<32> bits(value);
    return Instruction(bits);
}

void CPU::updateValuesInstructionDecode(Instruction myInst){
	// read rs1 if not LUI (aluop != 1)
	if (cpu_control.aluop != 1){ 
		rs1 = myInst.extractBits(19, 5);
		cout << "rs1: " << rs1 << endl;
	}
	// get rd if (regwrite = 1)
	if (cpu_control.regwrite == 1){
		rd = myInst.extractBits(11, 5);
		cout << "rd: " << rd << endl;
	}
	// ALUsrc mux logic
	if (cpu_control.alusrc == 0){ // read rs2
		rs2 = myInst.extractBits(24, 5);
		cout << "rs2: " << rs2 << endl;
	}
	else { // else alusrc == 1 then immediate
		// if LUI then immediate is longer
		//cout << myInst.instr << endl;
		if (cpu_control.aluop == 1){
			imm = myInst.extractBits(31, 20);
		}
		else{
			imm = myInst.extractBits(31, 12);
		}
		cout << "imm: " << imm << endl;
	}
}


void Controller::setController(bitset<7> opcode){ // given opcode will set the control bits accordingly
	if (opcode.to_string() == "0110011") { // R TYPE
		regwrite = 1; alusrc = 0; branch = 0; memre = 0; memwr = 0; memtoreg = 0; aluop = 0b10;
	}
	else if (opcode.to_string() == "0010011") { // I TYPE
		regwrite = 1; alusrc = 1; branch = 0; memre = 0; memwr = 0; memtoreg = 0; aluop = 0b11;
	}
	else if (opcode.to_string() == "1100011") { // BEQ TYPES TODO: Fix this custom ALUOP
		regwrite = 1; alusrc = 1; branch = 0; memre = 0; memwr = 0; memtoreg = 0; aluop = 0b01;
	}
	else if (opcode.to_string() == "0000011" || opcode.to_string() == "0100011") { // LW/SW TYPES TODO: Fix this custom ALUOP
		regwrite = 1; alusrc = 1; branch = 0; memre = 0; memwr = 0; memtoreg = 0; aluop = 0b00;
	}
	else if (opcode.to_string() == "0110111") { // LUI TODO: Fix this custom ALUOP
		regwrite = 1; alusrc = 1; branch = 0; memre = 0; memwr = 0; memtoreg = 0; aluop = 1;
	}
	
}

bitset<7> Instruction::getOpCode(){
	bitset<7> opcode;

	for (int i = 0; i < 7; i++) {
		opcode[i] = instr[i]; // copy bits 0–6
	}
	return opcode;
}
bitset<3> Instruction::getfunc3(){
	bitset<3> func3;

	for (int i = 0; i < 3; i++){
		func3[i] = instr[12 + i];
	}
	return func3;
}
bitset<7> Instruction::getfunc7(){
	bitset<7> func7;

	for (int i = 0; i < 7; i++){
		func7[i] = instr[25 + i];
	}
	return func7;
}

int Instruction::extractBits(int start, int length) {
    string s;
	for (int i = 0; i < length; i++){
		s += (instr[start - i] ? '1' : '0');
	}
	cout << "extracted " << s << endl;
	int value = std::stoi(s, nullptr, 2);
	// if negative
	if (s[0] == '1'){ // find 2's complement myself
		value = value - (1LL << s.size());
	}
	return value;
}

void ALU_Control::setALUControl(int aluop, Instruction myInst){
	// easy case if 00 or 01
	if (aluop == 0b00){ // SW/LW Type -> ADD
		fourbitout = 0b0000;
	}
	else if (aluop == 0b01){ // BEQ Type -> SUB
		fourbitout = 0b0100;
	}
	else if (aluop == 0b10){ // R Type -> check func3
		if (myInst.getfunc3().to_string() == "000"){  // ADD/SUB
			// check func7: either ADD/SUB
			if (myInst.getfunc7().to_string() == "0000000"){
				fourbitout = 0b1000; // ADD
			}
			else{
				fourbitout = 0b1001; // SUB
			}
		}
		else if (myInst.getfunc3().to_string() == "110"){ // OR
			fourbitout = 0b1010;
		}
		else if (myInst.getfunc3().to_string() == "111"){ // AND
			fourbitout = 0b1011;
		}
	}
	else if (aluop == 0b11){ // I Type -> check func3
		if (myInst.getfunc3().to_string() == "000"){  // ADDI
			fourbitout = 0b1100;
		}
		else if (myInst.getfunc3().to_string() == "110"){ // ORI
			fourbitout = 0b1110;
		}
		else if (myInst.getfunc3().to_string() == "111"){ // ANDI
			fourbitout = 0b1111;
		}
	}
}

void ALU::executeALU(int fourbit, int rs1, int rs2orimm, bool lui){
	// LUI
	if (lui == true){

	}

	// ADD				LWSW				ADD						ADDI
	else if (fourbit == 0b0000 || fourbit == 0b1000 || fourbit == 0b1100){

	}
	// SUB             	BNE						SUB					SLTIU
	else if (fourbit == 0b0100 || fourbit == 0b1001 || fourbit == 0b0010){

	}
	// AND				AND					ANDI
	else if (fourbit == 0b1011 || fourbit == 0b1111){

	}
	// OR				OR					ORI
	else if (fourbit == 0b1010 || fourbit == 0b1110){

	}
	// SHIFT (SRA)
	else if (fourbit == 0b0001){
		
	}
}