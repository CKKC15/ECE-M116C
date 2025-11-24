// my_predictor.h
// TAGE Branch Predictor - fast, tuned version

#include <string.h>

// Configuration
#define NUM_TABLES 7
#define BASE_BITS 14               // 16K entries
#define TABLE_BITS 13              // 8K entries per tagged table (was 12)
#define TAG_BITS 10
#define MAX_HIST 200               // max history bits we track

// Geometric-ish history lengths
const int HIST_LEN[NUM_TABLES] = {4, 8, 16, 32, 64, 128, 200};

// TAGE entry
struct tage_entry {
    unsigned int tag : TAG_BITS;
    signed char ctr : 3;           // -4 to 3
    unsigned char u : 2;           // 0..3 usefulness
};

class my_update : public branch_update {
public:
    unsigned int base_idx;
    unsigned int idx[NUM_TABLES];
    unsigned int tag[NUM_TABLES];
    int provider;
    int altpred;
    bool pred;
};

class my_predictor : public branch_predictor {
public:
    my_update u;
    branch_info bi;
    
    // History register - store last 200 bits (LSB = most recent)
    unsigned long long ghist[4];   // 4 * 64 = 256 bits
    
    // Base bimodal predictor
    unsigned char base[1 << BASE_BITS];
    
    // TAGE tables
    tage_entry tables[NUM_TABLES][1 << TABLE_BITS];
    
    unsigned int clock;

    // Dynamic "use alternate on newly allocated" counter (0..15, start neutral)
    unsigned char use_alt_on_na;
    
    my_predictor(void) : clock(0), use_alt_on_na(8) {
        memset(ghist, 0, sizeof(ghist));
        // initialize base predictor to weakly taken (2)
        memset(base, 2, sizeof(base));
        memset(tables, 0, sizeof(tables));
    }

    // Fast, constant-time compression of up to 'len' bits of history into 64 bits.
    // Avoids shifts >= 64 and uses 0..3 history words as needed.
    unsigned long long compress_history(int len) {
        if (len > MAX_HIST) len = MAX_HIST;

        unsigned long long h0 = ghist[0];
        unsigned long long h1 = ghist[1];
        unsigned long long h2 = ghist[2];

        // Case 1: use only the youngest 0..63 bits.
        if (len <= 64) {
            unsigned long long mask = (len == 64) ? ~0ULL : ((len == 0) ? 0ULL : ((1ULL << len) - 1ULL));
            unsigned long long x0 = h0 & mask;
            unsigned long long h = x0 ^ (x0 >> 2) ^ (x0 >> 5);
            h ^= (unsigned long long)len * 0x9e3779b97f4a7c15ULL;
            return h;
        }

        // Case 2: need bits 0..(len-1) with 64 < len <= 128
        if (len <= 128) {
            int k = len - 64; // 1..64
            unsigned long long x0 = h0; // full 64 bits from youngest word
            unsigned long long m1 = (k == 64) ? ~0ULL : ((1ULL << k) - 1ULL);
            unsigned long long x1 = h1 & m1;
            unsigned long long h = x0 ^ (x1 * 0x9e3779b97f4a7c15ULL);
            h ^= (h >> 17);
            h ^= (unsigned long long)len * 0x85ebca6b;
            return h;
        }

        // Case 3: len > 128, up to 200. Use words 0,1,2.
        int k = len - 128; // 1..72
        unsigned long long x0 = h0;
        unsigned long long x1 = h1;
        unsigned long long x2 = h2;
        if (k < 64) {
            unsigned long long m2 = (1ULL << k) - 1ULL;
            x2 &= m2;
        } // else use full x2 when k >= 64

        unsigned long long h = x0
                             ^ (x1 * 0x9e3779b97f4a7c15ULL)
                             ^ (x2 * 0xc2b2ae3d27d4eb4fULL);
        h ^= (h >> 13);
        h ^= (unsigned long long)len * 0x27d4eb2d;
        return h;
    }
    
    branch_update* predict(branch_info& b) {
        bi = b;
        
        if (!(b.br_flags & BR_CONDITIONAL)) {
            u.direction_prediction(true);
            u.target_prediction(0);
            return &u;
        }
        
        // Base index
        u.base_idx = (b.address >> 2) & ((1u << BASE_BITS) - 1u);
        
        // Compute all indices and tags with ONE history compression per table
        for (int i = 0; i < NUM_TABLES; i++) {
            unsigned long long h = compress_history(HIST_LEN[i]);

            // Fold history into 32 bits
            unsigned int fold = (unsigned int)(h ^ (h >> 32));

            // Index: mix PC and folded history
            u.idx[i] = (b.address ^ fold ^ (b.address >> TABLE_BITS)) & ((1u << TABLE_BITS) - 1u);

            // Tag: use a different slice/mix of the same history
            unsigned int tag_fold = (unsigned int)((h >> 16) ^ (h >> 40));
            u.tag[i] = (b.address ^ tag_fold ^ (b.address >> (TAG_BITS + 1))) & ((1u << TAG_BITS) - 1u);
        }
        
        // Find provider (longest matching) and alternate
        u.provider = -1;
        u.altpred = -1;
        
        for (int i = NUM_TABLES - 1; i >= 0; i--) {
            tage_entry *e = &tables[i][u.idx[i]];
            if (e->tag == u.tag[i]) {
                if (u.provider == -1) {
                    u.provider = i;
                } else {
                    u.altpred = i;
                    break;
                }
            }
        }
        
        bool prov_pred = false;
        bool alt_pred  = false;
        
        if (u.provider >= 0) {
            tage_entry *pe = &tables[u.provider][u.idx[u.provider]];
            prov_pred = (pe->ctr >= 0);
            
            if (u.altpred >= 0) {
                tage_entry *ae = &tables[u.altpred][u.idx[u.altpred]];
                alt_pred = (ae->ctr >= 0);
            } else {
                alt_pred = (base[u.base_idx] >= 2);
            }

            // TAGE-style: for newly allocated entries with low usefulness,
            // sometimes prefer alternate depending on use_alt_on_na.
            int abs_ctr = (pe->ctr >= 0) ? pe->ctr : -pe->ctr;
            bool newly_allocated = (pe->u == 0 && abs_ctr <= 1);
            
            if (newly_allocated) {
                if (use_alt_on_na < 8) {
                    u.pred = alt_pred;
                } else {
                    u.pred = prov_pred;
                }
            } else {
                u.pred = prov_pred;
            }
        } else {
            // No tagged hit, use base
            u.pred = (base[u.base_idx] >= 2);
        }
        
        u.direction_prediction(u.pred);
        u.target_prediction(0);
        return &u;
    }
    
    void update(branch_update* up, bool taken, unsigned int target) {
        // Always update history; but only train predictors for conditional branches
        bool is_cond = (bi.br_flags & BR_CONDITIONAL);

        if (!is_cond) {
            // Update global history and exit
            for (int i = 3; i > 0; i--) {
                ghist[i] = (ghist[i] << 1) | (ghist[i-1] >> 63);
            }
            ghist[0] = (ghist[0] << 1) | (taken ? 1ULL : 0ULL);
            return;
        }
        
        my_update* mu = (my_update*)up;
        
        // Update base predictor
        unsigned char* bc = &base[mu->base_idx];
        if (taken) {
            if (*bc < 3) (*bc)++;
        } else {
            if (*bc > 0) (*bc)--;
        }
        
        // We'll need provider/alt predictions here for training use_alt_on_na
        bool prov_pred = false;
        bool alt_pred  = false;
        
        if (mu->provider >= 0) {
            tage_entry* pe = &tables[mu->provider][mu->idx[mu->provider]];
            prov_pred = (pe->ctr >= 0);

            if (mu->altpred >= 0) {
                tage_entry* ae = &tables[mu->altpred][mu->idx[mu->altpred]];
                alt_pred = (ae->ctr >= 0);
            } else {
                alt_pred = (base[mu->base_idx] >= 2);
            }

            // Update provider counter
            if (taken) {
                if (pe->ctr < 3) pe->ctr++;
            } else {
                if (pe->ctr > -4) pe->ctr--;
            }

            // Update usefulness bits when provider and alt disagree
            if (prov_pred != alt_pred) {
                if (prov_pred == taken) {
                    if (pe->u < 3) pe->u++;
                } else {
                    if (pe->u > 0) pe->u--;
                }
            }

            // Train use_alt_on_na only when provider is newly allocated and weak
            int abs_ctr = (pe->ctr >= 0) ? pe->ctr : -pe->ctr;
            bool newly_allocated = (pe->u == 0 && abs_ctr <= 1);

            if (newly_allocated && mu->altpred >= 0) {
                bool provider_correct = (prov_pred == taken);
                bool alt_correct      = (alt_pred  == taken);
                if (provider_correct != alt_correct) {
                    if (alt_correct) {
                        // alternate was better -> bias toward alt
                        if (use_alt_on_na > 0) use_alt_on_na--;
                    } else {
                        // provider was better -> bias toward provider
                        if (use_alt_on_na < 15) use_alt_on_na++;
                    }
                }
            }
        }
        
        // Allocate new entries on misprediction
        if (mu->pred != taken) {
            // Find tables with longer history than provider
            int start = (mu->provider >= 0) ? mu->provider + 1 : 0;
            
            int allocated = 0;
            for (int i = start; i < NUM_TABLES && allocated < 2; i++) {
                tage_entry* e = &tables[i][mu->idx[i]];
                
                // Allocate if entry is not useful
                if (e->u == 0) {
                    e->tag = mu->tag[i];
                    // weakly biased toward correct outcome
                    e->ctr = taken ? 0 : -1;
                    e->u = 0;
                    allocated++;
                }
            }
        }
        
        // Periodic useful bit reset (aging)
        clock++;
        if ((clock & 0x7FFF) == 0) {  // Every 32K branches
            for (int i = 0; i < NUM_TABLES; i++) {
                for (int j = 0; j < (1 << TABLE_BITS); j++) {
                    if (tables[i][j].u > 0) {
                        tables[i][j].u--;
                    }
                }
            }
        }
        
        // Update global history with this branch outcome
        for (int i = 3; i > 0; i--) {
            ghist[i] = (ghist[i] << 1) | (ghist[i-1] >> 63);
        }
        ghist[0] = (ghist[0] << 1) | (taken ? 1ULL : 0ULL);
    }
};
