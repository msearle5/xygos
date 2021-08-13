#include "player.h"
#include "player-ability.h"
#include "player-calcs.h"
#include "savefile.h"
#include "z-util.h"

#define SUPER_EVERY 10
#define SUPER_POWERS (PY_MAX_LEVEL / SUPER_EVERY)

/* Persistent state for the super extension */
struct super_state {
	s32b weakness;
	s32b power[SUPER_POWERS];
};

static void super_loadsave(bool complete) {
	struct super_state *state;

	if (!complete) {
		if (player->extension->state == NULL)
			player->extension->state = mem_zalloc(sizeof(struct super_state));
		state = (struct super_state *)player->extension->state;
		/* Load/save super_state */
		rdwr_s32b(&state->weakness);
		for(int i=0; i<SUPER_POWERS; i++) {
			rdwr_s32b(&state->power[i]);
		}
	}
}

/* Start a new character as a super */
static void super_init(void)
{
	/* Initialise saved state */
	struct super_state *state = player->extension->state = mem_zalloc(sizeof(struct super_state));

	/* Roll weaknesses, powers */

	/* Some elements are too rarely used to be a reasonable weakness, so are missing
	 * from this list.
	 * OTOH, shards is high damage enough and difficult enough to find a resistance to
	 * that it would be too nasty even though it is not particularly common.
	 */
	static const byte weak_elem[] = {
		ELEM_ACID, ELEM_ELEC, ELEM_COLD, ELEM_FIRE,
		ELEM_POIS,
		ELEM_LIGHT, ELEM_SOUND,
		ELEM_RADIATION,
	};
	state->weakness = weak_elem[randint0(sizeof(weak_elem))];

	/* The uniform - don't gen any other wearables? */

	/* Ensure powers don't conflict with the weakness (Torch vs fire vulnerability, Flight vs Pilot
	 * (or starting gear - no point if it duplicates a power) for example).
	 * A super should have powers reasonably well spread out, but some powers are worth more
	 * than others.
	 **/
	bool ok;
	int reps = 0;
	do {
		reps++;
		ok = true;
		for(int i=0; i<SUPER_POWERS; i++) {
			int level = (i + 1) * SUPER_EVERY;
			int power;

			/* Select a random ability with the Super flag set */
			do {
				power = randint0(PF_MAX);
			} while ((!ability[power]) || (!(ability[power]->flags & AF_SUPER)));

			/* ... which must not be out of level */
			if ((level < ability[power]->minlevel) || ((ability[power]->maxlevel > 0) && (level > ability[power]->maxlevel))) {
				ok = false;
				break;
			}

			/* or a duplicate */
			for(int j=0;j<i;j++) {
				if (state->power[j] == power) {
					ok = false;
					break;
				}
			}
			if (!ok) break;

			/* or forbidden by a previous power */
			bool forbid = false;
			for(int k=0; k<PF_MAX; k++) {
				if (ability[k]) {
					if (ability[k]->forbid[power]) {
						for(int j=0;j<i;j++) {
							if (state->power[j] == k) {
								forbid = true;
								break;
							}
						}
					}
				}
				if (forbid)
					break;
			}
			if (forbid) {
				ok = false;
				break;
			}

			/* or a (near) duplicate of a class power */
			if ((streq(player->class->name, "Pilot")) && (streq(ability[power]->name, "Flight"))) {
				ok = false;
				break;
			}

			/* or the weakness TODO */

			state->power[i] = power;
		}
	} while (!ok);
}

/* Resistances and weaknesses */
static void super_calc(struct player_state *state)
{
	struct player *p = player;
	struct super_state *super = (struct super_state *)p->extension->state;
	if (!super) return;

	/* Currently the weakness is always an element, but more interesting things could
	 * be added - as abilities? (magnetic body, etc)
	 */
	for(int i=0;i<ELEM_MAX;i++)
		p->extension->el_info[i].res_level = 0;
	p->extension->el_info[super->weakness].res_level = -1;
}

/* Level up, manifest powers */
static void super_levelup(int from, int to)
{
	struct player *p = player;
	struct super_state *state = (struct super_state *)p->extension->state;

	/* Gain an ability every SUPER_EVERY levels */
	int lev = player->lev;
	for(int level=from; level<=to; level++) {
		if ((level > 0) && ((level % SUPER_EVERY) == 0)) {
			s32b power = state->power[(level / SUPER_EVERY)-1];

			player->lev = level;
			if (!gain_ability(power, false)) {
				fprintf(stderr,"failed!\n");
			}
		}
	}
	player->lev = lev;
}

/* Install hooks */
void install_race_SUPER(void)
{
	struct player_race *r = get_race_by_name("Super");
	r->init = super_init;
	r->levelup = super_levelup;
	r->loadsave = super_loadsave;
	r->calc = super_calc;
}
