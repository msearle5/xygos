/**
 * \file init.h
 * \brief initialization
 *
 * Copyright (c) 2000 Robert Ruehlmann
 *
 * This software may be copied and distributed for educational, research,
 * and not for profit purposes provided that this copyright and statement
 * are included in all such copies.
 */

#ifndef INCLUDED_INIT_H
#define INCLUDED_INIT_H

#include "h-basic.h"
#include "z-bitflag.h"
#include "z-file.h"
#include "z-rand.h"
#include "datafile.h"
#include "object.h"

/**
 * Information about maximal indices of certain arrays.
 *
 * This will become a list of "all" the game constants - NRM
 */
struct angband_constants
{
	/* Array bounds etc, set on parsing edit files */
	uint16_t f_max;				/**< Maximum number of terrain features */
	uint16_t trap_max;			/**< Maximum number of trap kinds */
	uint16_t k_max;				/**< Maximum number of object base kinds */
	uint16_t a_max;				/**< Maximum number of artifact kinds */
	uint16_t a_base;			/**< Maximum number of artifact kinds (directly defined, not random) */
	uint16_t a_quest;			/**< Maximum number of quest artifact (so always present, even with standarts off) kinds */
	uint16_t e_max;				/**< Maximum number of ego-item kinds */
	uint16_t r_max;				/**< Maximum number of monster races */
	uint16_t mm_max;			/**< Maximum number of monster mutations */
	uint16_t mp_max;			/**< Maximum number of monster pain message sets */
	uint16_t s_max;				/**< Maximum number of magic spells */
	uint16_t pit_max;			/**< Maximum number of monster pit types */
	uint16_t act_max;			/**< Maximum number of activations for randarts */
	uint16_t fault_max;			/**< Maximum number of faults */
	uint16_t slay_max;			/**< Maximum number of slays */
	uint16_t brand_max;			/**< Maximum number of brands */
	uint16_t mon_blows_max;		/**< Maximum number of monster blows */
	uint16_t mon_passive_max;	/**< Maximum number of monster blows */
	uint16_t blow_methods_max;	/**< Maximum number of monster blow methods */
	uint16_t blow_effects_max;	/**< Maximum number of monster blow effects */
	uint16_t equip_slots_max;	/**< Maximum number of player equipment slots */
	uint16_t profile_max;		/**< Maximum number of cave_profiles */
	uint16_t quest_max;			/**< Maximum number of quests */
	uint16_t projection_max;	/**< Maximum number of projection types */
	uint16_t calculation_max;	/**< Maximum number of object power calculations */
	uint16_t property_max;		/**< Maximum number of object properties */
	uint16_t ordinary_kind_max;	/**< Maximum number of objects in object.txt */
	uint16_t shape_max;			/**< Maximum number of player shapes */

	/* Maxima of things on a given level, read from constants.txt */
	uint16_t level_monster_max;	/**< Maximum number of monsters on a given level */

	/* Monster generation constants, read from constants.txt */
	uint16_t alloc_monster_chance;	/**< 1/per-turn-chance of generation */
	uint16_t level_monster_min;		/**< Minimum number generated */
	uint16_t town_monsters_day;		/**< Townsfolk generated - day */
	uint16_t town_monsters_night;	/**< Townsfolk generated  - night */
	uint16_t repro_monster_max;		/**< Maximum breeders on a level */
	uint16_t ood_monster_chance;	/**< Chance of OoD monster is 1 in this */
	uint16_t ood_monster_amount;	/**< Max number of levels OoD */
	uint16_t monster_group_max;		/**< Maximum size of a group */
	uint16_t mutant_chance;			/**< Chance to try for a mutation */
	uint16_t monster_group_dist;	/**< Max dist of a group from a related group */
	uint32_t town_easy_turns;		/**< Number of turns before difficulty increases */
	uint32_t town_levelup_turns;	/**< Number of turns between difficulty increases */
	uint16_t town_allmons_level;	/**< Level when level-1 non-p/h can show in the town */
	uint16_t town_equalmons_level;	/**< Level when all in-level mons can show in the town */
	uint16_t town_delfirst_level;	/**< Level when the first shop disappears */
	uint16_t town_delall_level;		/**< Level when all shops disappear */
	uint16_t arena_min_monsters;	/**< Minimum number of opponents */
	uint16_t arena_max_monsters;	/**< Maximum number of opponents */
	uint16_t arena_max_depth;		/**< Maximum depth of Arena */
	uint16_t arena_wait_time;		/**< Time between fights */
	
	/* Monster gameplay constants, read from constants.txt */
	uint16_t glyph_hardness;		/**< How hard for a monster to break a glyph */
	uint16_t repro_monster_rate;	/**< Monster reproduction rate-slower */
	uint16_t life_drain_percent;	/**< Percent of player life drained */
	uint16_t flee_range;			/**< Monsters run this many grids out of view */
	uint16_t turn_range;			/**< Monsters turn to fight closer than this */
	uint16_t drop_random;			/**< Monsters drop an item 1 in this many turns */

	/* Dungeon generation constants, read from constants.txt */
	uint16_t level_room_max;	/**< Maximum number of rooms on a level */
	uint16_t level_door_max;	/**< Maximum number of potential doors on a level */
	uint16_t wall_pierce_max;	/**< Maximum number of potential wall piercings */
	uint16_t tunn_grid_max;		/**< Maximum number of tunnel grids */
	uint16_t room_item_av;		/**< Average number of items in rooms */
	uint16_t both_item_av;		/**< Average number of items in random places */
	uint16_t both_gold_av;		/**< Average number of money items */
	uint16_t level_pit_max;		/**< Maximum number of pits on a level */

	/* World shape constants, read from constants.txt */
	uint16_t max_depth;		/**< Maximum dungeon level */
	uint16_t day_length;	/**< Number of turns from dawn to dawn */
	uint16_t dungeon_hgt;	/**< Maximum number of vertical grids on a level */
	uint16_t dungeon_wid;	/**< Maximum number of horizontical grids on a level */
	uint16_t town_hgt;		/**< Maximum number of vertical grids in the town */
	uint16_t town_wid;		/**< Maximu number of horizontical grids in the town */
	uint16_t feeling_total;	/**< Total number of feeling squares per level */
	uint16_t feeling_need;	/**< Squares needed to see to get first feeling */
    uint16_t stair_skip;    /**< Number of levels to skip for each down stair */
	uint16_t move_energy;	/**< Energy the player or monster needs to move */
	uint16_t town_max;		/**< Total number of towns in t_info[], set by world_init_towns */

	/* Carrying capacity constants, read from constants.txt */
	uint16_t pack_size;				/**< Maximum number of pack slots */
	uint16_t quiver_size;			/**< Maximum number of quiver slots */
	uint16_t quiver_slot_size;		/**< Maximum number of missiles per quiver slot */
	uint16_t thrown_quiver_mult;	/**< Size multiplier for non-ammo in quiver */
	uint16_t floor_size;			/**< Maximum number of items per floor grid */

	/* Store parameters, read from constants.txt */
	uint16_t store_inven_max;	/**< Maximum number of objects in store inventory */
	uint16_t store_turns;		/**< Number of turns between turnovers */
	uint16_t store_shuffle;		/**< 1/per-day-chance of owner changing */
	uint16_t store_magic_level;	/**< Level for apply_magic() in normal stores */
	uint16_t theft_dex;			/**< Scale for difficulty of stealing, vs. dexterity */
	uint16_t theft_chr;			/**< Scale for difficulty of stealing, vs. charisma */
	uint32_t faction_turns;		/**< Number of turns before -ve faction starts to increase */
	uint16_t install_turns;		/**< Time taken to install or remove cyberware */

	/* Object creation constants, read from constants.txt */
	uint16_t max_obj_depth;	/**< Maximum depth used in object allocation */
	uint16_t great_obj;		/**< 1/chance of inflating the requested object level */
	uint16_t great_ego;		/**< 1/chance of inflating the requested ego item level */
	uint16_t rand_art;		/**< Number of random artifacts */
	uint16_t cash_max;		/**< Largest pile of cash generated */
	uint16_t aggr_power;	/**< Power rating below which only faulty randarts can aggravate */
	uint16_t inhibit_strong; /**< Percentage of items allowed for strong inhibition */
	uint16_t inhibit_weak;	/**< Percentage of items allowed for weak inhibition */
	uint16_t max_blows;		/**< Maximum blows */
	uint16_t inhibit_blows;	/**< Limit blows above this */
	uint16_t inhibit_ac;	/**< No AC above this */
	uint16_t veryhigh_ac;	/**< AC above this is rare */
	uint16_t high_ac;		/**< AC above this is unusual */
	uint16_t veryhigh_hit;	/**< To-hit above this is rare */
	uint16_t high_hit;		/**< To-hit above this is unusual */
	uint16_t veryhigh_dam;	/**< To-dam above this is rare */
	uint16_t high_dam;		/**< To-dam above this is unusual */
	uint16_t inhibit_might;	/**< Inhibit extra might above this */
	uint16_t inhibit_shots;	/**< Inhibit extra shots above this */
	uint16_t damage_power;	/**< Power rating: power from damage */
	uint16_t to_hit_power;	/**< Power rating: power from to-hit */
	uint16_t nonweap_damage; /**< Power rating: power from damage on nonweapons */

	/* Player constants, read from constants.txt */
	uint16_t max_sight;				/**< Maximum visual range */
	uint16_t max_range;				/**< Maximum missile and spell range */
	uint16_t start_gold;			/**< Amount of gold the player starts with */
	uint16_t start_gold_spread;		/**< Variation in the amount of gold the player starts with */
	uint16_t food_value;			/**< Number of turns 1% of food lasts */
	uint16_t blow_weight_scale;		/**< Divisor of weapon weight when calculating blows */
	uint32_t exp_learn_icon;		/**< Experience gained from learning the last icon */
};

struct init_module {
	const char *name;
	void (*init)(void);
	void (*cleanup)(void);
};

extern bool play_again;

extern const char *list_element_names[];
extern const char *list_obj_flag_names[];

extern struct angband_constants *z_info;

extern const char *ANGBAND_SYS;

extern char *ANGBAND_DIR_GAMEDATA;
extern char *ANGBAND_DIR_CUSTOMIZE;
extern char *ANGBAND_DIR_HELP;
extern char *ANGBAND_DIR_SCREENS;
extern char *ANGBAND_DIR_FONTS;
extern char *ANGBAND_DIR_TILES;
extern char *ANGBAND_DIR_SOUNDS;
extern char *ANGBAND_DIR_ICONS;
extern char *ANGBAND_DIR_USER;
extern char *ANGBAND_DIR_SAVE;
extern char *ANGBAND_DIR_PANIC;
extern char *ANGBAND_DIR_SCORES;
extern char *ANGBAND_DIR_INFO;
extern char *ANGBAND_DIR_ARCHIVE;

extern struct parser *init_parse_artifact(void);
extern struct parser *init_parse_class(void);
extern struct parser *init_parse_ego(void);
extern struct parser *init_parse_feat(void);
extern struct parser *init_parse_history(void);
extern struct parser *init_parse_object(void);
extern struct parser *init_parse_object_base(void);
extern struct parser *init_parse_pain(void);
extern struct parser *init_parse_p_race(void);
extern struct parser *init_parse_pit(void);
extern struct parser *init_parse_monster(void);
extern struct parser *init_parse_vault(void);
extern struct parser *init_parse_constants(void);
extern struct parser *init_parse_flavor(void);
extern struct parser *init_parse_names(void);
extern struct parser *init_parse_hints(void);
extern struct parser *init_parse_lies(void);
extern struct parser *init_parse_trap(void);
extern struct parser *init_parse_chest_trap(void);
extern struct parser *init_parse_quest(void);

extern struct file_parser flavor_parser;

extern const char *player_info_flags[];

extern struct class_magic *parsing_magic;
extern int total_spells;

errr grab_effect_data(struct parser *p, struct effect *effect);
extern void init_file_paths(const char *config, const char *lib, const char *data);
extern void init_game_constants(void);
extern void init_arrays(void);
extern void create_needed_dirs(void);
extern bool init_angband(void);
extern void cleanup_angband(void);
extern void init_parse_magic(struct parser *p);
extern void cleanup_magic(struct class_magic *magic);


#endif /* INCLUDED_INIT_H */
