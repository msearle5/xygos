/**
 * \file list-equip-slots.h
 * \brief types of slot for equipment
 *
 * Fields:
 * slot - The index name of the slot
 * acid_v - whether equipment in the slot needs checking for acid damage
 * name - whether the actual item name is mentioned when things happen to it
 * weight,ac - for wrestlers' AC bonus calculation (see effective_ac_of())
 * mention - description for when the slot is mentioned briefly
 * heavy describe - description for when the slot item is too heavy
 * describe - description for when the slot is described at length
 */
/* slot				acid_v	name	weight,	ac,	mention			heavy decribe	describe */
EQUIP(NONE,			false,	false,	0,		0,	"",				"",				"")
EQUIP(WEAPON,		false,	false,	0,		0,	"Wielding",		"Just lifting",	"attacking monsters with")
EQUIP(GUN,			false,	false,	0,		0,	"Firing",		"Just holding",	"firing with")
EQUIP(LIGHT,		false,	false,	0,		0,	"Light source",	"",				"using to light your way")
EQUIP(BODY_ARMOR,	true,	true,	6000,	40,	"On %s",		"",				"wearing on your %s")
EQUIP(CLOAK,		true,	true,	1000,	8,	"On %s",		"",				"wearing on your %s")
EQUIP(BELT,			true,	true,	2000,	5,	"On %s",		"",				"wearing on your %s")
EQUIP(SHIELD,		true,	true,	1200,	25,	"On %s",		"",				"wearing on your %s")
EQUIP(HAT,			true,	true,	1800,	15,	"On %s",		"",				"wearing on your %s")
EQUIP(GLOVES,		true,	true,	500,	8,	"On %s",		"",				"wearing on your %s")
EQUIP(CARD,			false,	true,	0,		0,	"Up %s",		"",				"hidden up your %s")
EQUIP(BOOTS,		true,	true,	600,	10,	"On %s",		"",				"wearing on your %s")
EQUIP(LEGS,			false,	true,	12000,	4,	"In %s",		"",				"implanted into your %s")
EQUIP(ARMS,			false,	true,	9000,	3,	"In %s",		"",				"implanted into your %s")
EQUIP(CHIP,			false,	true,	100,	0,	"In %s",		"",				"implanted into your %s")
