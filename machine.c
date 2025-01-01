#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <assert.h>
#include "machine.h"
#include "code.h"

struct machine_t machine;

/*
 * Allocate more space to keep track of values on the simulated stack.
 */
void grow_stack(uint64_t new_sp) {
    // Grow the stack upwards
    if (new_sp < machine.stack_top) {
        // Round down to a multiple of word size
        if (new_sp % WORD_SIZE_BYTES != 0) {
            new_sp -= new_sp % WORD_SIZE_BYTES;
        }

        // Allocate space and copy over old values 
        void *new_stack = malloc(machine.stack_bot - new_sp + 1);
        memset(new_stack, 0, machine.stack_top - new_sp);
        if (machine.stack != NULL) {
            memcpy(new_stack + (machine.stack_top - new_sp), machine.stack, machine.stack_bot - machine.stack_top + 1);
            free(machine.stack);
        }

        // Update machine
        machine.stack = new_stack;
        machine.stack_top = new_sp;
    }
    // Grow the stack downwards
    else if (new_sp > machine.stack_bot) {
        // Round up to a multiple of word size
        if (new_sp % WORD_SIZE_BYTES != 0) {
            new_sp += WORD_SIZE_BYTES - (new_sp % WORD_SIZE_BYTES);
        }
        else {
            new_sp += WORD_SIZE_BYTES;
        }

        // Allocate space and copy over old values 
        void *new_stack = malloc(new_sp - machine.stack_top);
        memset(new_stack + (machine.stack_bot - machine.stack_top), 0, new_sp - machine.stack_bot);
        if (machine.stack != NULL) {
            memcpy(new_stack, machine.stack, machine.stack_bot - machine.stack_top);
            free(machine.stack);
        }

        // Update machine
        machine.stack = new_stack;
        machine.stack_bot = new_sp - 1;
    }
}

/*
 * Initialize the machine
 */
void init_machine(uint64_t sp, uint64_t pc, char *code_filepath) {
    // Populate general purpose registers
    for (int i = 0; i <= 30; i++) {
        machine.registers[i] = REGISTER_NULL;
    }
    
    // Populate special purpose registers
    machine.sp = sp;
    machine.pc = pc;
    
    // Load code
    machine.code = parse_file(code_filepath, &(machine.code_top), &(machine.code_bot));

    // Prepare stack
    machine.stack_top = sp;
    machine.stack_bot = sp + WORD_SIZE_BYTES - 1;
    machine.stack = malloc(WORD_SIZE_BYTES);
    memset(machine.stack, 0, WORD_SIZE_BYTES);

    // Clear all condition codes
    machine.conditions = 0;
}

void print_memory() {
    // Print condition codes
    printf("Condition codes:");
    if (machine.conditions & CONDITION_ZERO) {
        printf(" Z");
    }
    if (machine.conditions & CONDITION_NEGATIVE) {
        printf(" N");
    }
    if (machine.conditions & CONDITION_POSITIVE) {
        printf(" P");
    }
    printf("\n");

    // Print the value of all used registers
    printf("Registers:\n");
    for (int i = 0; i <= 30; i++) {
        if (machine.registers[i] != REGISTER_NULL) {
            printf("\tw/x%d = 0x%lx\n", i, machine.registers[i]);
        }
    }
    printf("\tsp = 0x%lX\n", machine.sp);
    printf("\tpc = 0x%lX\n", machine.pc);

    // If necessary, grow the stack before printing it
    if (machine.sp < machine.stack_top || machine.sp > machine.stack_bot) {
        grow_stack(machine.sp);
    }

    // Print the value of all words on the stack
    printf("Stack:\n");
    unsigned char *stack = machine.stack;
    for (int i = 0; i < (machine.stack_bot - machine.stack_top); i += 8) {
        printf("\t");

        if (machine.sp == i + machine.stack_top) {
            printf("%10s ", "sp->");
        }
        else {
            printf("           ");
        }

        printf("+-------------------------+\n");
        printf("\t0x%08lX | ", i + machine.stack_top);
        for (int j = 0; j < 8; j++) {
            printf("%02X ", stack[i+j]);
        }
        printf("|\n");
    }
    printf("\t           +-------------------------+\n");
}

/*
 * Get the next instruction to execute
 */
struct instruction_t fetch() {
    int index = (machine.pc - machine.code_top) / 4;
    return machine.code[index];
}

//return a value of a given register of an operand, with modifications depending on its type
uint64_t look_thru_registers(struct operand_t operand){
    switch(operand.reg_type){
        case REGISTER_w:
            uint64_t temp = machine.registers[operand.reg_num];
            temp <<= 32;
            temp >>= 32;
            return temp;
            break;
        case REGISTER_x:
            return machine.registers[operand.reg_num];
            break;
        case REGISTER_sp:
            return machine.sp;
            break;
        case REGISTER_pc:
            return machine.pc;
            break; 
    }
    return 0;
}
/*
 * Get the value associated with a constant or register operand.
 */
uint64_t get_value(struct operand_t operand) {
    assert(operand.type == OPERAND_constant || operand.type == OPERAND_address || operand.type == OPERAND_register);
    
    if((operand.type == OPERAND_constant) || (operand.type == OPERAND_address)){return operand.constant;}

    else if(operand.type == OPERAND_register){
        uint64_t get = look_thru_registers(operand);
        return get;
        }
    return 0;
}

/*
 * Put a value in a register specified by an operand.
 */
void put_value(struct operand_t operand, uint64_t value) {
    assert(operand.type == OPERAND_register);
    switch(operand.reg_type){
        case REGISTER_w:
            value <<= 32;
            value >>= 32;
            machine.registers[operand.reg_num] = value;
            break;
        case REGISTER_x:
            machine.registers[operand.reg_num] = value;
            break;
        case REGISTER_sp:
            machine.sp = value;
            break;
        case REGISTER_pc:
            machine.pc = value;
            break; 
    }
}

/*
 * Get the memory address associated with a memory operand.
 */
uint64_t get_memory_address(struct operand_t operand) {
    assert(operand.type == OPERAND_memory);
    uint64_t k = look_thru_registers(operand);
    return (k + operand.constant);
    }


//executes all arithmetic based operations
void execute_arithmetic(struct instruction_t instruction) {
    uint64_t op1 = get_value(instruction.operands[1]);
    uint64_t op2 = get_value(instruction.operands[2]);
    uint64_t result;
    switch(instruction.operation) {
    case OPERATION_add:
        result = op1 + op2;
        break;
    case OPERATION_sub:
    case OPERATION_subs:
        result = op1 - op2;
        break;
    case OPERATION_mul:
        result = op1 * op2;
        break;
    case OPERATION_sdiv:
    case OPERATION_udiv:
        result = op1 / op2;
        break;
    case OPERATION_neg:
        result = (-1) * op1;
        break;
    }
    put_value(instruction.operands[0], result);
}

//executes all bitwise operations
void execute_bitwise(struct instruction_t instruction){
    uint64_t op1 = get_value(instruction.operands[1]);
    uint64_t op2 = 0;
    if(instruction.operation != OPERATION_mvn){
        op2 = get_value(instruction.operands[2]);
    }

    uint64_t result;
    switch(instruction.operation){
    case OPERATION_lsl:
        result = op1 << op2;
        break;
    case OPERATION_lsr:
        result = op1 >> op2;
        break;
    case OPERATION_and:
        result = op1 & op2;
        break;
    case OPERATION_orr:
        result = op1 | op2;
        break;
    case OPERATION_eor:
        result = op1 ^ op2;
        break;
    case OPERATION_mvn:
        result = ~op1;
        break;
    }


    put_value(instruction.operands[0], result);
}

//executes all move, store, and load operations
void execute_MSL(struct instruction_t instruction){

    uint64_t sim_mem_address;
    if(instruction.operation != OPERATION_mov){
        sim_mem_address = get_memory_address(instruction.operands[1]);
    }
    switch(instruction.operation){
        case OPERATION_mov:
            uint64_t op2 = get_value(instruction.operands[1]);
            put_value(instruction.operands[0],op2);
            break;
        case OPERATION_ldr:
            if(instruction.operands[0].reg_type == REGISTER_w){
                uint32_t *stack = machine.stack;
                put_value(instruction.operands[0],*(stack+ ((sim_mem_address-machine.stack_top)/4)));
            }
            else{
                uint64_t *stack = machine.stack;
                put_value(instruction.operands[0],*(stack+ ((sim_mem_address-machine.stack_top)/8)));
            }
            break;
        case OPERATION_str:
        uint32_t *stack = machine.stack;
        uint32_t stack_add = (sim_mem_address - machine.stack_top) /4;
        uint64_t store = get_value(instruction.operands[0]);
        if(instruction.operands[0].reg_type == REGISTER_w){
            store &= 0xFFFFFFFF;
        }
        *(stack+stack_add) = store;
        if(instruction.operands[0].reg_type == REGISTER_x){
            uint64_t *stack = machine.stack;
            *(stack + ((sim_mem_address-machine.sp)/8)) = get_value(instruction.operands[0]);
        }
            break;
    }    

}

//executes the comparison operation
void execute_compare(struct instruction_t instruction){
    uint64_t op1 = get_value(instruction.operands[0]);
    uint64_t op2 = get_value(instruction.operands[1]);
    uint8_t flag;
    if(op1-op2 == 0){flag = CONDITION_ZERO;}
    else if(op1-op2 < 0){flag = CONDITION_NEGATIVE;}
    else if(op1-op2 > 0){flag = CONDITION_POSITIVE;}
    machine.conditions = flag;
}

//executes the branch operations that do not require a comparison check
void execute_basic_branch(struct instruction_t instruction){
    uint64_t op1 = get_value(instruction.operands[0]);
    switch(instruction.operation){
        case OPERATION_b:
            machine.pc = op1;
            break;
        case OPERATION_bl:
            machine.registers[30] = machine.pc + 4;
            machine.pc = op1;
            break;
    }
}

//executes the branch operations that do require a comparison check
void execute_conditional_branch(struct instruction_t instruction){
    uint64_t op1 = get_value(instruction.operands[0]);
    switch(instruction.operation){
        case OPERATION_bne:
            if(machine.conditions == CONDITION_NEGATIVE || machine.conditions == CONDITION_POSITIVE ){
                machine.pc = op1;
            }
            break;
        case OPERATION_beq:
            if(machine.conditions == CONDITION_ZERO){
                machine.pc = op1;
            }
            break;
        case OPERATION_blt:
            if(machine.conditions == CONDITION_NEGATIVE){
                machine.pc = op1;
            }
            break;
        case OPERATION_bgt:
            if(machine.conditions == CONDITION_POSITIVE){
                machine.pc = op1;
            }
            break;
        case OPERATION_ble:
        if(machine.conditions == CONDITION_NEGATIVE|| machine.conditions == CONDITION_ZERO){
                machine.pc = op1;
            }
            break;
        case OPERATION_bge:
        if(machine.conditions == CONDITION_POSITIVE || machine.conditions == CONDITION_ZERO){
                machine.pc = op1;
            }
            break;
    }
}

//holds logic for the return operation
void execute_return(){
    machine.pc = machine.registers[30];
}



//find how many zeros are at the start of a uint_32 number
int count_leading_zeros32(uint32_t num){
    uint32_t copy = num;
    int ct = 0;
    if(copy == 0){return HALFWORD_SIZE_BITS;}

    while(copy != 0){
        ct++;
        copy >>=1;
    }
    return HALFWORD_SIZE_BITS-ct;

//find how many zeros are at the start of a uint_64 number
}int count_leading_zeros64(uint64_t num){
    uint64_t copy = num;
    int ct = 0;
    if(copy == 0){return WORD_SIZE_BITS;}

    while(copy != 0){
        ct++;
        copy >>=1;
    }
    return WORD_SIZE_BITS-ct;
}


//execute operations based on counting lead bits 
void execute_count_lead(struct instruction_t instruction){
    uint64_t found;
    switch(instruction.operation){
        case OPERATION_cls:
            if(instruction.operands[1].reg_type == REGISTER_w){
                uint32_t op1 = get_value(instruction.operands[1]);
                int lead = op1 >> (HALFWORD_SIZE_BITS-1)  & 1;
                if(lead){
                    op1 = ~op1;
                }
                found = count_leading_zeros32(op1)-1;
            }
            else{
                uint64_t op1 = get_value(instruction.operands[1]);
                int lead = op1 >> (WORD_SIZE_BITS-1)  & 1;
                if(lead){
                    op1 = ~op1;
                }
                found = count_leading_zeros64(op1)-1;
            }
            
                break;
        case OPERATION_clz:
            if(instruction.operands[1].reg_type == REGISTER_w){
                uint32_t op1 = get_value(instruction.operands[1]);
                found = count_leading_zeros32(op1);
            }
            else{
                uint64_t op1 = get_value(instruction.operands[1]);
                found = count_leading_zeros64(op1);
            }

            break;

    }
    put_value(instruction.operands[0],found);

}

/*
 * Execute an instruction
 */
void execute(struct instruction_t instruction) {
    switch(instruction.operation) {
    case OPERATION_add:
    case OPERATION_sub:
    case OPERATION_subs:
    case OPERATION_mul:
    case OPERATION_sdiv:
    case OPERATION_udiv:
    case OPERATION_neg:
        execute_arithmetic(instruction);
        break;
    case OPERATION_lsl:
    case OPERATION_lsr:
    case OPERATION_and:
    case OPERATION_orr:
    case OPERATION_eor:
    case OPERATION_mvn:
        execute_bitwise(instruction);
        break;
    case OPERATION_mov:
    case OPERATION_str:
    case OPERATION_ldr:
        execute_MSL(instruction);
        break;
    case OPERATION_cmp:
        execute_compare(instruction);
        break;
    case OPERATION_b:
    case OPERATION_bl:
        execute_basic_branch(instruction);
        break;
    case OPERATION_bne:
    case OPERATION_beq:
    case OPERATION_blt:
    case OPERATION_bgt:
    case OPERATION_ble:
    case OPERATION_bge:
        execute_conditional_branch(instruction);
        break;
    case OPERATION_ret:
        execute_return();
        break;
    case OPERATION_nop:
        break;
    case OPERATION_clz:
    case OPERATION_cls:
        execute_count_lead(instruction);
        break;
    default:
        printf("!!Instruction not implemented!!\n");
    }
}