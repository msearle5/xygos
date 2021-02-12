/**
 * \file list-player-flags.h
 * \brief player race and class flags
 *
 * Adjusting these flags does not break savefiles. Flags below start from 1
 * on line 14, so a flag's sequence number is its line number minus 13.
 *
 * Fields:
 * symbol - the flag name
 * additional details in player_property.txt
 */

PF(NONE)
PF(FAST_SHOT)
PF(BRAVERY_30)
PF(BLESS_WEAPON)
PF(ZERO_FAIL)
PF(BEAM)
PF(CHOOSE_SPELLS)
PF(KNOW_MUSHROOM)
PF(KNOW_ZAPPER)
PF(SEE_ORE)
PF(NO_MANA)
PF(CHARM)
PF(UNLIGHT)
PF(ROCK)
PF(STEAL)
PF(SHIELD_BASH)
PF(EVIL)
PF(COMBAT_REGEN)
PF(NO_FOOD)
PF(EAT_BATTERIES)

/* Abilities - general talents */
PF(FORAGING)
PF(PRECOCITY)
PF(PATIENCE)
PF(UNKNOWN_TALENTS)
PF(EMOTIONAL_INTELLIGENCE)

/* Marksman specialties */
PF(HANDGUN_SPECIALIST)
PF(RIFLE_SPECIALIST)
PF(6MM_HANDGUN_SPECIALIST)
PF(9MM_HANDGUN_SPECIALIST)
PF(12MM_HANDGUN_SPECIALIST)
PF(6MM_RIFLE_SPECIALIST)
PF(9MM_RIFLE_SPECIALIST)
PF(12MM_RIFLE_SPECIALIST)

/* Mutations */
PF(PUKING)

PF(SPIT_ACID)

/* Skin */
PF(ELEPHANT_HIDE)
PF(CRYSTAL_SKIN)
PF(SCALY_SKIN)
PF(STEEL_SKIN)
