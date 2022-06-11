#include <SDL2/SDL.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>


#define VIRTUAL_SCREEN_WIDTH 64
#define VIRTUAL_SCREEN_HEIGHT 32
#define MEMORY_SIZE 0x1000
#define STACK_SIZE 16
#define WINDOW_WIDTH 640
#define WINDOW_HEIGHT 320
#define SCREEN_COLOR 0xff05c714
#define FONTSET_SIZE 80
#define FONTSET_MEMORY_OFFSET 0x50
#define VF 0xF
#define INSTRUCTION_SIZE 2
#define LOAD_ADDRESS 0x200

const unsigned char fontset[FONTSET_SIZE] = {
        0xF0, 0x90, 0x90, 0x90, 0xF0,		// 0
        0x20, 0x60, 0x20, 0x20, 0x70,		// 1
        0xF0, 0x10, 0xF0, 0x80, 0xF0,		// 2
        0xF0, 0x10, 0xF0, 0x10, 0xF0,		// 3
        0x90, 0x90, 0xF0, 0x10, 0x10,		// 4
        0xF0, 0x80, 0xF0, 0x10, 0xF0,		// 5
        0xF0, 0x80, 0xF0, 0x90, 0xF0,		// 6
        0xF0, 0x10, 0x20, 0x40, 0x40,		// 7
        0xF0, 0x90, 0xF0, 0x90, 0xF0,		// 8
        0xF0, 0x90, 0xF0, 0x10, 0xF0,		// 9
        0xF0, 0x90, 0xF0, 0x90, 0x90,		// A
        0xE0, 0x90, 0xE0, 0x90, 0xE0,		// B
        0xF0, 0x80, 0x80, 0x80, 0xF0,		// C
        0xE0, 0x90, 0x90, 0x90, 0xE0,		// D
        0xF0, 0x80, 0xF0, 0x80, 0xF0,		// E
        0xF0, 0x80, 0xF0, 0x80, 0x80		// F
};

typedef struct{
    uint8_t memory[MEMORY_SIZE];
    uint32_t pixels[VIRTUAL_SCREEN_HEIGHT][VIRTUAL_SCREEN_WIDTH];
    uint16_t stack[STACK_SIZE];
    uint8_t registers[16];
    uint16_t address_register;
    uint16_t program_counter;
    uint8_t keys[16];
    uint8_t stack_pos;
    uint8_t delay_timer;
    uint8_t sound_timer;
    bool waiting_for_key;
    uint8_t key_target_reg;
}chip_8;


void init_chip_8(chip_8* c){
    memset(c->memory, 0, MEMORY_SIZE * sizeof(c->memory[0]));
    memcpy(c->memory + FONTSET_MEMORY_OFFSET, fontset, FONTSET_SIZE * sizeof(fontset[0]));
    memset(c->pixels, 0, VIRTUAL_SCREEN_HEIGHT * VIRTUAL_SCREEN_WIDTH * sizeof(c->pixels[0][0]));
    memset(c->stack, 0, STACK_SIZE * sizeof(c->stack[0]));
    memset(c->registers, 0, 16 * sizeof(c->registers[0]));
    memset(c->keys, 0, 16 * sizeof(c->keys[0]));
    c->address_register = 0;
    c->program_counter = LOAD_ADDRESS;
    c->stack_pos = 0;
    
    c->delay_timer = 0;
    c->sound_timer = 0;
    
    c->waiting_for_key = false;
    c->key_target_reg = 0;
}

bool load_program(chip_8* c, const char* filename){
    FILE* fp = fopen(filename, "rb");
    if(!fp){
        fprintf(stderr, "could not open file!!!\n");
        return false;
    }
    fseek(fp, 0L, SEEK_END);
    int size = ftell(fp);
    rewind(fp);
    if(size > MEMORY_SIZE - LOAD_ADDRESS){
        fprintf(stderr, "file too large!!!\n");
        return false;
    }
    fread(c->memory + LOAD_ADDRESS, 1, size, fp);
    fclose(fp);
    return true;
}

uint16_t get_current_instruction(chip_8* c){
    uint8_t hi = c->memory[c->program_counter];
    uint8_t lo = c->memory[c->program_counter+1];
    uint16_t full = (hi << 8) | lo;
    return full;
}

uint16_t get_next_instruction(chip_8* c){
    uint8_t hi = c->memory[c->program_counter+2];
    uint8_t lo = c->memory[c->program_counter+3];
    uint16_t full = (hi << 8) | lo;
    return full;
}

void push(chip_8* c, uint16_t value){
    if(c->stack_pos+1 >= STACK_SIZE-1){
        fprintf(stderr, "stack overflow!!!");
        return;
    }
    
    c->stack_pos++;
    c->stack[c->stack_pos] = value;
}

uint16_t pop(chip_8* c){
    if(c->stack_pos == 0){
        fprintf(stderr, "stack underflow!!!");
        return -1;
    }
    uint16_t result = c->stack[c->stack_pos];
    c->stack_pos--;
    return result;
}

void clear_screen(chip_8* c){
    memset(c->pixels, 0, VIRTUAL_SCREEN_HEIGHT * VIRTUAL_SCREEN_WIDTH * sizeof(c->pixels[0][0]));
    c->program_counter += INSTRUCTION_SIZE;
}

void return_from_subroutine(chip_8* c){
    c->program_counter = pop(c);
}

void goto_address(chip_8* c, uint16_t address){
    c->program_counter = address;
}

void call_subroutine(chip_8* c, uint16_t address){
    push(c, c->program_counter + INSTRUCTION_SIZE);
    goto_address(c, address);
}

void skip_equal(chip_8* c, uint8_t reg, uint8_t val){
    if(c->registers[reg] == val){
        c->program_counter += 2 * INSTRUCTION_SIZE;
    }
    else{
        c->program_counter += INSTRUCTION_SIZE;
    }
}

void skip_not_equal(chip_8* c, uint8_t reg, uint8_t val){
    if(c->registers[reg] != val){
        c->program_counter += 2 * INSTRUCTION_SIZE;
    }
    else{
        c->program_counter += INSTRUCTION_SIZE;
    }
}

void skip_equal_reg(chip_8* c, uint8_t reg1, uint8_t reg2){
    if(c->registers[reg1] == c->registers[reg2]){
        c->program_counter += 2 * INSTRUCTION_SIZE;
    }
    else{
        c->program_counter += INSTRUCTION_SIZE;
    }
}

void load_imm(chip_8* c, uint8_t reg, uint8_t val){
    c->registers[reg] = val;
    c->program_counter += INSTRUCTION_SIZE;
}

void add_imm(chip_8* c, uint8_t reg, uint8_t val){
    c->registers[reg] += val;
    c->program_counter += INSTRUCTION_SIZE;
}

void mov(chip_8* c, uint8_t reg1, uint8_t reg2){
    c->registers[reg1] = c->registers[reg2];
    c->program_counter += INSTRUCTION_SIZE;
}

void bit_or(chip_8* c, uint8_t reg1, uint8_t reg2){
    c->registers[reg1] |= c->registers[reg2];
    c->program_counter += INSTRUCTION_SIZE;
}

void bit_and(chip_8* c, uint8_t reg1, uint8_t reg2){
    c->registers[reg1] &= c->registers[reg2];
    c->program_counter += INSTRUCTION_SIZE;
}

void bit_xor(chip_8* c, uint8_t reg1, uint8_t reg2){
    c->registers[reg1] ^= c->registers[reg2];
    c->program_counter += INSTRUCTION_SIZE;
}

void add_reg(chip_8* c, uint8_t reg1, uint8_t reg2){
    int a = c->registers[reg1];
    int b = c->registers[reg2];
    int res = a + b;
    c->registers[VF] = res > 0xff;
    c->registers[reg1] = res & 0xff;
    c->program_counter += INSTRUCTION_SIZE;
}

void sub_reg(chip_8* c, uint8_t reg1, uint8_t reg2){
    int a = c->registers[reg1];
    int b = c->registers[reg2];
    int res = a - b;
    c->registers[VF] = res >= 0;
    c->registers[reg1] = res & 0xff;
    c->program_counter += INSTRUCTION_SIZE;
}

void shift_right(chip_8* c, uint8_t reg){
    c->registers[VF] = (c->registers[reg]) & 1;
    (c->registers[reg]) >>= 1;
    c->program_counter += INSTRUCTION_SIZE;
}

void sub_reg_switch(chip_8* c, uint8_t reg1, uint8_t reg2){
    int a = c->registers[reg1];
    int b = c->registers[reg2];
    int res = b - a;
    c->registers[VF] = res >= 0;
    c->registers[reg1] = res & 0xff;
    c->program_counter += INSTRUCTION_SIZE;
}


void shift_left(chip_8* c, uint8_t reg){
    c->registers[VF] = ((c->registers[reg]) >> 7) & 1;
    (c->registers[reg]) <<= 1;
    c->program_counter += INSTRUCTION_SIZE;
}


void skip_not_equal_reg(chip_8* c, uint8_t reg1, uint8_t reg2){
    if(c->registers[reg1] != c->registers[reg2]){
        c->program_counter += 2 * INSTRUCTION_SIZE;
    }
    else{
        c->program_counter += INSTRUCTION_SIZE;
    }
}

void set_address_reg(chip_8* c, uint16_t val){
    c->address_register = val;
    c->program_counter += INSTRUCTION_SIZE;
}


void goto_address_plus_V0(chip_8* c, uint16_t address){
    c->program_counter = address + c->registers[0];
}

void rand_mod(chip_8* c, uint8_t reg, uint8_t m){
    c->registers[reg] = rand() & m;
    c->program_counter += INSTRUCTION_SIZE;
}

void draw_sprite(chip_8* c, uint8_t reg1, uint8_t reg2, uint8_t n){
    int x_start = c->registers[reg1];
    int y_start = c->registers[reg2];
    c->registers[VF] = 0;
    for(int y = 0; y < n; y++){
        for(int x = 0; x < 8; x++){
            int y_pos = (y_start + y) % VIRTUAL_SCREEN_HEIGHT;
            int x_pos = (x_start + x) % VIRTUAL_SCREEN_WIDTH;
            uint32_t old_color = c->pixels[y_pos][x_pos];
            uint8_t bit_value = ((c->memory[c->address_register + y]) >> (7-x)) & 1;
            uint32_t draw_color = bit_value ? SCREEN_COLOR : 0;
            uint32_t new_color = old_color ^ draw_color;
            if(old_color != 0 && draw_color != 0){
                c->registers[VF] = 1; //collision
            }
            c->pixels[y_pos][x_pos] = new_color;
        }
    }
    c->program_counter += INSTRUCTION_SIZE;
}

void skip_if_key_pressed(chip_8* c, uint8_t reg){
    if(c->keys[c->registers[reg]] == 1){
        c->program_counter += 2 * INSTRUCTION_SIZE;
    }
    else{
        c->program_counter += INSTRUCTION_SIZE;
    }
}

void skip_if_key_not_pressed(chip_8* c, uint8_t reg){
    if(c->keys[c->registers[reg]] == 0){
        c->program_counter += 2 * INSTRUCTION_SIZE;
    }
    else{
        c->program_counter += INSTRUCTION_SIZE;
    }
}

void get_delay(chip_8* c, uint8_t reg){
    c->registers[reg] = c->delay_timer;
    c->program_counter += INSTRUCTION_SIZE;
}

void wait_for_key(chip_8* c, uint8_t reg){
    c->waiting_for_key = true;
    c->key_target_reg = reg;
    c->program_counter += INSTRUCTION_SIZE;
}

void key_event(chip_8* c, uint8_t key){
    c->registers[c->key_target_reg] = key;
    c->waiting_for_key = false;
}

void set_delay(chip_8* c, uint8_t reg){
    c->delay_timer = c->registers[reg];
    c->program_counter += INSTRUCTION_SIZE;
}

void set_sound(chip_8* c, uint8_t reg){
    c->sound_timer = c->registers[reg];
    c->program_counter += INSTRUCTION_SIZE;
}

void add_address_reg(chip_8* c, uint8_t reg){
    c->address_register += c->registers[reg];
    c->program_counter += INSTRUCTION_SIZE;
}

void set_font_char(chip_8* c, uint8_t reg){
    c->address_register = FONTSET_MEMORY_OFFSET + c->registers[reg] * 5;
    c->program_counter += INSTRUCTION_SIZE;
}

void set_bcd(chip_8* c, uint8_t reg){
    int num = c->registers[reg];
    int ones = num % 10;
    int tens = (num / 10) % 10;
    int huns = (num / 100) % 10;
    c->memory[c->address_register + 0] = huns & 0xff;
    c->memory[c->address_register + 1] = tens & 0xff;
    c->memory[c->address_register + 2] = ones & 0xff;
    c->program_counter += INSTRUCTION_SIZE;
}

void reg_dump(chip_8* c, uint8_t reg){
    for(int i = 0; i <= reg; i++){
        c->memory[c->address_register + i] = c->registers[i];
    }
    c->program_counter += INSTRUCTION_SIZE;
}

void reg_load(chip_8* c, uint8_t reg){
    for(int i = 0; i <= reg; i++){
        c->registers[i] = c->memory[c->address_register + i];
    }
    c->program_counter += INSTRUCTION_SIZE;
}

void not_implemented(chip_8* c, uint16_t instruction){
    fprintf(stderr, "INSTRUCTION, NOT IMPLEMENTED!!! 0x%04x\n", instruction);
    exit(1);
}
void debug_decode(uint16_t instr){
    uint16_t full = instr;
    uint8_t hi = instr >> 8;
    uint8_t lo = instr & 0xff;
    switch(hi >> 4){
        case 0x0:
            if(full == 0x00E0){
                printf("clear_screen");
            }
            else if(full == 0x00EE){
                printf("return_from_subroutine");
            }
            else{
                printf("not_implemented");
            }
            break;
        case 0x1:
            printf("goto_address 0x%04x", full & 0xfff);
            break;
        case 0x2:
            printf("call_subroutine 0x%04x", full & 0xfff);
            break;
        case 0x3:
            printf("skip_equal 0x%02x 0x%02x", hi & 0xf, lo);
            break;
        case 0x4:
            printf("skip_not_equal 0x%02x 0x%02x", hi & 0xf, lo);
            break;
        case 0x5:
            printf("skip_equal_reg 0x%02x 0x%02x", hi & 0xf, lo);
            break;
        case 0x6:
            printf("load_imm 0x%02x 0x%02x", hi & 0xf, lo);
            break;
        case 0x7:
            printf("add_imm 0x%02x 0x%02x", hi & 0xf, lo);
            break;
        case 0x8:
            if((lo & 0xf) == 0){
                printf("mov 0x%02x 0x%02x", hi & 0xf, lo >> 4);
            }
            else if((lo & 0xf) == 1){
                printf("bit_or 0x%02x 0x%02x", hi & 0xf, lo >> 4);
            }
            else if((lo & 0xf) == 2){
                printf("bit_and 0x%02x 0x%02x", hi & 0xf, lo >> 4);
            }
            else if((lo & 0xf) == 3){
                printf("bit_xor 0x%02x 0x%02x", hi & 0xf, lo >> 4);
            }
            else if((lo & 0xf) == 4){
                printf("add_reg 0x%02x 0x%02x", hi & 0xf, lo >> 4);
            }
            else if((lo & 0xf) == 5){
                printf("sub_reg 0x%02x 0x%02x", hi & 0xf, lo >> 4);
            }
            else if((lo & 0xf) == 6){
                printf("shift_right 0x%02x", hi & 0xf);
            }
            else if((lo & 0xf) == 7){
                printf("sub_reg_switch 0x%02x 0x%02x", hi & 0xf, lo >> 4);
            }
            else if((lo & 0xf) == 0xe){
                printf("shift_left 0x%02x", hi & 0xf);
            }
            else{
                printf("not_implemented");
            }
            break;
        case 0x9:
            printf("skip_not_equal_reg 0x%02x 0x%02x", hi & 0xf, lo >> 4);
            break;
        case 0xa:
            printf("set_address_reg 0x%04x", full & 0xfff);
            break;
        case 0xb:
            printf("goto_address_plus_V0 0x%04x", full & 0xfff);
            break;
        case 0xc:
            printf("rand_mod 0x%02x 0x%02x", hi & 0xf, lo);
            break;
        case 0xd:
            printf("draw_sprite 0x%02x 0x%02x 0x%02x", hi & 0xf, lo >> 4, lo & 0xf);
            break;
        case 0xe:
            if(lo == 0x9e){
                printf("skip_if_key_pressed 0x%02x", hi & 0xf);
            }
            else if(lo == 0xa1){
                printf("skip_if_key_not_pressed 0x%02x", hi & 0xf);
            }
            else{
                printf("not_implemented");
            }
            break;
        case 0xf:
            if(lo == 0x07){
                printf("get_delay 0x%02x", hi & 0xf);
            }
            else if(lo == 0x0a){
                printf("wait_for_key 0x%02x", hi & 0xf);
            }
            else if(lo == 0x15){
                printf("set_delay 0x%02x", hi & 0xf);
            }
            else if(lo == 0x18){
                printf("set_sound 0x%02x", hi & 0xf);
            }
            else if(lo == 0x1e){
                printf("add_address_reg 0x%02x", hi & 0xf);
            }
            else if(lo == 0x29){
                printf("set_font_char 0x%02x", hi & 0xf);
            }
            else if(lo == 0x33){
                printf("set_bcd 0x%02x", hi & 0xf);
            }
            else if(lo == 0x55){
                printf("reg_dump 0x%02x", hi & 0xf);
            }
            else if(lo == 0x65){
                printf("reg_load 0x%02x", hi & 0xf);
            }
            else{
                printf("not_implemented");
            }
            break;
        default:
            printf("not_implemented");
    }
}


int step(chip_8* c){
    bool continue_exec = false;
    if(c->waiting_for_key){
        for(int i = 0; i < 16; i++){
            if(c->keys[i] != 0){
                key_event(c, i);
                continue_exec = true;
                break;
            }
        }
    }
    else{
        continue_exec = true;
    }
    if(!continue_exec) return 1;
    uint8_t hi = c->memory[c->program_counter];
    uint8_t lo = c->memory[c->program_counter+1];
    uint16_t full = (hi << 8) | lo;
    switch(hi >> 4){
        case 0x0:
            if(full == 0x00E0){
                clear_screen(c);
            }
            else if(full == 0x00EE){
                return_from_subroutine(c);
            }
            else{
                not_implemented(c, full);
            }
            break;
        case 0x1:
            goto_address(c, full & 0xfff);
            break;
        case 0x2:
            call_subroutine(c, full & 0xfff);
            break;
        case 0x3:
            skip_equal(c, hi & 0xf, lo);
            break;
        case 0x4:
            skip_not_equal(c, hi & 0xf, lo);
            break;
        case 0x5:
            skip_equal_reg(c, hi & 0xf, lo >> 4);
            break;
        case 0x6:
            load_imm(c, hi & 0xf, lo);
            break;
        case 0x7:
            add_imm(c, hi & 0xf, lo);
            break;
        case 0x8:
            if((lo & 0xf) == 0){
                mov(c, hi & 0xf, lo >> 4);
            }
            else if((lo & 0xf) == 1){
                bit_or(c, hi & 0xf, lo >> 4);
            }
            else if((lo & 0xf) == 2){
                bit_and(c, hi & 0xf, lo >> 4);
            }
            else if((lo & 0xf) == 3){
                bit_xor(c, hi & 0xf, lo >> 4);
            }
            else if((lo & 0xf) == 4){
                add_reg(c, hi & 0xf, lo >> 4);
            }
            else if((lo & 0xf) == 5){
                sub_reg(c, hi & 0xf, lo >> 4);
            }
            else if((lo & 0xf) == 6){
                shift_right(c, hi & 0xf);
            }
            else if((lo & 0xf) == 7){
                sub_reg_switch(c, hi & 0xf, lo >> 4);
            }
            else if((lo & 0xf) == 0xe){
                shift_left(c, hi & 0xf);
            }
            else{
                not_implemented(c, full);
            }
            break;
        case 0x9:
            skip_not_equal_reg(c, hi & 0xf, lo >> 4);
            break;
        case 0xa:
            set_address_reg(c, full & 0xfff);
            break;
        case 0xb:
            goto_address_plus_V0(c, full & 0xfff);
            break;
        case 0xc:
            rand_mod(c, hi & 0xf, lo);
            break;
        case 0xd:
            draw_sprite(c, hi & 0xf, lo >> 4, lo & 0xf);
            return 2;
            break;
        case 0xe:
            if(lo == 0x9e){
                skip_if_key_pressed(c, hi & 0xf);
            }
            else if(lo == 0xa1){
                skip_if_key_not_pressed(c, hi & 0xf);
            }
            else{
                not_implemented(c, full);
            }
            break;
        case 0xf:
            if(lo == 0x07){
                get_delay(c, hi & 0xf);
            }
            else if(lo == 0x0a){
                wait_for_key(c, hi & 0xf);
            }
            else if(lo == 0x15){
                set_delay(c, hi & 0xf);
            }
            else if(lo == 0x18){
                set_sound(c, hi & 0xf);
            }
            else if(lo == 0x1e){
                add_address_reg(c, hi & 0xf);
            }
            else if(lo == 0x29){
                set_font_char(c, hi & 0xf);
            }
            else if(lo == 0x33){
                set_bcd(c, hi & 0xf);
            }
            else if(lo == 0x55){
                reg_dump(c, hi & 0xf);
            }
            else if(lo == 0x65){
                reg_load(c, hi & 0xf);
            }
            else{
                not_implemented(c, full);
            }
            break;
        default:
            not_implemented(c, full);
    }
    return 0;
}

void print_debug(chip_8* c){
    printf("registers:\n");
    for(int i = 0; i < 16; i++){
        if(i % 4 == 0 && i != 0){
            printf("\n");
        }
        printf("reg %02x: 0x%02x  ", i, c->registers[i]);
    }
    printf("\n");
    printf("address register: 0x%04x\n", c->address_register);
    printf("mem at address register:\n");
    for(int i = 0; i < 3; i++){
        printf("0x%02x ", c->memory[c->address_register + i]);
    }
    printf("\n");
    printf("program counter: 0x%04x\n", c->program_counter);
    printf("next instructions:\n");
    for(int i = 0; i < 16; i++){
        uint8_t hi = c->memory[c->program_counter + 2*i];
        uint8_t lo = c->memory[c->program_counter + 2*i+1];
        uint16_t full = (hi << 8) | lo;
        printf("0x%04x ", full);
    }
    printf("\n");
    uint8_t hi = c->memory[c->program_counter];
    uint8_t lo = c->memory[c->program_counter + 1];
    uint16_t full = (hi << 8) | lo;
    debug_decode(full);
    printf("\nkeys:\n");
    for(int i = 0; i < 16; i++){
        printf("key %02d: %d  ", i, c->keys[i]);
    }
    printf("\n");
    printf("stack pos: %d\n", c->stack_pos);
    printf("waiting for key: %d\n", c->waiting_for_key);
    printf("\n\n");
}

typedef struct{
    chip_8* chip;
    uint16_t previous_instruction;
    uint16_t current_instruction;
    uint16_t next_instruction;
    uint16_t breakpoints[10];
    uint16_t no_breakpoints;
    uint16_t break_instructions[10];
    uint16_t no_break_instructions;
    uint16_t break_address_reg[10];
    uint16_t no_break_addres_regs;
}debugger;

void init_debugger(debugger* d, chip_8* c){
    d->chip = c;
    d->previous_instruction = 0;
    d->current_instruction = get_current_instruction(c);
    d->next_instruction = get_next_instruction(c);
    memset(d->breakpoints, 0, 10 * sizeof(uint16_t));
    d->no_breakpoints = 0;
    memset(d->break_instructions, 0, 10 * sizeof(uint16_t));
    d->no_break_instructions = 0;
    memset(d->break_address_reg, 0, 10 * sizeof(uint16_t));
    d->no_break_addres_regs = 0;
}

void add_debug_instruction(debugger* d, uint16_t instruction){
    d->break_instructions[d->no_break_instructions] = instruction;
    d->no_break_instructions++;
}

void add_break_address_reg(debugger* d, uint16_t address){
    d->break_address_reg[d->no_break_addres_regs] = address;
    d->no_break_addres_regs++;
}

int debug_step(debugger* d){
    for(int i = 0; i < d->no_breakpoints; i++){
        if(d->chip->program_counter == d->breakpoints[i]){
            print_debug(d->chip);
        }
    }
    for(int i = 0; i < d->no_break_instructions; i++){
        if((get_current_instruction(d->chip) & d->break_instructions[i]) == d->break_instructions[i]){
            print_debug(d->chip);
        }
    }
    for(int i = 0; i < d->no_break_addres_regs; i++){
        if(d->chip->address_register == d->break_address_reg[i]){
            print_debug(d->chip);
        }
    }
    int res = step(d->chip);
    d->previous_instruction = d->current_instruction;
    d->current_instruction = d->next_instruction;
    d->next_instruction = get_current_instruction(d->chip);
    return res;
}

int main(int argc, char** argv) {
    if(argc != 2){
        fprintf(stderr, "./program romfile\n");
        return 1;
    }
    
    const float target_frametime = 1.0f/60.0f;
    
	if (SDL_Init(SDL_INIT_EVERYTHING) != 0) {
		fprintf(stderr, "SDL_Init Error: %s\n", SDL_GetError());
        goto cleanup_end;
	}

	SDL_Window* win = SDL_CreateWindow("Hello World!", 0, 0, WINDOW_WIDTH, WINDOW_HEIGHT, SDL_WINDOW_SHOWN);
	if (win == NULL) {
		fprintf(stderr, "SDL_CreateWindow Error: %s\n", SDL_GetError());
		goto cleanup_end;
	}

	SDL_Renderer* ren = SDL_CreateRenderer(win, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
	if (ren == NULL) {
		fprintf(stderr, "SDL_CreateRenderer Error: %s\n", SDL_GetError());
		goto cleanup_window;
	}
    
    SDL_Texture* virtual_screen = SDL_CreateTexture(ren, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STATIC, VIRTUAL_SCREEN_WIDTH, VIRTUAL_SCREEN_HEIGHT);
	if (virtual_screen == NULL) {
		fprintf(stderr, "SDL_CreateTexture Error: %s\n", SDL_GetError());
		goto cleanup_renderer;
	}
    
    chip_8 chip;
    init_chip_8(&chip);
    debugger debug;
    init_debugger(&debug, &chip);
    
    //add_debug_instruction(&debug, 0xf065);
    add_break_address_reg(&debug, 0x0202);
    const char* filename = argv[1];
    if(!load_program(&chip, filename)){
        goto cleanup_texture;
    }
    
    SDL_Rect target_rect = {0, 0, WINDOW_WIDTH, WINDOW_HEIGHT}; //used to scale the texture
	
    uint64_t last_time = SDL_GetPerformanceCounter();
    float dt = 0.0f;
    bool running = true;
	while(running) {
        SDL_Event e;
        while(SDL_PollEvent(&e) != 0) {
            switch(e.type) {
                case SDL_QUIT:
                    running = false;
                    break;
                default:
                    break;
            }
        }
        
        uint64_t new_time = SDL_GetPerformanceCounter();
        float diff = new_time - last_time;
        diff = diff / (float)(SDL_GetPerformanceFrequency());
        float diff_ms = diff * 1000.0f;
        dt += diff_ms;
        if(dt > 16.666f){
            dt -= 16.666f;
            if(chip.delay_timer > 0) chip.delay_timer--;
            if(chip.sound_timer > 0) chip.sound_timer--;
        }
        last_time = new_time;
        
        const uint8_t *key_state = SDL_GetKeyboardState(NULL);
        chip.keys[0x0] = (bool)key_state[SDL_SCANCODE_X];
        chip.keys[0x1] = (bool)key_state[SDL_SCANCODE_1];
        chip.keys[0x2] = (bool)key_state[SDL_SCANCODE_2] || (bool)key_state[SDL_SCANCODE_UP];
        chip.keys[0x3] = (bool)key_state[SDL_SCANCODE_3];
        chip.keys[0xc] = (bool)key_state[SDL_SCANCODE_4];
        chip.keys[0x4] = (bool)key_state[SDL_SCANCODE_Q] || (bool)key_state[SDL_SCANCODE_LEFT];
        chip.keys[0x5] = (bool)key_state[SDL_SCANCODE_W];
        chip.keys[0x6] = (bool)key_state[SDL_SCANCODE_E] || (bool)key_state[SDL_SCANCODE_RIGHT];
        chip.keys[0xd] = (bool)key_state[SDL_SCANCODE_R];
        chip.keys[0x7] = (bool)key_state[SDL_SCANCODE_A];
        chip.keys[0x8] = (bool)key_state[SDL_SCANCODE_S] || (bool)key_state[SDL_SCANCODE_DOWN];
        chip.keys[0x9] = (bool)key_state[SDL_SCANCODE_D];
        chip.keys[0xe] = (bool)key_state[SDL_SCANCODE_F];
        chip.keys[0xa] = (bool)key_state[SDL_SCANCODE_Z];
        chip.keys[0x0] = (bool)key_state[SDL_SCANCODE_X];
        chip.keys[0xb] = (bool)key_state[SDL_SCANCODE_C];
        chip.keys[0xf] = (bool)key_state[SDL_SCANCODE_V];
        
        int res = step(&chip);
        //int res = debug_step(&debug);
        if(res == 2){
            SDL_SetRenderDrawColor(ren, 0, 0, 0, 0);
            SDL_RenderClear(ren);
            SDL_UpdateTexture(virtual_screen, NULL, chip.pixels, VIRTUAL_SCREEN_WIDTH * sizeof(Uint32));
            SDL_RenderCopy(ren, virtual_screen, NULL, &target_rect);
            SDL_RenderPresent(ren);
        }
        
        //print_debug(&chip);
        
        	
        
        if(diff_ms < target_frametime){
            
            SDL_Delay(target_frametime - diff_ms);
        }
	}
    
    cleanup_texture:
    SDL_DestroyTexture(virtual_screen);
    cleanup_renderer:
	SDL_DestroyRenderer(ren);
    cleanup_window:
	SDL_DestroyWindow(win);
    cleanup_end:
	SDL_Quit();
    
    
}
