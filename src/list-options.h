/**
 * \file list-options.h
 * \brief options
 *
 * Currently, if there are more than 21 of any option type, the later ones
 * will be ignored
 * Cheat options need to be followed by corresponding score options
 */

/* name                   description
type     normal */
OP(none,                  "",
SPECIAL, false)
OP(rogue_like_commands,   "Use the roguelike command keyset",
INTERFACE, false)
OP(use_sound,             "Use sound",
INTERFACE, false)
OP(show_damage,           "Show damage player deals to monsters",
INTERFACE, false)
OP(use_old_target,        "Use old target by default",
INTERFACE, false)
OP(pickup_always,         "Always pickup items",
INTERFACE, false)
OP(pickup_inven,          "Always pickup items matching inventory",
INTERFACE, true)
OP(show_flavors,          "Show flavors in object descriptions",
INTERFACE, false)
OP(disturb_near,          "Disturb whenever viewable monster moves",
INTERFACE, true)
OP(auto_more,             "Automatically clear '-more-' prompts",
INTERFACE, false)
OP(notify_recharge,       "Notify on object recharge",
INTERFACE, false)
OP(effective_speed,       "Show effective speed as multiplier",
INTERFACE, false)
OP(fast_arena,            "Skip to the result of an arena fight",
INTERFACE, false)
OP(show_target,           "Highlight target with cursor",
MAP, true)
OP(highlight_player,      "Highlight player with cursor between turns",
MAP, false)
OP(center_player,         "Center map continuously",
MAP, false)
OP(solid_walls,           "Show walls as solid blocks",
MAP, false)
OP(hybrid_walls,          "Show walls with shaded background",
MAP, false)
OP(view_yellow_light,     "Color: Illuminate torchlight in yellow",
MAP, true)
OP(animate_flicker,       "Color: Shimmer multi-colored things",
MAP, true)
OP(purple_uniques,        "Color: Show unique monsters in purple",
MAP, false)
OP(hp_changes_color,      "Color: Player color indicates % hit points",
MAP, true)
OP(mouse_movement,        "Allow mouse clicks to move the player",
MAP, false)
OP(cheat_hear,            "Cheat: Peek into monster creation",
CHEAT, false)
OP(score_hear,            "Score: Peek into monster creation",
SCORE, false)
OP(cheat_room,            "Cheat: Peek into level creation",
CHEAT, false)
OP(score_room,            "Score: Peek into level creation",
SCORE, false)
OP(cheat_xtra,            "Cheat: Peek into something else",
CHEAT, false)
OP(score_xtra,            "Score: Peek into something else",
SCORE, false)
OP(cheat_live,            "Cheat: Allow player to avoid death",
CHEAT, false)
OP(score_live,            "Score: Allow player to avoid death",
SCORE, false)
OP(birth_randarts,        "Generate a new, random artifact set",
BIRTH, true)
OP(birth_botharts,        "Use both random and non-random artifacts",
BIRTH, true)
OP(birth_connect_stairs,  "Generate connected stairs",
BIRTH, true)
OP(birth_force_descend,   "Force player descent (never make up stairs)",
BIRTH, false)
OP(birth_no_recall,       "Recall has no effect",
BIRTH, false)
OP(birth_no_artifacts,    "Restrict creation of artifacts",
BIRTH, false)
OP(birth_stacking,        "Stack objects on the floor",
BIRTH, true)
OP(birth_lose_arts,       "Lose artifacts when leaving level",
BIRTH, false)
OP(birth_feelings,        "Show level feelings",
BIRTH, true)
OP(birth_no_selling,      "Increase cash drops but disable selling",
BIRTH, false)
OP(birth_single_home,      "All towns' homes share the same inventory",
BIRTH, true)
OP(birth_start_kit,       "Start with a kit of useful gear",
BIRTH, true)
OP(birth_ai_learn,        "Monsters learn from their mistakes",
BIRTH, true)
OP(birth_know_icons,      "Know all icons on birth",
BIRTH, false)
OP(birth_know_flavors,    "Know all flavors on birth",
BIRTH, false)
OP(birth_levels_persist,  "Persistent levels (experimental)",
BIRTH, false)
OP(birth_percent_damage,  "To-dam is a percentage of dice (experimental)",
BIRTH, false)
OP(birth_time_limit,      "Difficulty increases over time",
BIRTH, true)
OP(birth_multi_class,     "Allow switching classes at level up",
BIRTH, true)
OP(birth_auto_char_dump,  "Save a character dump at every level up",
BIRTH, false)
