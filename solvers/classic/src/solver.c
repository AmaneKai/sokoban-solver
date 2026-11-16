#include "solver.h"

static atomic_bool solution_found;
static char* best_solution;

typedef struct {
	int thread_id;
	int width;
	int height;
	char** map;
	char** items;
} ThreadArgs;

static inline u32 xorshift32(u32* state) {
	u32 x = *state;
	if (x == 0) x = 1;
	x ^= x << 13;
	x ^= x >> 17;
	x ^= x << 5;
	*state = x;
	return x;
}

static void init_context(FastContext* ctx, int w, int h, char** map, char** items, int thread_id) {
	ctx->pool = malloc(sizeof(SokoState) * STATE_POOL_SIZE);
	ctx->hash_table = calloc(HASH_SIZE, sizeof(u32));
	ctx->pq = malloc(sizeof(u32) * STATE_POOL_SIZE);

	if (!ctx->pool || !ctx->hash_table || !ctx->pq) 
		return;

	ctx->pool_ptr = 1; 
	ctx->pq_size = 0;
	ctx->visited_seq = 0;
	memset(ctx->visited, 0, sizeof(ctx->visited));
	memset(ctx->is_box, 0, sizeof(ctx->is_box));
	memset(ctx->is_goal, 0, sizeof(ctx->is_goal));

	ctx->W = w + 4;
	ctx->H = h + 4;
	ctx->area = ctx->W * ctx->H;
	ctx->box_count = 0;
	ctx->goal_count = 0;

	ctx->prng_state = 123456789 + thread_id * 1337;

	ctx->dirs[0] = -ctx->W; // Up
	ctx->dirs[1] = ctx->W;  // Down
	ctx->dirs[2] = -1;      // Left
	ctx->dirs[3] = 1;       // Right

	for (int i = 0; i < ctx->area; i++) 
		ctx->map[i] = '#';

	for (int r = 0; r < h; r++) {
		for (int c = 0; c < w; c++) {
			int idx = (r + 2) * ctx->W + (c + 2);
			char m = map[r][c];
			char it = items[r][c];

			ctx->map[idx] = m;

			if (m == '.' || m == '*' || it == '*' || it == '.') {
				ctx->is_goal[idx] = true;
				ctx->map[idx] = ' '; 

				if (ctx->goal_count < MAX_BOXES) {
					ctx->goals[ctx->goal_count++] = idx;
				}
			}

			if (it == '@' || it == '+') {
				ctx->initial_player = idx;
			}

			if (it == '$' || it == '*') {
				if (ctx->box_count < MAX_BOXES) {
					ctx->initial_boxes[ctx->box_count++] = idx;
				}
			}
		}
	}

	u32 z_state = 123456789;

	for (int i = 0; i < ctx->area; i++) {
		u64 h1 = ((u64)xorshift32(&z_state) << 32) | xorshift32(&z_state);
		u64 h2 = ((u64)xorshift32(&z_state) << 32) | xorshift32(&z_state);
		ctx->zobrist_box[i] = h1;
		ctx->zobrist_player[i] = h2;
	}
}

static void free_context(FastContext* ctx) {
	if (ctx->pool) 
		free(ctx->pool);

	if (ctx->hash_table) 
		free(ctx->hash_table);

	if (ctx->pq) 
		free(ctx->pq);

	free(ctx);
}

static void precompute_pull_distances(FastContext* ctx) {
	for (int g = 0; g < ctx->goal_count; g++) {
		for (int i = 0; i < ctx->area; i++) 
			ctx->goal_dist[g][i] = 1000000;

		u16 q[MAX_CELLS];
		int head = 0, tail = 0;

		ctx->goal_dist[g][ctx->goals[g]] = 0;
		q[tail++] = ctx->goals[g];

		while (head < tail) {
			u16 curr = q[head++];

			for (int d = 0; d < 4; d++) {
				u16 from = curr - ctx->dirs[d];
				u16 player_stand = from - ctx->dirs[d];
				if (ctx->map[from] != '#' && ctx->map[player_stand] != '#') {
					if (ctx->goal_dist[g][from] > ctx->goal_dist[g][curr] + 1) {
						ctx->goal_dist[g][from] = ctx->goal_dist[g][curr] + 1;
						q[tail++] = from;
					}
				}
			}
		}
	}

	for (int i = 0; i < ctx->area; i++) {
		int min_d = 1000000;

		for (int g = 0; g < ctx->goal_count; g++) {
			if (ctx->goal_dist[g][i] < min_d) 
				min_d = ctx->goal_dist[g][i];
		}

		ctx->min_dist[i] = min_d;
	}
}

static inline int calc_h(FastContext* ctx, u16* boxes) {
	int h = 0;
	u32 used_g = 0;
	u32 used_b = 0;

	// Pass 1: Perfect matches
	for (int b = 0; b < ctx->box_count; b++) {
		if (ctx->is_goal[boxes[b]]) {
			for (int g = 0; g < ctx->goal_count; g++) {
				if (ctx->goals[g] == boxes[b]) {
					used_g |= (1U << g);
					used_b |= (1U << b);
					break;
				}
			}
		}
	}

	// Pass 2: Global Greedy Assignment (Order-Independent)
	for (int step = 0; step < ctx->box_count; step++) {
		int best_b = -1;
		int best_g = -1;
		int min_d = 1000000;

		for (int b = 0; b < ctx->box_count; b++) {
			if (used_b & (1U << b)) 
				continue;

			for (int g = 0; g < ctx->goal_count; g++) {
				if (used_g & (1U << g)) 
					continue;

				int d = ctx->goal_dist[g][boxes[b]];

				if (d < min_d) {
					min_d = d;
					best_b = b;
					best_g = g;
				}
			}
		}

		if (best_b != -1) {
			used_b |= (1U << best_b);
			used_g |= (1U << best_g);
			h += min_d;
		} else {
			break;
		}
	}

	// Add soft fallback penalty for boxes forced to take dead targets
	for (int b = 0; b < ctx->box_count; b++) {
		if (!(used_b & (1U << b))) {
			int fallback = 1000000;
			for(int g=0; g<ctx->goal_count; g++) {
				if (ctx->goal_dist[g][boxes[b]] < fallback) 
					fallback = ctx->goal_dist[g][boxes[b]];
			}
			h += fallback + 1000; 
		}
	}

	return h;
}

static inline void pq_push(FastContext* ctx, u32 idx) {
	int i = ctx->pq_size++;

	while (i > 0) {
		int p = (i - 1) / 2;
		if (ctx->pool[ctx->pq[p]].priority <= ctx->pool[idx].priority) break;
		ctx->pq[i] = ctx->pq[p];
		i = p;
	}

	ctx->pq[i] = idx;
}

static inline u32 pq_pop(FastContext* ctx) {
	if (ctx->pq_size == 0) 
		return 0;

	u32 ret = ctx->pq[0];
	u32 temp = ctx->pq[--ctx->pq_size];
	int i = 0;

	while (i * 2 + 1 < ctx->pq_size) {
		int child = i * 2 + 1;
		if (child + 1 < ctx->pq_size && 
		    ctx->pool[ctx->pq[child+1]].priority < ctx->pool[ctx->pq[child]].priority) 
			child++;

		if (ctx->pool[temp].priority <= ctx->pool[ctx->pq[child]].priority) 
			break;

		ctx->pq[i] = ctx->pq[child];
		i = child;
	}

	ctx->pq[i] = temp;

	return ret;
}

static inline bool hash_insert(FastContext* ctx, u64 full_hash, u32 pool_idx) {
	u32 slot = full_hash & HASH_MASK;

	while (ctx->hash_table[slot] != 0) {
		u32 existing = ctx->hash_table[slot];

		if (ctx->pool[existing].full_hash == full_hash) {
			bool same = true;

			for(int b = 0; b < ctx->box_count; b++) {
				if(ctx->pool[existing].boxes[b] != ctx->pool[pool_idx].boxes[b]) { 
					same = false; break; 
				}
			}

			if(same) 
				return false;
		}

		slot = (slot + 1) & HASH_MASK;
	}

	ctx->hash_table[slot] = pool_idx;

	return true;
}

static inline bool is_blocked(FastContext* ctx, u16 cell, u16 B, u16 old_B) {
	if (ctx->map[cell] == '#') 
		return true;

	if (cell == B) 
		return true;

	if (cell == old_B) 
		return false;

	return ctx->is_box[cell];
}

static inline bool check_2x2(FastContext* ctx, u16 B, u16 old_B) {
	int dC[] = {-1, 1, -1, 1};
	int dR[] = {-ctx->W, -ctx->W, ctx->W, ctx->W};

	for (int q = 0; q < 4; q++) {
		u16 c1 = B + dC[q], c2 = B + dR[q], c3 = B + dC[q] + dR[q];
		if (is_blocked(ctx, c1, B, old_B) && 
		    is_blocked(ctx, c2, B, old_B) && 
		    is_blocked(ctx, c3, B, old_B)) {
			if (!ctx->is_goal[B]) 
				return true;
			if (is_blocked(ctx, c1, B, old_B) 
			    && ctx->map[c1] != '#' 
			    && !ctx->is_goal[c1]) 
				return true;
			if (is_blocked(ctx, c2, B, old_B) 
			    && ctx->map[c2] != '#' 
			    && !ctx->is_goal[c2]) 
				return true;
			if (is_blocked(ctx, c3, B, old_B) 
			    && ctx->map[c3] != '#' 
			    && !ctx->is_goal[c3]) 
				return true;
		}
	}

	return false;
}

static char* reconstruct_path(FastContext* ctx, u32 final_idx) {
	char* sol = malloc(1000000); 
	if (!sol) return strdup("");
	int sol_len = 0;

	u32* path = malloc(sizeof(u32) * STATE_POOL_SIZE);
	int path_len = 0;
	u32 curr = final_idx;

	while (curr != 1) {
		path[path_len++] = curr;
		curr = ctx->pool[curr].prev_idx;
	}

	for (int i = 0; i < path_len / 2; i++) {
		u32 t = path[i]; path[i] = path[path_len - 1 - i]; path[path_len - 1 - i] = t;
	}

	u16 p_pos = ctx->initial_player;
	memset(ctx->is_box, 0, sizeof(ctx->is_box));

	for (int i = 0; i < ctx->box_count; i++) 
		ctx->is_box[ctx->initial_boxes[i]] = true;

	char walk_c[] = {'u', 'd', 'l', 'r'};
	char push_c[] = {'u', 'd', 'l', 'r'};

	int* w_path = malloc(sizeof(int) * MAX_CELLS);
	int* parent = malloc(sizeof(int) * MAX_CELLS);
	int* move_dir = malloc(sizeof(int) * MAX_CELLS);
	u16* q = malloc(sizeof(u16) * MAX_CELLS);

	for (int step = 0; step < path_len; step++) {
		SokoState* s = &ctx->pool[path[step]];
		u16 target = s->push_from;

		int head = 0, tail = 0;
		ctx->visited_seq++;
		q[tail++] = p_pos;
		ctx->visited[p_pos] = ctx->visited_seq;

		while (head < tail) {
			u16 c = q[head++];
			if (c == target) break;
			for (int d = 0; d < 4; d++) {
				u16 n = c + ctx->dirs[d];
				if (ctx->map[n] != '#' && 
				    !ctx->is_box[n] && ctx->visited[n] != ctx->visited_seq) {
					ctx->visited[n] = ctx->visited_seq;
					parent[n] = c;
					move_dir[n] = d;
					q[tail++] = n;
				}
			}
		}

		int w_len = 0;
		u16 tr = target;
		while (tr != p_pos) {
			w_path[w_len++] = move_dir[tr];
			tr = parent[tr];
		}

		for (int i = w_len - 1; i >= 0; i--) 
			sol[sol_len++] = walk_c[w_path[i]];

		sol[sol_len++] = push_c[s->push_dir];
		p_pos = target + ctx->dirs[s->push_dir];
		ctx->is_box[target + ctx->dirs[s->push_dir]] = false;
		ctx->is_box[target + ctx->dirs[s->push_dir] * 2] = true;
	}

	sol[sol_len] = '\0';

	char* exact_sol = strdup(sol);
	free(sol);
	free(path); free(w_path); free(parent); free(move_dir); free(q);
	return exact_sol;
}

static void solve_sokoban_internal(FastContext* ctx, int thread_id) {
	SokoState* init_state = &ctx->pool[ctx->pool_ptr++];
	init_state->prev_idx = 0;
	init_state->g = 0;
	init_state->hash = 0;
	init_state->priority = 0;
	init_state->switches = 0;
	init_state->last_dest = 0;

	for (int i = 0; i < ctx->box_count; i++) 
		init_state->boxes[i] = ctx->initial_boxes[i];

	for (int k = 1; k < ctx->box_count; k++) {
		u16 key = init_state->boxes[k];
		int j = k - 1;

		while (j >= 0 && init_state->boxes[j] > key) {
			init_state->boxes[j+1] = init_state->boxes[j];
			j--;
		}

		init_state->boxes[j+1] = key;
	}

	for (int i = 0; i < ctx->box_count; i++) {
		init_state->hash ^= ctx->zobrist_box[init_state->boxes[i]];
		ctx->is_box[init_state->boxes[i]] = true;
	}

	u16 n_q[MAX_CELLS];
	int n_head = 0, n_tail = 0;
	u16 norm_p = ctx->initial_player;
	ctx->visited_seq++;
	n_q[n_tail++] = ctx->initial_player;
	ctx->visited[ctx->initial_player] = ctx->visited_seq;

	while (n_head < n_tail) {
		u16 c = n_q[n_head++];
		if (c < norm_p) norm_p = c;
		for (int d = 0; d < 4; d++) {
			u16 n = c + ctx->dirs[d];
			if (ctx->map[n] != '#' && !ctx->is_box[n] && 
			    ctx->visited[n] != ctx->visited_seq) {
				ctx->visited[n] = ctx->visited_seq;
				n_q[n_tail++] = n;
			}
		}
	}

	init_state->full_hash = init_state->hash ^ ctx->zobrist_player[norm_p];
	hash_insert(ctx, init_state->full_hash, 1);
	pq_push(ctx, 1);

	for (int i = 0; i < ctx->box_count; i++) ctx->is_box[init_state->boxes[i]] = false;

	u16 q[MAX_CELLS];
	int iter = 0;

	while (ctx->pq_size > 0) {
		// Fast-path atomic check to cleanly jump out if another thread solved it
		if ((iter++ & 1023) == 0 && 
		    atomic_load_explicit(&solution_found, memory_order_relaxed)) {
			break;
		}

		u32 curr_idx = pq_pop(ctx);
		SokoState* curr = &ctx->pool[curr_idx];

		for (int i = 0; i < ctx->box_count; i++) 
			ctx->is_box[curr->boxes[i]] = true;

		int head = 0, tail = 0;
		u16 p_pos = (curr_idx == 1) ? 
		    ctx->initial_player : 
		    (curr->push_from + ctx->dirs[curr->push_dir]);

		ctx->visited_seq++;
		q[tail++] = p_pos;
		ctx->visited[p_pos] = ctx->visited_seq;
		ctx->walk_dist[p_pos] = 0;

		while (head < tail) {
			u16 c = q[head++];
			for (int d = 0; d < 4; d++) {
				u16 n = c + ctx->dirs[d];
				if (ctx->map[n] != '#' && 
				    !ctx->is_box[n] && 
				    ctx->visited[n] != ctx->visited_seq) {
					ctx->visited[n] = ctx->visited_seq;
					ctx->walk_dist[n] = ctx->walk_dist[c] + 1;
					q[tail++] = n;
				}
			}
		}

		for (int i = 0; i < tail; i++) {
			u16 c = q[i];
			for (int d = 0; d < 4; d++) {
				u16 orig_b_pos = c + ctx->dirs[d];
				if (ctx->is_box[orig_b_pos]) {
					u16 dest = orig_b_pos + ctx->dirs[d];

					if (ctx->map[dest] == '#' || 
					    ctx->is_box[dest] || 
					    ctx->min_dist[dest] >= 1000000) 
						continue;

					if (check_2x2(ctx, dest, orig_b_pos)) 
						continue; 

					if (ctx->pool_ptr >= STATE_POOL_SIZE) 
						goto search_end;

					u32 next_idx = ctx->pool_ptr++;
					SokoState* next = &ctx->pool[next_idx];

					next->prev_idx = curr_idx;
					next->push_from = c;
					next->push_dir = d;
					next->g = curr->g + 1; 
					next->hash = curr->hash ^ 
					    ctx->zobrist_box[orig_b_pos] ^ 
					    ctx->zobrist_box[dest];

					next->last_dest = dest;
					next->switches = curr->switches;
					if (curr->last_dest != 0 && 
					    curr->last_dest != orig_b_pos) {
						next->switches++;
					}

					for (int j = 0; j < ctx->box_count; j++) {
						next->boxes[j] = (curr->boxes[j] == orig_b_pos) ? 
						    dest : 
						    curr->boxes[j];
					}

					for (int k = 1; k < ctx->box_count; k++) {
						u16 key = next->boxes[k];
						int j = k - 1;

						while (j >= 0 && next->boxes[j] > key) {
							next->boxes[j+1] = next->boxes[j];
							j--;
						}

						next->boxes[j+1] = key;
					}

					int h = calc_h(ctx, next->boxes);
					if (h == 0) {
						char* sol = reconstruct_path(ctx, next_idx);
						bool expected = false;

						if (atomic_compare_exchange_strong(&solution_found, &expected, true)) {
							best_solution = sol;
						} else {
							free(sol);
						}

						return;
					}

					ctx->is_box[orig_b_pos] = false;
					ctx->is_box[dest] = true;

					u16 n_q2[MAX_CELLS];
					int n_head2 = 0, n_tail2 = 0;
					u16 norm_p2 = dest - ctx->dirs[d]; 
					ctx->visited_seq++;
					n_q2[n_tail2++] = norm_p2;
					ctx->visited[norm_p2] = ctx->visited_seq;

					while(n_head2 < n_tail2) {
						u16 n_c = n_q2[n_head2++];

						if (n_c < norm_p2) 
							norm_p2 = n_c;

						for(int nd_i = 0; nd_i < 4; nd_i++) {
							u16 n_n = n_c + ctx->dirs[nd_i];
							if (ctx->map[n_n] != '#' && 
							    !ctx->is_box[n_n] && 
							    ctx->visited[n_n] != ctx->visited_seq) {
								ctx->visited[n_n] = ctx->visited_seq;
								n_q2[n_tail2++] = n_n;
							}
						}
					}

					ctx->is_box[orig_b_pos] = true;
					ctx->is_box[dest] = false;

					next->full_hash = next->hash ^ ctx->zobrist_player[norm_p2];
					if (!hash_insert(ctx, next->full_hash, next_idx)) {
						ctx->pool_ptr--;
					} else {
						u64 tier1 = (u64)h * 100000ULL;        
						u64 tier2 = (u64)next->switches * 50000ULL;
						u64 tier3 = (u64)next->g * 1000ULL;
						u64 tier4 = (u64)ctx->walk_dist[c];

						u32 noise = xorshift32(&ctx->prng_state) % 10000;

						// Deep Diverging Portfolio
						switch(thread_id % 8) {
							case 0: tier3 = 0; tier2 = 0; break; // Pure greedy
							case 1: break; // Standard Best First
							case 2: tier2 *= 4; break; // Heavy switch penalty
							case 3: tier1 *= 2; break; // Greedy-leaning A*
							case 4: tier3 = 0; tier1 += noise * 10; break; // Noisy greedy
							case 5: tier1 += noise * 5; tier3 *= 2; break; // Noisy A*
							case 6: tier1 *= 10; tier2 *= 100; tier3 = 0; tier4 = 0; break; // 
							case 7: tier4 *= 10; tier1 += noise * 20; break; // High noise + walk dist
						}

						next->priority = tier1 + tier2 + tier3 + tier4 + (xorshift32(&ctx->prng_state) % 100);
						pq_push(ctx, next_idx);
					}
				}
			}
		}
		for (int i = 0; i < ctx->box_count; i++) 
			ctx->is_box[curr->boxes[i]] = false;
	}

search_end:
	return;
}

void* thread_search(void* arg) {
	ThreadArgs* td = (ThreadArgs*)arg;
	FastContext* ctx = malloc(sizeof(FastContext));
	if (!ctx) return NULL;

	init_context(ctx, td->width, td->height, td->map, td->items, td->thread_id);
	if (!ctx->pool || !ctx->hash_table || !ctx->pq) {
		free_context(ctx);
		return NULL;
	}

	precompute_pull_distances(ctx);

	int init_h = calc_h(ctx, ctx->initial_boxes);
	if (init_h == 0) {
		bool expected = false;
		if (atomic_compare_exchange_strong(&solution_found, &expected, true)) {
			best_solution = strdup("");
		}
		free_context(ctx);
		return NULL;
	}

	bool possible = true;
	for (int i = 0; i < ctx->box_count; i++) {
		if (ctx->min_dist[ctx->initial_boxes[i]] >= 1000000) {
			possible = false;
			break;
		}
	}

	if (possible) {
		solve_sokoban_internal(ctx, td->thread_id);
	}

	free_context(ctx);
	return NULL;
}

#define NUM_THREADS 8

char* solve_sokoban(int width, int height, char** map, char** items) {
	atomic_store(&solution_found, false);
	best_solution = NULL;

	pthread_t threads[NUM_THREADS];
	ThreadArgs args[NUM_THREADS];

	for (int i = 0; i < NUM_THREADS; i++) {
		args[i].thread_id = i;
		args[i].width = width;
		args[i].height = height;
		args[i].map = map;
		args[i].items = items;
		pthread_create(&threads[i], NULL, thread_search, &args[i]);
	}

	for (int i = 0; i < NUM_THREADS; i++) {
		pthread_join(threads[i], NULL);
	}

	if (best_solution) {
		return best_solution; 
	}
	return strdup("");
}
