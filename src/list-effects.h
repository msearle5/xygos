/**
 * \file list-effects.h
 * \brief List of effects
 *
 * name: effect code
 * aim: does the effect require aiming?
 * info: info label for spells
 * args: how many arguments the description takes
 * info flags: flags for object description
 * description: text of description
 * menu_name: format string for menu name; use an empty string if there's no
 * plan to use it from a menu
 */
/* name 							aim		info		args	info flags		description */
EFFECT(RANDOM,						false,	NULL,		0,		EFINFO_NONE,	"randomly ",	"")
EFFECT(DAMAGE,						false,	"hurt",		1,		EFINFO_DICE,	"does %s damage to the player",	"")
EFFECT(HEAL_HP,						false,	"heal",		2,		EFINFO_HEAL,	"heals %s hitpoints%s",	"heal self")
EFFECT(MON_HEAL_HP,					false,	NULL,		0,		EFINFO_NONE,	"heals monster hitpoints",	"")
EFFECT(MON_HEAL_KIN,				false,	NULL,		0,		EFINFO_NONE,	"heals fellow monster hitpoints",	"")
EFFECT(NOURISH,						false,	NULL,		3,		EFINFO_FOOD,	"%s for %s turns (%s percent)",	"%s %s")
EFFECT(CRUNCH,						false,	NULL,		0,		EFINFO_NONE,	"crunches",	"")
EFFECT(CURE,						false,	NULL,		1,		EFINFO_CURE,	"cures %s",	"cure %s")
EFFECT(TIMED_SET,					false,	NULL,		2,		EFINFO_TIMED,	"administers %s for %s turns",	"administer %s")
EFFECT(TIMED_INC,					false,	"dur",		2,		EFINFO_TIMED,	"extends %s for %s turns",	"extend %s")
EFFECT(TIMED_INC_NO_RES,			false,	"dur",		2,		EFINFO_TIMED,	"extends %s for %s turns (unresistable)",	"extend %s")
EFFECT(MON_TIMED_INC,				false,	NULL,		2,		EFINFO_TIMED,	"increases monster %s by %s turns",	"")
EFFECT(TIMED_DEC,					false,	NULL,		2,		EFINFO_TIMED,	"reduces length of %s by %s turns",	"reduce %s")
EFFECT(GLYPH,						false,	NULL,		1,		EFINFO_NONE,	"inscribes a glyph beneath you",	"inscribe a glyph")
EFFECT(WEB,							false,	NULL,		0,		EFINFO_NONE,	"creates a web",	"create a web")
EFFECT(RESTORE_STAT,				false,	NULL,		1,		EFINFO_STAT,	"restores your %s",	"restore %s")
EFFECT(DRAIN_STAT,					false,	NULL,		1,		EFINFO_STAT,	"reduces your %s",	"")
EFFECT(LOSE_RANDOM_STAT,			false,	NULL,		1,		EFINFO_STAT,	"reduces a stat other than %s",	"")
EFFECT(GAIN_STAT,					false,	NULL,		1,		EFINFO_STAT,	"increases your %s",	"")
EFFECT(RESTORE_EXP,					false,	NULL,		0,		EFINFO_NONE,	"restores your experience",	"restore experience")
EFFECT(LOSE_EXP,					false,	NULL,		0,		EFINFO_NONE,	"drains your experience",	"drain experience")
EFFECT(GAIN_EXP,					false,	NULL,		1,		EFINFO_CONST,	"grants %d experience points",	"")
EFFECT(DRAIN_LIGHT,					false,	NULL,		0,		EFINFO_NONE,	"drains your light source",	"")
EFFECT(RECYCLE,						false,	NULL,		0,		EFINFO_NONE,	"recycles an item into blocks", "recycle item")
EFFECT(ADD_FAULT,					false,	NULL,		0,		EFINFO_NONE,	"makes an item faulty", "cause fault")
EFFECT(REMOVE_FAULT,				false,	NULL,		0,		EFINFO_NONE,	"attempts repair of a single fault on an object", "repair fault")
EFFECT(REMOVE_FAULTS,				false,	NULL,		0,		EFINFO_NONE,	"attempts repair of all faults on an object", "repair faults")
EFFECT(REMOVE_FRAGILE,				false,	NULL,		0,		EFINFO_NONE,	"makes a fragile object as robust as new again", "remove fragility")
EFFECT(RECALL,						false,	NULL,		0,		EFINFO_NONE,	"returns you from the dungeon or takes you to the dungeon after a short delay",	"recall")
EFFECT(DEEP_DESCENT,				false,	NULL,		0,		EFINFO_NONE,	"teleports you up to five levels lower than the lowest point you have reached so far", "descend to the depths")
EFFECT(ALTER_REALITY,				false,	NULL,		0,		EFINFO_NONE,	"creates a new level",	"alter reality")
EFFECT(MAP_AREA,					false,	NULL,		0,		EFINFO_NONE,	"maps the area around you",	"map surroundings")
EFFECT(READ_MINDS,					false,	NULL,		0,		EFINFO_NONE,	"maps the area around recently detected monsters",	"read minds")
EFFECT(DETECT_TRAPS,				false,	NULL,		0,		EFINFO_NONE,	"detects traps nearby",	"detect traps")
EFFECT(DETECT_DOORS,				false,	NULL,		0,		EFINFO_NONE,	"detects doors nearby",	"detect doors")
EFFECT(DETECT_STAIRS,				false,	NULL,		0,		EFINFO_NONE,	"detects stairs nearby",	"detect stairs")
EFFECT(DETECT_GOLD,					false,	NULL,		0,		EFINFO_NONE,	"detects cash nearby",	"detect cash")
EFFECT(SENSE_OBJECTS,				false,	NULL,		0,		EFINFO_NONE,	"senses objects nearby",	"sense objects")
EFFECT(DETECT_OBJECTS,				false,	NULL,		0,		EFINFO_NONE,	"detects objects nearby",	"detect objects")
EFFECT(DETECT_LIVING_MONSTERS,		false,	NULL,		0,		EFINFO_NONE,	"detects living creatures nearby",	"detect living")
EFFECT(DETECT_VISIBLE_MONSTERS,		false,	NULL,		0,		EFINFO_NONE,	"detects visible creatures nearby",	"detect visible")
EFFECT(DETECT_INVISIBLE_MONSTERS,	false,	NULL,		0,		EFINFO_NONE,	"detects invisible creatures nearby",	"detect invisible")
EFFECT(DETECT_FEARFUL_MONSTERS,		false,	NULL,		0,		EFINFO_NONE,	"detects creatures nearby which are susceptible to fear",	"detect fearful")
EFFECT(ID_FAULT,					false,	NULL,		0,		EFINFO_NONE,	"identifies a single unknown fault on a selected item",	"identify fault")
EFFECT(IDENTIFY,					false,	NULL,		0,		EFINFO_NONE,	"identifies a single unknown icon on a selected item",	"identify")
EFFECT(DETECT_EVIL,					false,	NULL,		0,		EFINFO_NONE,	"detects evil creatures nearby",	"detect evil")
EFFECT(DETECT_SOUL,					false,	NULL,		0,		EFINFO_NONE,	"detects creatures with a spirit nearby",	"detect souls")
EFFECT(CREATE_STAIRS,				false,	NULL,		0,		EFINFO_NONE,	"creates a staircase beneath your feet",	"create stairs")
EFFECT(DISENCHANT,					false,	NULL,		0,		EFINFO_NONE,	"disenchants one of your wielded items",	"disenchant item")
EFFECT(ENCHANT,						false,	NULL,		0,		EFINFO_NONE,	"attempts to enhance an item",	"enhance item")
EFFECT(ARTIFACT_CREATION,			false,	NULL,		0,		EFINFO_NONE,	"creates an artifact", "create artifact")
EFFECT(RECHARGE,					false,	"power",	0,		EFINFO_NONE,	"tries to recharge a device, possibly destroying it on failure",	"recharge")
EFFECT(PROJECT_LOS,					false,	"power",	1,		EFINFO_SEEN,	"%s which are in line of sight",	"%s in line of sight")
EFFECT(PROJECT_LOS_AWARE,			false,	"power",	1,		EFINFO_SEEN,	"%s which are in line of sight",	"%s in line of sight")
EFFECT(ACQUIRE,						false,	NULL,		0,		EFINFO_NONE,	"teleports in good items from off-level", "teleport in items")
EFFECT(LOCAL_ACQUIRE,				false,	NULL,		0,		EFINFO_NONE,	"teleports the level's best item to you", "teleport best item")
EFFECT(WISH,						false,	NULL,		0,		EFINFO_NONE,	"asks for an item", "ask for an item")
EFFECT(WAKE,						false,	NULL,		0,		EFINFO_NONE,	"awakens all nearby sleeping monsters",	"awaken all")
EFFECT(SUMMON,						false,	NULL,		1,		EFINFO_SUMM,	"summons %s at the current level",	"summon %s")
EFFECT(BANISH,						false,	NULL,		0,		EFINFO_NONE,	"removes all of a given creature type from the level",	"banish")
EFFECT(MASS_BANISH,					false,	NULL,		0,		EFINFO_NONE,	"removes all nearby creatures",	"banish all")
EFFECT(PROBE,						false,	NULL,		0,		EFINFO_NONE,	"gives you information on the health and abilities of monsters you can see",	"probe")
EFFECT(HOP,							false,	"range",	2,		EFINFO_TELE,	"hops", "hop")
EFFECT(PORTAL,						false,	"range",	2,		EFINFO_TELE,	"teleports %s to the nearest portal, or if there is none randomly %s", "go to a portal")
EFFECT(CREATE_PORTAL,				false,	"range",	2,		EFINFO_TELE,	"creates a pair of portals, at your position and a random distant spot", "create portals")
EFFECT(TELEPORT,					false,	"range",	2,		EFINFO_TELE,	"teleports %s randomly %s",	"teleport %s %s")
EFFECT(TELEPORT_TO,					false,	NULL,		0,		EFINFO_NONE,	"teleports toward a target",	"teleport to target")
EFFECT(TELEPORT_LEVEL,				false,	NULL,		0,		EFINFO_NONE,	"teleports you one level up or down",	"teleport level")
EFFECT(MELT_DOWN,					false,	NULL,		0,		EFINFO_NONE,	"melt through the floor",	"melt down")
EFFECT(RUBBLE,						false,	NULL,		0,		EFINFO_NONE,	"causes rubble to fall around you",	"")
EFFECT(GRANITE,						false,	NULL,		0,		EFINFO_NONE,	"causes a granite wall to fall behind you",		"")
EFFECT(DESTRUCTION,					false,	NULL,		1,		EFINFO_QUAKE,	"destroys an area around you in the shape of a circle radius %d, and blinds you for 1d10+10 turns",	"destroy area")
EFFECT(EARTHQUAKE,					false,	NULL,		1,		EFINFO_QUAKE,	"causes an earthquake around you of radius %d",	"cause earthquake")
EFFECT(LIGHT_LEVEL,					false,	NULL,		0,		EFINFO_NONE,	"completely lights up and maps the level", "light level")
EFFECT(DARKEN_LEVEL,				false,	NULL,		0,		EFINFO_NONE,	"completely darkens and maps the level", "darken level")
EFFECT(LIGHT_AREA,					false,	NULL,		0,		EFINFO_NONE,	"lights up the surrounding area", "light area")
EFFECT(DARKEN_AREA,					false,	NULL,		0,		EFINFO_NONE,	"darkens the surrounding area", "darken area")
EFFECT(SPOT,						false,	"dam",		4,		EFINFO_SPOT,	"creates a ball of %s with radius %d, centred on and hitting the target, with full intensity to radius %d, dealing %s damage at the centre",	"engulf with %s")
EFFECT(SPHERE,						false,	"dam",		4,		EFINFO_SPOT,	"creates a ball of %s with radius %d, centred on the player, with full intensity to radius %d, dealing %s damage at the centre",	"project %s")
EFFECT(BALL,						true,	"dam",		3,		EFINFO_BALL,	"fires a ball of %s with radius %d, dealing %s damage at the centre",	"fire a ball of %s")
EFFECT(BREATH,						true,	NULL,		3,		EFINFO_BREATH,	"breathes a cone of %s with width %d degrees, dealing %s damage at the source",	"breathe a cone of %s")
EFFECT(ARC,							true,	"dam",		3,		EFINFO_BREATH,	"produces a cone of %s with width %d degrees, dealing %s damage at the source",	"produce a cone of %s")
EFFECT(SHORT_BEAM,					true,	"dam",		3,		EFINFO_SHORT,	"produces a beam of %s with length %d, dealing %s damage",	"produce a beam of %s")
EFFECT(LASH,						true,	NULL,		2,		EFINFO_LASH,	"fires a beam of %s length %d, dealing damage determined by blows",	"lash with %s")
EFFECT(SWARM,						true,	"dam",		3,		EFINFO_BALL,	"fires a series of %s balls of radius %d, dealing %s damage at the centre of each",	"fire a swarm of %s balls")
EFFECT(STRIKE,						true,	"dam",		3,		EFINFO_BALL,	"creates a ball of %s with radius %d, dealing %s damage at the centre",	"strike with %s")
EFFECT(STAR,						false,	"dam",		2,		EFINFO_BOLTD,	"fires a line of %s in all directions, each dealing %s damage",	"fire a line of %s in all directions")
EFFECT(STAR_BALL,					false,	"dam",		3,		EFINFO_BALL,	"fires balls of %s with radius %d in all directions, dealing %s damage at the centre of each",	"fire balls of %s in all directions")
EFFECT(BOLT,						true,	"dam",		2,		EFINFO_BOLTD,	"casts a bolt of %s dealing %s damage",	"cast a bolt of %s")
EFFECT(BEAM,						true,	"dam",		2,		EFINFO_BOLTD,	"casts a beam of %s dealing %s damage",	"cast a beam of %s")
EFFECT(BOLT_OR_BEAM,				true,	"dam",		2,		EFINFO_BOLTD,	"casts a bolt or beam of %s dealing %s damage",	"cast a bolt or beam of %s")
EFFECT(LINE,						true,	"dam",		2,		EFINFO_BOLTD,	"creates a line of %s dealing %s damage",	"create a line of %s")
EFFECT(ALTER,						true,	NULL,		1,		EFINFO_BOLT,	"creates a line which %s",	"create a line which %s")
EFFECT(BOLT_STATUS,					true,	NULL,		1,		EFINFO_BOLT,	"casts a bolt which %s",	"cast a bolt which %s")
EFFECT(BOLT_STATUS_DAM,				true,	"dam",		2,		EFINFO_BOLTD,	"casts a bolt which %s, dealing %s damage",	"cast a bolt which %s")
EFFECT(BOLT_AWARE,					true,	NULL,		1,		EFINFO_BOLT,	"creates a bolt which %s",	"create a bolt which %s")
EFFECT(TOUCH,						false,	NULL,		1,		EFINFO_TOUCH,	"%s on all adjacent squares",	"%s all adjacent")
EFFECT(TOUCH_AWARE,					false,	NULL,		1,		EFINFO_TOUCH,	"%s on all adjacent squares",	"%s all adjacent")
EFFECT(BLAST_ARMOR,					false,	NULL,		0,		EFINFO_NONE,	"blasts your worn armor", "blast armor")
EFFECT(BLAST_WEAPON,				false,	NULL,		0,		EFINFO_NONE,	"blasts your wielded melee weapon", "blast weapon")
EFFECT(UNBRAND_ITEM,				false,	NULL,		0,		EFINFO_NONE,	"removes ego and other powers from an item",	"remove an item's powers")
EFFECT(BRAND_ITEM,					false,	NULL,		1,		EFINFO_DICE,	"brands an item as an ego, adding %s to combat stats",	"brand item")
EFFECT(BRAND_WEAPON,				false,	NULL,		0,		EFINFO_NONE,	"brands your wielded melee weapon",	"brand weapon")
EFFECT(BRAND_AMMO,					false,	NULL,		0,		EFINFO_NONE,	"brands a stack of ammunition",	"brand ammunition")
EFFECT(CREATE_ARROWS,				false,	NULL,		0,		EFINFO_NONE,	"uses a staff to create a stack of arrows",	"use staff for arrows")
EFFECT(SHAPECHANGE,					false,	NULL,		0,		EFINFO_NONE,	"changes the player's shape",	"change shape")
EFFECT(CURSE,						true,	"dam",		0,		EFINFO_NONE,	"damages a monster directly",	"curse")
EFFECT(COMMAND,						true,	NULL,		0,		EFINFO_NONE,	"takes control of a monster",	"command")
EFFECT(JUMP_AND_BITE,				false,	NULL,		0,		EFINFO_NONE,	"jumps the player to the closest living monster and bites it",	"jump and bite")
EFFECT(MOVE_ATTACK,					true,	"blows",	1,		EFINFO_DICE,	"moves the player up to 4 spaces and executes up to %d melee blows",	"move and attack")
EFFECT(SINGLE_COMBAT,				true,	NULL,		0,		EFINFO_NONE,	"engages a monster in single combat",	"engage in single combat")
EFFECT(MELEE_BLOWS,					true,	"blows",	1,		EFINFO_DICE,	"strikes %d blows against an adjacent monster",	"pummel")
EFFECT(SWEEP,						false,	"blows",	1,		EFINFO_DICE,	"strikes %d blows against all adjacent monsters",	"sweep")
EFFECT(BIZARRE,						true,	NULL,		0,		EFINFO_NONE,	"does bizarre things",	"do bizarre things")
EFFECT(WONDER,						true,	NULL,		0,		EFINFO_NONE,	"creates random and unpredictable effects",	"create random effects")
EFFECT(SELECT,						false,	NULL,		0,		EFINFO_NONE,	"selects one of ",	"")
EFFECT(HABANERO,					false, "dam",		3,		EFINFO_NONE,	"may make you resist cold for a while, and maybe even breathe fire", "")
EFFECT(SNOZZCUMBER,					false, NULL,		0,		EFINFO_NONE,	"may turn you into a giant", "")
EFFECT(PRINT,						false, NULL,		2,		EFINFO_NONE,	"creates an item", "create an item") 
EFFECT(MUTATE,						false, NULL,		0,		EFINFO_NONE,	"causes you to mutate strangely (or perhaps lose a mutation)", "mutate")
EFFECT(HORNS,						false, NULL,		0,		EFINFO_NONE,	"causes a loud honk that wakes nearby monsters", "honk")  
EFFECT(FORCE_REGEN,					false, NULL,		0,		EFINFO_NONE,	"forces regeneration", "force regeneration")
EFFECT(RUMOR,						false, NULL,		0, 		EFINFO_NONE,	"displays a rumor", "display rumor")
EFFECT(CLIMBING,					false, NULL,		0, 		EFINFO_NONE,	"climbs the wall to the level above", "climb the wall")
EFFECT(PRISMATIC,					false, NULL,		0, 		EFINFO_NONE,	"switches into an alternate mode", "switch mode")
EFFECT(LAND,						false, NULL,		0,		EFINFO_NONE,	"lands", "land")
EFFECT(TAKEOFF,						false, NULL,		0,		EFINFO_NONE,	"takes off", "take off")
EFFECT(BANANA,						false, NULL,		0, 		EFINFO_NONE,	"", "")
EFFECT(PERSONALITY,					false, NULL,		0,		EFINFO_NONE,	"changes your personality", "change personality")
EFFECT(NEXT,						false, NULL,		0, 		EFINFO_NONE,	"", "")
EFFECT(SET_VALUE,					false,	NULL,		0,		EFINFO_NONE,	"",					"")
EFFECT(CLEAR_VALUE,					false,	NULL,		0,		EFINFO_NONE,	"",					"")
EFFECT(MESSAGE,						false, NULL,		0,		EFINFO_NONE,	"", 				"")
EFFECT(IF_SUCCESSFUL,				false, NULL,		0,		EFINFO_NONE,	"", 				"")
EFFECT(TIME_LOOP,					false, NULL,		0,		EFINFO_NONE,	"", 				"")
EFFECT(RUN_CARD,					false, NULL,		0, 		EFINFO_NONE,	"runs a card", "run card")
EFFECT(LOL,							false, NULL,		0, 		EFINFO_NONE,	"",					"")
