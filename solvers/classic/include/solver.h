#ifndef SOLVER_H
#define SOLVER_H

#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <pthread.h>
#include <stdatomic.h>

typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;

#define MAX_W 128
#define MAX_H 128
#define MAX_CELLS 17424 
#define MAX_BOXES 32

// 5 Million States per thread (~520MB per thread -> ~4.2GB total for 8 threads)
#define HASH_SIZE (1 << HASH_BITS)
#define HASH_MASK (HASH_SIZE - 1)

#ifdef __APPLE__
    #define STATE_POOL_SIZE 2500000
    #define HASH_BITS 23
#else
    #define STATE_POOL_SIZE 5000000
    #define HASH_BITS 24
#endif

// Neatly aligned 104-byte struct
typedef struct {
    u64 hash;               
    u64 priority;           
    u64 full_hash;          
    u32 prev_idx;           
    int g;                  
    u16 boxes[MAX_BOXES];   
    u16 push_from;          
    u16 last_dest;          
    u16 switches;           
    u8 push_dir;            
    u8 _pad;                
} SokoState;

typedef struct {
    char map[MAX_CELLS];
    bool is_goal[MAX_CELLS];
    int min_dist[MAX_CELLS]; 
    
    int W, H, area;
    int box_count;
    int goal_count;
    u16 initial_player;
    u16 initial_boxes[MAX_BOXES];
    u16 goals[MAX_BOXES];

    int goal_dist[MAX_BOXES][MAX_CELLS];

    u64 zobrist_box[MAX_CELLS];
    u64 zobrist_player[MAX_CELLS];

    SokoState* pool;
    u32 pool_ptr;
    u32* hash_table;
    u32* pq;
    int pq_size;

    u32 visited[MAX_CELLS];
    u32 visited_seq;
    bool is_box[MAX_CELLS];
    int walk_dist[MAX_CELLS];
    
    u32 prng_state;
    int dirs[4];
} FastContext;

char* solve_sokoban(int width, int height, char** map, char** items);

#endif
