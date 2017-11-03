#include "runner.h"
#include "assert.h"
#include "parser.h"

Runner::Runner(Parser *parser) {
  m_parser = parser;
  // generate word parser functors
  // Generate parse functors. This should probably be a template class!
  decodeRInstr =
      generateWordParser(vector<int>{5, 3, 5, 5, 7}); // from LSB to MSB
  decodeIInstr = generateWordParser(vector<int>{5, 3, 5, 12});
  decodeSInstr = generateWordParser(vector<int>{5, 3, 5, 5, 7});
  decodeBInstr = generateWordParser(vector<int>{1, 4, 3, 5, 5, 6, 1});
  decodeUInstr = generateWordParser(vector<int>{5, 20});
  decodeJInstr = generateWordParser(vector<int>{5, 8, 1, 10, 1});
  // memory allocation
  // temporary allocation method - get filesize from parser, and allocate same
  // amount of memory
  m_textSize = m_parser->getFileSize();
  m_text = std::vector<uint8_t>(m_textSize);
  m_reg = std::vector<uint32_t>(32, 0);

  // file parsing
  m_parser->parseFile(&m_text);
}

Runner::~Runner() {}

int Runner::exec() {
  // Main simulator loop:
  // Make parser parse an instruction based on the current program counter, and,
  // if successfull, execute the read instruction. Loop until parser
  // unsuccesfully parses an instruction.
  instrState err;
  while (getInstruction(m_pc)) {
    if ((err = execInstruction(m_currentInstruction)) != SUCCESS) {
      handleError(err);
      return 1;
    }
  }
  return 0;
}

bool Runner::getInstruction(int pc) {
  if (pc < m_textSize) {
    auto word = *((uint32_t *)(m_text.data() + pc));
    m_currentInstruction.word = word;
    m_currentInstruction.type = static_cast<instrType>(word & 0x7f);
    return true;
  }
  return false;
}

instrState Runner::execInstruction(Instruction instr) {
  switch (instr.type) {
  case LUI:
    return execLuiInstr(instr);
  case JAL:
    return execJalInstr(instr);
  case JALR:
    return execJalrInstr(instr);
  case BRANCH:
    return execBranchInstr(instr);
  case LOAD:
    return execLoadInstr(instr);
  case STORE:
    return execStoreInstr(instr);
  case OP_IMM:
    return execOpImmInstr(instr);
  case OP:
    return execOpInstr(instr);
  default:
    return instrState::EXEC_ERR;
    break;
  }
}

instrState Runner::execLuiInstr(Instruction instr) {
  /* "LUI places the U-immediate value in the top 20 bits of
   * the destination m_register rd, filling in the lowest 12 bits with zeros"*/
  std::vector<uint32_t> fields = decodeUInstr(instr.word);
  m_reg[fields[1]] = fields[0] << 12;
  m_pc += 4;
  return SUCCESS;
}

instrState Runner::execJalInstr(Instruction instr) {
  std::vector<uint32_t> fields = decodeJInstr(instr.word);
  m_pc += fields[0] << 20 | fields[1] << 1 | fields[2] << 11 |
          fields[3] << 12; // must be signed!
  m_reg[fields[4]] =
      m_pc +
      4; // rd = pc + 4 // is rd equal to pc+4 before or after pc increment?
  return SUCCESS;
}

instrState Runner::execJalrInstr(Instruction instr) {
  std::vector<uint32_t> fields = decodeIInstr(instr.word);
  m_reg[fields[3]] = m_pc + 4; // store return address
  m_pc = ((int32_t)fields[0] + fields[1]) &
         0xfffe; // set LSB of result to zero // shouldnt this be 0xfffffffe?
  return SUCCESS;
}

instrState Runner::execBranchInstr(Instruction instr) {
  std::vector<uint32_t> fields = decodeBInstr(instr.word);

  // calculate target address using signed offset
  auto target = m_pc + (int32_t)((fields[0] << 12) | (fields[1] << 5) |
                                 (fields[5] << 1) | (fields[6] << 11));
  switch (fields[4]) {
  case 0b000: // BEQ
    m_pc = m_reg[fields[2]] == m_reg[fields[3]] ? target : m_pc + 4;
    break;
  case 0b001: // BNE
    m_pc = m_reg[fields[2]] != m_reg[fields[3]] ? target : m_pc + 4;
    break;
  case 0b100: // BLT - signed comparison
    m_pc = (int32_t)m_reg[fields[2]] > (int32_t)m_reg[fields[3]] ? target
                                                                 : m_pc + 4;
    break;
  case 0b101: // BGE - signed comparison
    m_pc = (int32_t)m_reg[fields[2]] <= (int32_t)m_reg[fields[3]] ? target
                                                                  : m_pc + 4;
    break;
  case 0b110: // BLTU
    m_pc = m_reg[fields[2]] > m_reg[fields[3]] ? target : m_pc + 4;
    break;
  case 0b111: // BGEU
    m_pc = m_reg[fields[2]] <= m_reg[fields[3]] ? target : m_pc + 4;
    break;
  default:
    return ERR_BFUNCT3;
  }
  return SUCCESS;
}

instrState Runner::execLoadInstr(Instruction instr) {
  std::vector<uint32_t> fields = decodeIInstr(instr.word);
  if (fields[3] == 0) {
    return ERR_NULLLOAD;
  }
  auto target = (int32_t)fields[0] + fields[1];

  // Handle different load types by pointer casting and subsequent
  // dereferencing. This will handle whether to sign or zero extend.
  switch (fields[2]) {
  case 0b000: // LB - load sign extended byte
    m_reg[fields[3]] = ((int8_t *)m_mem)[target];
    break;
  case 0b001: // LH load sign extended halfword
    m_reg[fields[3]] = ((int16_t *)m_mem)[target];
    break;
  case 0b010: // LW load word
    m_reg[fields[3]] = ((uint32_t *)m_mem)[target];
    break;
  case 0b100: // LBU load zero extended byte
    m_reg[fields[3]] = ((uint8_t *)m_mem)[target];
    break;
  case 0b101: // LHU load zero extended halfword
    m_reg[fields[3]] = ((uint16_t *)m_mem)[target];
    break;
  }
  return SUCCESS;
}

instrState Runner::execStoreInstr(Instruction instr) {
  std::vector<uint32_t> fields = decodeSInstr(instr.word);

  auto target = (int32_t)(fields[0] << 5 | fields[4]) + m_reg[fields[2]];
  switch (fields[3]) {
  case 0b000: // SB
    m_mem[target] = (uint8_t)m_reg[fields[1]];
    break;
  case 0b001:                                   // SH
    m_mem[target] = (uint16_t)m_reg[fields[1]]; // will this work? m_mem[target]
                                                // is uint8_t. I think we need
                                                // (uint16_t)((m_mem +
                                                // target)*), not sure though
    break;
  case 0b010: // SW
    m_mem[target] = m_reg[fields[1]];
    break;
  default:
    return ERR_BFUNCT3;
  }
  return SUCCESS;
}

instrState Runner::execOpImmInstr(Instruction instr) {
  std::vector<uint32_t> fields = decodeIInstr(instr.word);
  if (fields[3] == 0) {
    return ERR_NULLLOAD;
  }

  switch (fields[2]) {
  case 0b000: // ADDI
    m_reg[fields[3]] = m_reg[fields[1]] + (int32_t)fields[0];
    break;
  case 0b010: // SLTI
    m_reg[fields[3]] = (int32_t)m_reg[fields[1]] < (int32_t)fields[0] ? 1 : 0;
    break;
  case 0b011: // SLTIU
    m_reg[fields[3]] = m_reg[fields[1]] < fields[0] ? 1 : 0;
    break;
  case 0b100: // XORI
    m_reg[fields[3]] = m_reg[fields[1]] ^ fields[0];
    break;
  case 0b110: // ORI
    m_reg[fields[3]] = m_reg[fields[1]] | fields[0];
    break;
  case 0b111: // ANDI
    m_reg[fields[3]] = m_reg[fields[1]] & fields[0];
    break;
  }
  m_pc += 4;
  return SUCCESS;
}

instrState Runner::execOpInstr(Instruction instr) {
  std::vector<uint32_t> fields = decodeRInstr(instr.word);
  switch (fields[3]) {
  case 0b000:
    if (fields[0] == 0) {
      m_reg[fields[4]] = m_reg[fields[1]] + m_reg[fields[2]]; // Add
    } else if (fields[0] == 0b0100000) {
      m_reg[fields[4]] = m_reg[fields[1]] - m_reg[fields[2]]; // Sub
    }
  }
  // insert rest of code

  m_pc += 4;
  return SUCCESS;
}

void Runner::handleError(instrState err) const {
  // handle error and print program counter + current instruction
}

uint32_t generateBitmask(int n) {
  // Generate bitmask. There might be a smarter way to do this
  uint32_t mask = 0;
  for (int i = 0; i < n - 1; i++) {
    mask |= 0b1;
    mask <<= 1;
  }
  mask |= 0b1;
  return mask;
}

uint32_t bitcount(int n) {
  int count = 0;
  while (n > 0) {
    count += 1;
    n = n & (n - 1);
  }
  return count;
}

decode_functor Runner::generateWordParser(std::vector<int> bitFields) {
  // Generates functors that can decode a binary number based on the input
  // vector which is supplied upon generation

  // Assert that total bitField size is (32-7)=25-bit. Subtract 7 for op-code
  int tot = 0;
  for (const auto &field : bitFields) {
    tot += field;
  }
  assert(tot == 25 && "Requested word parsing format is not 32-bit in length");

  // Generate vector of <fieldsize,bitmask>
  std::vector<std::pair<uint32_t, uint32_t>> parseVector;

  // Generate bit masks and fill parse vector
  for (const auto &field : bitFields) {
    parseVector.push_back(
        std::pair<uint32_t, uint32_t>(field, generateBitmask(field)));
  }

  // Create parse functor
  decode_functor wordParser = [=](uint32_t word) {
    word = word >> 7; // remove OpCode
    std::vector<uint32_t> parsedWord;
    for (const auto &field : parseVector) {
      parsedWord.insert(parsedWord.begin(), word & field.second);
      word = word >> field.first;
    }
    return parsedWord;
  };

  return wordParser;
}