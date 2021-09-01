/* Marksman class
 */

#include "init.h"
#include "obj-tval.h"
#include "player.h"
#include "player-util.h"
#include "savefile.h"
#include "z-util.h"

/* Identify an item as a Marksman: it must be at your level or below, and
 * either ammo or a gun.
 **/
static bool marksman_id(struct object *obj)
{
	return (((tval_is_ammo(obj)) || (tval_is_launcher(obj))) &&
		(obj->kind->level <= levels_in_class(get_class_by_name("Marksman")->cidx)));
}

/* Install hooks */
void install_class_MARKSMAN(void)
{
	struct player_class *c = get_class_by_name("Marksman");
	c->id = marksman_id;
}
