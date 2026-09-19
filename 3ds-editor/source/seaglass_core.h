/* seaglass_core.h -- portable C port of the Seaglass save engine.
 * No 3DS dependencies: compiles on host for testing and on devkitARM. */
#pragma once
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#define SAVE_SIZE   0x20000
#define STAT_N      6           /* hp,atk,def,spe,spa,spd (stored order)   */
#define MAX_LIST    1600

typedef struct {
    uint8_t  head[0x20];        /* pv, otid, nickname, otname, checksum... */
    uint8_t  G[12], A[12], E[12], M[12];   /* decrypted substructs         */
    uint8_t  tail[0x14];        /* party tail 0x50..0x63 (level, stats)    */
    uint32_t pv, otid;
    bool     is_party;
    int      party_index;       /* if party                                */
    uint32_t storage_off;       /* if box                                  */
} sg_mon;

typedef struct { uint16_t id; } sg_id;

/* ---- lifecycle ---- */
bool sg_load_save(const char *path);
bool sg_load_rom (const char *path);       /* also runs offset autodetect  */
bool sg_write_save(const char *path);      /* writes current buffer        */
extern bool sg_rom_ok;                     /* tables verified for this ROM */

/* ---- progress reporting (optional; UI concern, kept out of the engine's
   own logic) ----
   sg_load_rom() is one long blocking call, worst-case several seconds on
   slow storage (DS/DSi). Register a callback to get 0-100 progress updates
   through detection and list-building so a UI can render a loading bar; the
   callback fires on whatever the caller's own thread/stack is (there is no
   engine-side timing or threading), so keep it fast. Pass NULL (the default)
   to disable -- desktop/3DS don't register one and see no behavior change. */
typedef void (*sg_progress_fn)(int percent);
void sg_set_progress_callback(sg_progress_fn fn);

/* ---- trainer / listing ---- */
void sg_trainer_name(char *out, size_t n);
int  sg_active_slot(void);                 /* 0=A 1=B                      */
int  sg_party_count(void);
bool sg_get_party_mon(int i, sg_mon *m);
int  sg_box_scan(uint32_t *offs, int max); /* storage offsets of box mons  */
bool sg_get_box_mon(uint32_t off, sg_mon *m);
void sg_commit_mon(const sg_mon *m);       /* encode into save buffer      */

/* ---- mon fields ---- */
uint16_t mon_species(const sg_mon*);   void mon_set_species(sg_mon*, uint16_t);
uint16_t mon_item(const sg_mon*);      void mon_set_item(sg_mon*, uint16_t);
uint32_t mon_exp(const sg_mon*);
uint8_t  mon_friendship(const sg_mon*);void mon_set_friendship(sg_mon*, uint8_t);
uint16_t mon_move(const sg_mon*, int); void mon_set_move(sg_mon*, int, uint16_t);
uint8_t  mon_pp(const sg_mon*, int);   void mon_set_pp(sg_mon*, int, uint8_t);
uint8_t  mon_pp_up(const sg_mon*, int); void mon_set_pp_up(sg_mon*, int, uint8_t); /* 0-3 per move */
uint8_t  mon_ev(const sg_mon*, int);   void mon_set_ev(sg_mon*, int, uint8_t);
uint8_t  mon_contest(const sg_mon*, int); void mon_set_contest(sg_mon*, int, uint8_t); /* 0=cool..5=sheen */
uint8_t  mon_iv(const sg_mon*, int);   void mon_set_iv(sg_mon*, int, uint8_t);
int      mon_ability_slot(const sg_mon*); void mon_set_ability_slot(sg_mon*, int);
uint8_t  mon_pokerus_strain(const sg_mon*);
uint8_t  mon_pokerus_days(const sg_mon*);
void     mon_set_pokerus(sg_mon*, uint8_t strain, uint8_t days);
uint8_t  mon_met_location(const sg_mon*);      void mon_set_met_location(sg_mon*, uint8_t);
uint8_t  mon_met_level(const sg_mon*);         void mon_set_met_level(sg_mon*, uint8_t);
uint8_t  mon_origin_game(const sg_mon*);       void mon_set_origin_game(sg_mon*, uint8_t);
uint8_t  mon_poke_ball(const sg_mon*);         void mon_set_poke_ball(sg_mon*, uint8_t);
int      mon_ot_gender(const sg_mon*);         void mon_set_ot_gender(sg_mon*, int); /* 0=M,1=F */
int      mon_nature(const sg_mon*);
bool     mon_shiny(const sg_mon*);
bool     mon_is_egg(const sg_mon*);
int      mon_level(const sg_mon*);
void     mon_nickname(const sg_mon*, char *out, size_t n);
void     mon_set_nickname(sg_mon*, const char *s);
char     mon_gender(const sg_mon*);        /* 'M','F','N'                  */

/* ---- edit helpers ---- */
void sg_set_level(sg_mon*, int level);     /* exp curve aware              */
void sg_recompute_stats(sg_mon*);          /* party only; full heal        */
void sg_reroll_pv(sg_mon*, int nature, int shiny, char gender); /* -1/-1/0 = keep */

/* ---- ROM lookups (valid after sg_load_rom) ---- */
void sg_species_name(uint16_t sp, char *out, size_t n);
void sg_move_name(uint16_t mv, char *out, size_t n);
void sg_item_name(uint16_t it, char *out, size_t n);
void sg_ability_name(uint16_t ab, char *out, size_t n);
void sg_species_abilities(uint16_t sp, uint16_t *a0, uint16_t *a1);
void sg_base_stats(uint16_t sp, uint8_t out[6]);
uint8_t sg_gender_ratio(uint16_t sp);

/* front sprite as 64x64 RGBA8888 (16 KB out buffer). true on success. */
bool sg_sprite_rgba(uint16_t species, bool shiny, uint8_t *out /*64*64*4*/);

/* front sprite (64x64) as RGBA8888; returns false if unavailable */
bool sg_sprite_rgba(uint16_t species, bool shiny, uint8_t *out);   /* 64x64 RGBA */

/* box helpers: 14 boxes x 30 slots, 80 bytes each, storage base +4 */
uint32_t sg_box_slot_off(int box, int slot);

/* valid-id lists for cycling in the UI */
extern uint16_t sg_species_ids[MAX_LIST]; extern int sg_species_n;
extern uint16_t sg_move_ids[1000];        extern int sg_move_n;
extern uint16_t sg_item_ids[1300];        extern int sg_item_n;

/* same ids but sorted alphabetically by name (for the picker UI) */
extern uint16_t sg_move_sorted[1000];     extern int sg_move_sn;
extern uint16_t sg_item_sorted[1300];     extern int sg_item_sn;
extern uint16_t sg_species_sorted[MAX_LIST]; extern int sg_species_sn;

/* live-computed stat (current), and level for box mons (from exp) */
int sg_calc_stat(const sg_mon *m, int i);
int sg_level_of(const sg_mon *m);
