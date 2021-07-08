/**
 * \file list-materials.h
 * \brief List of object materials
 */
//@ add flags, etc?
/* Materials are defined as
 * MATERIAL(Name, Density (g/cc), Cost (only meaningful relative to others), is metal, is printable)
 * 
 * Costs are prime to avoid obvious patterns
 * ...maybe densities should be also, though this would be less obvious as weights are
 * normally not seen so precisely.
 */
MATERIAL( GENERIC, 		"generic", 		1000,	42,		false,	false )		// for objects without a specified material
MATERIAL( METAL, 		"metal", 		5000,	2347,	true,	false )		// for metals not listed below
MATERIAL( PLASTIC, 		"plastic", 		1150,	101, 	false,	true ) 		// nylon
MATERIAL( ALUMINIUM, 	"aluminium", 	2780,	331, 	true,	true )  	// 2024
MATERIAL( STEEL, 		"steel", 		7860,	1709, 	true,	true ) 		// Stainless 316
MATERIAL( COPPER, 		"copper", 		8500,	1321, 	true,	false )		// various brasses and bronzes are around this - much variation - pure metal is 8940
MATERIAL( LEAD, 		"lead", 		11340,	787, 	true,	false )		// pure
MATERIAL( GOLD, 		"gold", 		19320,	14779, 	true,	true )		// pure
MATERIAL( PLATINUM, 	"platinum", 	21450,	21577, 	true,	false )		// pure
MATERIAL( SILVER, 		"silver", 		10490,	4801, 	true,	false )		// pure
MATERIAL( TITANIUM, 	"titanium", 	4420,	2593, 	true,	true )		// Ti-6Al-4V
MATERIAL( UNOBTAINIUM, 	"unobtainium", 	1420,	17737, 	true,	true )		// 101% pure
MATERIAL( LEATHER, 		"leather", 		860,	409, 	false,	false )
MATERIAL( WOOD, 		"wood", 		740,	163, 	false,	false ) 	// American Red Oak
MATERIAL( GLASS, 		"glass", 		2230,	139, 	false,	false )		// borosilicate
MATERIAL( STONE, 		"stone", 		2700,	223, 	false,	false )		// granite - flint is similar
MATERIAL( CLOTH, 		"cloth", 		400,	149, 	false,	false )		// felt - much variation
MATERIAL( PAPER, 		"paper", 		800,	73, 	false,	false )		// printing paper - much variation
MATERIAL( WAX, 			"wax", 			900,	91, 	false,	false )		// paraffin wax
MATERIAL( ORGANIC, 		"organic", 		950,	83, 	false,	false )		// pork (lol - but this is a catch all category...)
MATERIAL( MAX, NULL, 0, 0, false, false )
