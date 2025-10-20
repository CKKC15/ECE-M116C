#include "CPU.h"

CPU::CPU()
{
	PC = 0; //set PC to 0
	for (int i = 0; i < 4096; i++) //copy instrMEM
	{
		dmemory[i] = (0);
	}
	// set regfile x0 -> 0
	regfile[0] = 0;
}

void CPU::setPC(int val){
	PC = val;
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
	//cout << hexStr << endl;

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
	if (cpu_control.islui != 1){ 
		rs1 = myInst.extractBits(19, 5, false);
		//cout << "rs1: " << rs1 << endl;
	}
	// get rd if (regwrite = 1)
	if (cpu_control.regwrite == 1){
		rd = myInst.extractBits(11, 5, false);
		//cout << "rd: " << rd << endl;
	}
	// ALUsrc mux logic
	if (cpu_control.alusrc == 0){ // read rs2
		rs2 = myInst.extractBits(24, 5, false);
		// but if branch == 1 we need immediate too
		//cout << "rs2: " << rs2 << endl;
		if (cpu_control.branch == 1){ // BNE then we need immediate too
			imm = myInst.extractBNEImmediate();
		}
		//cout << "imm: " << imm << endl;
	}
	else { // else alusrc == 1 then immediate
		// if LUI then immediate is longer
		//cout << myInst.instr << endl;
		if (cpu_control.islui == 1){
			imm = myInst.extractBits(31, 20, true);
		}
		else if (cpu_control.issw == 1){ // if store word, immediate is different and we also need rs2
			rs2 = myInst.extractBits(24, 5, false);
			//cout << "rs2: " << rs2 << endl;
			imm = myInst.extractSWImmediate();
		}
		else{
			if (cpu_control.branch == 1){ //JALR then we want unsigned result
				imm = myInst.extractBits(31, 12, false);
			}
			else{
				imm = myInst.extractBits(31, 12, true);
			}
		}
		//cout << "imm: " << imm << endl;
	}
}


void Controller::setController(bitset<7> opcode){ // given opcode will set the control bits accordingly
	if (opcode.to_string() == "0110011") { // R TYPE
		regwrite = 1; alusrc = 0; branch = 0; memre = 0; memwr = 0; memtoreg = 0; aluop = 0b10; islui = 0; issw = 0;
	}
	else if (opcode.to_string() == "0010011") { // I TYPE
		regwrite = 1; alusrc = 1; branch = 0; memre = 0; memwr = 0; memtoreg = 0; aluop = 0b11; islui = 0; issw = 0;
	}
	else if (opcode.to_string() == "1100011") { // BNE
		regwrite = 0; alusrc = 0; branch = 1; memre = 0; memwr = 0; memtoreg = 0; aluop = 0b01; islui = 0; issw = 0;
	}
	else if (opcode.to_string() == "1100111") { // JALR
		regwrite = 1; alusrc = 1; branch = 1; memre = 0; memwr = 0; memtoreg = 0; aluop = 0b00; islui = 0; issw = 0;
	}
	else if (opcode.to_string() == "0000011") { // LW/LBU
		regwrite = 1; alusrc = 1; branch = 0; memre = 1; memwr = 0; memtoreg = 1; aluop = 0b00; islui = 0; issw = 0;
	}
	else if (opcode.to_string() == "0100011") { // SW/SH
		regwrite = 0; alusrc = 1; branch = 0; memre = 0; memwr = 1; memtoreg = 0; aluop = 0b00; islui = 0; issw = 1;
	}
	else if (opcode.to_string() == "0110111") { // LUI aluop doesn't matter we use islui
		regwrite = 1; alusrc = 1; branch = 0; memre = 0; memwr = 0; memtoreg = 0; aluop = 0b00; islui = 1; issw = 0;
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

int Instruction::extractBits(int start, int length, bool takesign) {
    string s;
	for (int i = 0; i < length; i++){
		s += (instr[start - i] ? '1' : '0');
	}
	// << "extracted " << s << endl;
	int value = std::stoi(s, nullptr, 2);
	// if negative
	if (takesign && s[0] == '1'){ // find 2's complement myself
		value = value - (1LL << s.size());
	}
	return value;
}

int Instruction::extractSWImmediate() {
    string s;
	for (int i = 0; i < 7; i++){
		s += (instr[31 - i] ? '1' : '0');
	}
	for (int i = 0; i < 5; i++){
		s += (instr[11 - i] ? '1' : '0');
	}
	//cout << "extracted " << s << endl;
	int value = std::stoi(s, nullptr, 2);
	// if negative
	if (s[0] == '1'){ // find 2's complement myself
		value = value - (1LL << s.size());
	}
	return value;
}

int Instruction::extractBNEImmediate() {
	string s;
	s += (instr[31] ? '1' : '0'); //12
	s += (instr[7] ? '1' : '0'); //11
	for (int i = 0; i < 6; i++){ // 10-5
		s += (instr[30 - i] ? '1' : '0');
	}
	for (int i = 0; i < 4; i++){ // 4-1
		s += (instr[11 - i] ? '1' : '0');
	}
	
	//cout << "extracted " << s << endl;
	int value = std::stoi(s, nullptr, 2);
	// if negative
	if (s[0] == '1'){ // find 2's complement myself
		value = value - (1LL << s.size());
	}
	return value;
}

void ALU_Control::setALUControl(int aluop, Instruction myInst){
	// easy case if 00 or 01
	if (aluop == 0b00){ // SW/LW/SH/JALR Type -> ADD
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
		else if (myInst.getfunc3().to_string() == "101"){ // SRA
			fourbitout = 0b0001;
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
		else if (myInst.getfunc3().to_string() == "011"){ // SLTIU
			fourbitout = 0b0010;
		}
	}
}

void ALU::executeALU(int fourbit, int rs1, int rs2orimm, bool lui){
	// LUI
	if (lui == true){
		alu_res = rs2orimm << 12;
	}

	// ADD				LW/SW/SH				ADD						ADDI
	else if (fourbit == 0b0000 || fourbit == 0b1000 || fourbit == 0b1100){
		alu_res = rs1 + rs2orimm;
	}
	// SUB             	BNE						SUB			
	else if (fourbit == 0b0100 || fourbit == 0b1001){
		alu_res = rs1 - rs2orimm;
		zero = (alu_res == 0);
	}
	else if (fourbit == 0b0010) { // SLTIU
		alu_res = ((unsigned int)rs1 < (unsigned int)rs2orimm) ? 1 : 0;
	}
	// AND				AND					ANDI
	else if (fourbit == 0b1011 || fourbit == 0b1111){
		alu_res = rs1 & rs2orimm;
	}
	// OR				OR					ORI
	else if (fourbit == 0b1010 || fourbit == 0b1110){
		alu_res = rs1 | rs2orimm;
	}
	// SHIFT (SRA)
	else if (fourbit == 0b0001){
		alu_res = rs1 >> rs2orimm;
	}
}

int CPU::loadword(uint32_t address){
	address -= 65300;
	uint32_t value = 0;
    value |= (uint8_t)dmemory[address];
    value |= (uint8_t)dmemory[address + 1] << 8;
    value |= (uint8_t)dmemory[address + 2] << 16;
    value |= (uint8_t)dmemory[address + 3] << 24;
    return value;
}

void CPU::storeword(uint32_t address, uint32_t value) {
	address -= 65300;
    // Store least significant byte first (little-endian)
    dmemory[address]     = value & 0xFF;
    dmemory[address + 1] = (value >> 8) & 0xFF;
    dmemory[address + 2] = (value >> 16) & 0xFF;
    dmemory[address + 3] = (value >> 24) & 0xFF;
}

int CPU::loadbyteunsigned(uint32_t address){
	address -= 65300;
	return (uint8_t)dmemory[address];
}

void CPU::storehalf(uint32_t address, uint32_t value){
	address -= 65300;
	dmemory[address]     = value & 0xFF;        // LSB
    dmemory[address + 1] = (value >> 8) & 0xFF; // MSB
}