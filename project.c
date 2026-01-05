#include <ctype.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>

#define DISPLAY_W    10u
#define DISPLAY_H    10u
#define DISPLAY_SIZE (DISPLAY_W * DISPLAY_H)
#define DISPLAY_BASE 100u

#define GRID_W    DISPLAY_W
#define GRID_H    DISPLAY_H
#define GRID_BASE DISPLAY_BASE

static int IS_EXIT = 0;
static int AAA = 0;

static uint32_t PC = 0x1000;
static uint32_t instruction_memory[0x2000];
static uint32_t data_memory[0x4000];
static uint32_t register_file[32];

static uint32_t RegWrite;
static uint32_t ImmSrc;
static uint32_t ALUSrc;
static uint32_t MemWrite;
static uint32_t ResultSrc;
static uint32_t Branch;
static uint32_t ALUOp;
static uint32_t zero = 0;

static int player_r = 0;
static int player_c = 0;
static int target_r = 5;
static int target_c = 5;
static int game_score = 0;

static int vram_cur_r = 0;
static int vram_cur_c = 0;

static inline uint32_t extractor(uint32_t instr, int lo, int hi);
static inline int32_t sign_extend(uint32_t val, int bits);

static void term_clear_and_home(void) {
    printf("\033[2J\033[H");
    fflush(stdout);
}

static void vram_snapshot(uint32_t out[DISPLAY_SIZE]) {
    for (unsigned i = 0; i < DISPLAY_SIZE; ++i) {
        out[i] = data_memory[DISPLAY_BASE + i] & 0xFFu;
    }
}

static void display_print_header(void) {
    printf("RISC-V 10x10 ASCII Display  (VRAM: data_memory[%u..%u])\n",
           DISPLAY_BASE, DISPLAY_BASE + DISPLAY_SIZE - 1);
    printf("q in keyboard thread still quits program.\n");
    printf("-------------------------------------------------------\n");
}

static void display_print_grid(const uint32_t snapshot[DISPLAY_SIZE]) {
    for (unsigned r = 0; r < DISPLAY_H; ++r) {
        for (unsigned c = 0; c < DISPLAY_W; ++c) {
            uint32_t cell = snapshot[r * DISPLAY_W + c] & 0xFFu;
            char ch = (cell == 0) ? '.' : (isprint((int)cell) ? (char)cell : '?');
            putchar(ch);
        }
        putchar('\n');
    }
    fflush(stdout);
}

void *DISPLAY_THREAD(void *arg) {
    (void)arg;

    uint32_t snapshot[DISPLAY_SIZE];

    vram_snapshot(snapshot);
    term_clear_and_home();
    display_print_header();
    display_print_grid(snapshot);

    while (!IS_EXIT) {
        vram_snapshot(snapshot);
        term_clear_and_home();
        display_print_header();
        display_print_grid(snapshot);
        usleep(100 * 1000);
    }

    return NULL;
}

static void vram_putc(char ch) {
    if (ch == '\n') {
        vram_cur_r++;
        if (vram_cur_r >= (int)DISPLAY_H) {
            vram_cur_r = (int)DISPLAY_H - 1;
        }
        vram_cur_c = 0;
        return;
    }

    if (vram_cur_r < 0 || vram_cur_r >= (int)DISPLAY_H || vram_cur_c < 0 ||
        vram_cur_c >= (int)DISPLAY_W) {
        return;
    }

    data_memory[DISPLAY_BASE + (unsigned)vram_cur_r * DISPLAY_W + (unsigned)vram_cur_c] =
        (uint8_t)ch;

    vram_cur_c++;
    if (vram_cur_c >= (int)DISPLAY_W) {
        vram_cur_c = 0;
        vram_cur_r++;
        if (vram_cur_r >= (int)DISPLAY_H) {
            vram_cur_r = (int)DISPLAY_H - 1;
        }
    }
}

static void vram_puts_line(const char *s) {
    for (const char *p = s; *p; ++p) {
        if (*p == '\n') {
            break;
        }
        vram_putc(*p);
    }
    vram_putc('\n');
}

void Main_Decoder(uint32_t is_op) {
    switch (is_op) {
    case 3:
        RegWrite = 1;
        ImmSrc = 0;
        ALUSrc = 1;
        MemWrite = 0;
        ResultSrc = 1;
        Branch = 0;
        ALUOp = 0;
        break;

    case 35:
        RegWrite = 0;
        ImmSrc = 1;
        ALUSrc = 1;
        MemWrite = 1;
        ResultSrc = 0;
        Branch = 0;
        ALUOp = 0;
        break;

    case 51:
        RegWrite = 1;
        ImmSrc = 0;
        ALUSrc = 0;
        MemWrite = 0;
        ResultSrc = 0;
        Branch = 0;
        ALUOp = 2;
        break;

    case 99:
        RegWrite = 0;
        ImmSrc = 2;
        ALUSrc = 0;
        MemWrite = 0;
        ResultSrc = 0;
        Branch = 1;
        ALUOp = 1;
        break;

    default:
        RegWrite = 0;
        ImmSrc = 0;
        ALUSrc = 0;
        MemWrite = 0;
        ResultSrc = 0;
        Branch = 0;
        ALUOp = 0;
        break;
    }
}

void prerequisites(void) {
    instruction_memory[0x1000] = 0xFFC4A303;
    instruction_memory[0x1004] = 0x0064A423;
    instruction_memory[0x1008] = 0x0062E233;
    instruction_memory[0x100C] = 0xFE420AE3;

    register_file[5] = 6;
    register_file[9] = 0x2004;

    data_memory[0x2000] = 10;
}

static inline uint32_t AluDecoder(uint32_t alu_op, uint32_t instr) {
    uint32_t alu_control = 0;

    if (alu_op == 0) {
        alu_control = 000;
    } else if (alu_op == 1) {
        alu_control = 001;
    } else if (alu_op == 2) {
        uint32_t funct3 = extractor(instr, 12, 14);
        uint32_t funct7 = extractor(instr, 25, 31);
        uint32_t op5 = extractor(instr, 5, 5);
        uint32_t f7 = extractor(funct7, 5, 5);

        switch (funct3) {
        case 010:
            alu_control = 101;
            break;
        case 110:
            alu_control = 011;
            break;
        case 111:
            alu_control = 010;
            break;
        case 000:
            if (f7 == 1 && op5 == 1) {
                alu_control = 001;
            } else {
                alu_control = 000;
            }
            break;
        default:
            break;
        }
    }

    return alu_control;
}

static inline uint32_t Alu(uint32_t inst, int32_t src_a, int32_t src_b, uint32_t alu_control) {
    (void)inst;

    uint32_t result = 0;

    switch (alu_control) {
    case 000:
        result = (uint32_t)(src_a + src_b);
        break;
    case 001:
        result = (uint32_t)(src_a - src_b);
        break;
    case 010:
        result = (uint32_t)(src_a && src_b);
        break;
    case 011:
        result = (uint32_t)(src_a || src_b);
        break;
    case 101:
        result = (src_a < src_b) ? (uint32_t)src_a : (uint32_t)src_b;
        break;
    default:
        break;
    }

    return result;
}

static inline uint32_t extractor(uint32_t instr, int lo, int hi) {
    unsigned width = (unsigned)(hi - lo + 1);
    uint32_t mask = (width >= 32) ? 0xFFFFFFFFu : (uint32_t)((1ull << width) - 1ull);
    return (instr >> lo) & mask;
}

static inline int32_t sign_extend(uint32_t val, int bits) {
    uint32_t m = 1u << (bits - 1);
    return (int32_t)((val ^ m) - m);
}

uint32_t Extend(uint32_t instr, uint8_t imm_src) {
    uint32_t imm = 0;

    switch (imm_src) {
    case 0:
        imm = extractor(instr, 20, 31);
        printf("I type: %d \n", imm);
        break;

    case 1: {
        uint32_t hi = extractor(instr, 25, 31);
        uint32_t lo = extractor(instr, 7, 11);
        uint32_t s = (hi << 5) | lo;
        imm = (uint32_t)sign_extend(s, 12);
        break;
    }

    case 2: {
        uint32_t bit12 = extractor(instr, 31, 31) & 0x1u;
        uint32_t bit11 = extractor(instr, 7, 7) & 0x1u;
        uint32_t bits10_5 = extractor(instr, 30, 25) & 0x3Fu;
        uint32_t bits4_1 = extractor(instr, 11, 8) & 0xFu;

        uint32_t b = (bit12 << 12) | (bit11 << 11) | (bits10_5 << 5) | (bits4_1 << 1);
        imm = (uint32_t)sign_extend(b, 13);
        break;
    }

    default:
        break;
    }

    printf(" before sign check code is 0x%08X\n ", imm);

    uint32_t sign = (imm >> 11) & 1u;
    printf("sign bit is %d\n", sign);

    uint32_t result = imm;
    if (sign == 0) {
        printf("result is %d \n", imm);
    } else {
        result = imm | 0xFFFFF000u;
        printf("result is %d \n", result);
    }

    return result;
}

void CORE(void) {
    uint32_t instr = instruction_memory[PC];
    printf("Instr= %08X\n", instr);

    uint32_t is_op = extractor(instr, 0, 6);
    Main_Decoder(is_op);

    uint32_t ex = Extend(instr, (uint8_t)ImmSrc);
    printf("es is %d \n", ex);

    uint32_t rs1 = extractor(instr, 15, 19);
    printf("rd1 is = x%u 0X%08X\n", rs1, register_file[rs1]);

    uint32_t rs2 = extractor(instr, 20, 24);
    printf("rd2 is = x%u 0X%08X\n", rs2, register_file[rs2]);

    uint32_t src_a = register_file[rs1];
    printf("SrcA is= 0X%08X\n", src_a);

    uint32_t src_b = (ALUSrc == 0) ? register_file[rs2] : ex;
    printf("SrcBs= 0X%08X\n", src_b);

    uint32_t alu_ctl = AluDecoder(ALUOp, instr);
    uint32_t k = Alu(instr, (int32_t)src_a, (int32_t)src_b, alu_ctl);
    printf(" ALU operation result = 0x%08X\n", k);

    if (MemWrite == 1) {
        data_memory[k >> 2] = register_file[rs2];
        printf("Stored 0x%08X from x%u into data_memory[0x%08X]\n", register_file[rs2], rs2, k);
    }

    uint32_t ans = 0;
    if (ResultSrc == 1) {
        ans = data_memory[k >> 2];
        printf("Loaded 0x%08X from data_memory[0x%08X]\n", ans, k);
    }

    uint32_t wd3 = 0;
    if (ResultSrc == 0) {
        wd3 = k;
    } else if (ResultSrc == 1) {
        wd3 = ans;
    }

    if (RegWrite == 1) {
        uint32_t rd = extractor(instr, 7, 11);
        register_file[rd] = wd3;
        printf("Wrote 0x%08X to x%u\n", wd3, rd);
    }

    zero = (k == 0) ? 1u : 0u;

    uint32_t next_pc = PC + 4;
    if (Branch && zero) {
        next_pc = PC + ex;
        printf("Branch taken. New PC = 0x%08X\n", next_pc);
    }

    PC = next_pc;
}

void *SOFT_CPU_THREAD(void *arg) {
    (void)arg;

    printf("Hello from SOFT_CPU_THREAD\n");

    for (int i = 0; i < 5; i++) {
        CORE();
        if (IS_EXIT == 1) {
            return NULL;
        }
    }

    return NULL;
}

static void game_clear_grid(void) {
    for (unsigned i = 0; i < GRID_W * GRID_H; ++i) {
        data_memory[GRID_BASE + i] = 0;
    }
}

static void game_place_target(void) {
    int r, c;

    do {
        r = rand() % (int)GRID_H;
        c = rand() % (int)GRID_W;
    } while (r == player_r && c == player_c);

    target_r = r;
    target_c = c;
}

static void game_draw(void) {
    game_clear_grid();

    data_memory[GRID_BASE + (unsigned)target_r * GRID_W + (unsigned)target_c] = (uint8_t)'X';
    data_memory[GRID_BASE + (unsigned)player_r * GRID_W + (unsigned)player_c] = (uint8_t)'@';
}

static void game_init(void) {
    srand((unsigned)time(NULL));

    player_r = 0;
    player_c = 0;
    game_score = 0;

    game_place_target();
    game_draw();
}

static void game_move_char(char dir) {
    int nr = player_r;
    int nc = player_c;

    if (dir == 'w') {
        nr--;
    } else if (dir == 's') {
        nr++;
    } else if (dir == 'a') {
        nc--;
    } else if (dir == 'd') {
        nc++;
    }

    if (nr < 0) {
        nr = 0;
    }
    if (nr >= (int)GRID_H) {
        nr = (int)GRID_H - 1;
    }
    if (nc < 0) {
        nc = 0;
    }
    if (nc >= (int)GRID_W) {
        nc = (int)GRID_W - 1;
    }

    player_r = nr;
    player_c = nc;

    if (player_r == target_r && player_c == target_c) {
        game_score++;
        printf("Score = %d\n", game_score);
        game_place_target();
    }

    game_draw();
}

void *keyboard_THREAD(void *arg) {
    (void)arg;

    printf("Hello from keyboard_THREAD\n");

    char buf[256];

    while (!IS_EXIT && fgets(buf, sizeof(buf), stdin)) {
        if (buf[0] == 'q' && (buf[1] == '\n' || buf[1] == '\0')) {
            IS_EXIT = 1;
            return NULL;
        }

        if (buf[0] == 'w' || buf[0] == 'a' || buf[0] == 's' || buf[0] == 'd') {
            game_move_char(buf[0]);
            continue;
        }
    }

    return NULL;
}

int main(void) {
    const int NUM_THREADS = 2;

    pthread_t threads[NUM_THREADS];
    pthread_t display_thread;

    prerequisites();
    game_init();

    if (pthread_create(&threads[0], NULL, SOFT_CPU_THREAD, NULL) != 0) {
        perror("Failed to create thread");
        return 1;
    }

    if (pthread_create(&threads[1], NULL, keyboard_THREAD, NULL) != 0) {
        perror("Failed to create thread");
        return 1;
    }

    if (pthread_create(&display_thread, NULL, DISPLAY_THREAD, NULL) != 0) {
        perror("Failed to create display thread");
        return 1;
    }

    for (int i = 0; i < NUM_THREADS; i++) {
        pthread_join(threads[i], NULL);
        printf(">>> Threads %d finished\n", i);
    }

    pthread_join(display_thread, NULL);
    printf(">>> Display thread finished\n");

    printf("All Threads finished!\n");
    return 0;
}
