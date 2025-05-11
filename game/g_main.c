/*
Copyright (C) 1997-2001 Id Software, Inc.

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  

See the GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.

*/

#include "g_local.h"

game_locals_t	game;
level_locals_t	level;
game_import_t	gi;
game_export_t	globals;
spawn_temp_t	st;

int	sm_meat_index;
int	snd_fry;
int meansOfDeath;

int wasCrouched = 0;
int isSliding = 0;

edict_t		*g_edicts;

cvar_t	*deathmatch;
cvar_t	*coop;
cvar_t	*dmflags;
cvar_t	*skill;
cvar_t	*fraglimit;
cvar_t	*timelimit;
cvar_t	*password;
cvar_t	*spectator_password;
cvar_t	*maxclients;
cvar_t	*maxspectators;
cvar_t	*maxentities;
cvar_t	*g_select_empty;
cvar_t	*dedicated;

cvar_t	*filterban;

cvar_t	*sv_maxvelocity;
cvar_t	*sv_gravity;

cvar_t	*sv_rollspeed;
cvar_t	*sv_rollangle;
cvar_t	*gun_x;
cvar_t	*gun_y;
cvar_t	*gun_z;

cvar_t	*run_pitch;
cvar_t	*run_roll;
cvar_t	*bob_up;
cvar_t	*bob_pitch;
cvar_t	*bob_roll;

cvar_t	*sv_cheats;

cvar_t	*flood_msgs;
cvar_t	*flood_persecond;
cvar_t	*flood_waitdelay;

cvar_t	*sv_maplist;

void SpawnEntities (char *mapname, char *entities, char *spawnpoint);
void ClientThink (edict_t *ent, usercmd_t *cmd);
qboolean ClientConnect (edict_t *ent, char *userinfo);
void ClientUserinfoChanged (edict_t *ent, char *userinfo);
void ClientDisconnect (edict_t *ent);
void ClientBegin (edict_t *ent);
void ClientCommand (edict_t *ent);
void RunEntity (edict_t *ent);
void WriteGame (char *filename, qboolean autosave);
void ReadGame (char *filename);
void WriteLevel (char *filename);
void ReadLevel (char *filename);
void InitGame (void);
void G_RunFrame (void);


//===================================================================


void ShutdownGame (void)
{
	gi.dprintf ("==== ShutdownGame ====\n");

	gi.FreeTags (TAG_LEVEL);
	gi.FreeTags (TAG_GAME);
}


/*
=================
GetGameAPI

Returns a pointer to the structure with all entry points
and global variables
=================
*/
game_export_t *GetGameAPI (game_import_t *import)
{
	gi = *import;

	globals.apiversion = GAME_API_VERSION;
	globals.Init = InitGame;
	globals.Shutdown = ShutdownGame;
	globals.SpawnEntities = SpawnEntities;

	globals.WriteGame = WriteGame;
	globals.ReadGame = ReadGame;
	globals.WriteLevel = WriteLevel;
	globals.ReadLevel = ReadLevel;

	globals.ClientThink = ClientThink;
	globals.ClientConnect = ClientConnect;
	globals.ClientUserinfoChanged = ClientUserinfoChanged;
	globals.ClientDisconnect = ClientDisconnect;
	globals.ClientBegin = ClientBegin;
	globals.ClientCommand = ClientCommand;

	globals.RunFrame = G_RunFrame;

	globals.ServerCommand = ServerCommand;

	globals.edict_size = sizeof(edict_t);

	return &globals;
}

#ifndef GAME_HARD_LINKED
// this is only here so the functions in q_shared.c and q_shwin.c can link
void Sys_Error (char *error, ...)
{
	va_list		argptr;
	char		text[1024];

	va_start (argptr, error);
	vsprintf (text, error, argptr);
	va_end (argptr);

	gi.error (ERR_FATAL, "%s", text);
}

void Com_Printf (char *msg, ...)
{
	va_list		argptr;
	char		text[1024];

	va_start (argptr, msg);
	vsprintf (text, msg, argptr);
	va_end (argptr);

	gi.dprintf ("%s", text);
}

#endif

//======================================================================


/*
=================
ClientEndServerFrames
=================
*/
void ClientEndServerFrames (void)
{
	int		i;
	edict_t	*ent;

	// calc the player views now that all pushing
	// and damage has been added
	for (i=0 ; i<maxclients->value ; i++)
	{
		ent = g_edicts + 1 + i;
		if (!ent->inuse || !ent->client)
			continue;
		ClientEndServerFrame (ent);
	}

}

/*
=================
CreateTargetChangeLevel

Returns the created target changelevel
=================
*/
edict_t *CreateTargetChangeLevel(char *map)
{
	edict_t *ent;

	ent = G_Spawn ();
	ent->classname = "target_changelevel";
	Com_sprintf(level.nextmap, sizeof(level.nextmap), "%s", map);
	ent->map = level.nextmap;
	return ent;
}

/*
=================
EndDMLevel

The timelimit or fraglimit has been exceeded
=================
*/
void EndDMLevel (void)
{
	edict_t		*ent;
	char *s, *t, *f;
	static const char *seps = " ,\n\r";

	// stay on same level flag
	if ((int)dmflags->value & DF_SAME_LEVEL)
	{
		BeginIntermission (CreateTargetChangeLevel (level.mapname) );
		return;
	}

	// see if it's in the map list
	if (*sv_maplist->string) {
		s = strdup(sv_maplist->string);
		f = NULL;
		t = strtok(s, seps);
		while (t != NULL) {
			if (Q_stricmp(t, level.mapname) == 0) {
				// it's in the list, go to the next one
				t = strtok(NULL, seps);
				if (t == NULL) { // end of list, go to first one
					if (f == NULL) // there isn't a first one, same level
						BeginIntermission (CreateTargetChangeLevel (level.mapname) );
					else
						BeginIntermission (CreateTargetChangeLevel (f) );
				} else
					BeginIntermission (CreateTargetChangeLevel (t) );
				free(s);
				return;
			}
			if (!f)
				f = t;
			t = strtok(NULL, seps);
		}
		free(s);
	}

	if (level.nextmap[0]) // go to a specific map
		BeginIntermission (CreateTargetChangeLevel (level.nextmap) );
	else {	// search for a changelevel
		ent = G_Find (NULL, FOFS(classname), "target_changelevel");
		if (!ent)
		{	// the map designer didn't include a changelevel,
			// so create a fake ent that goes back to the same level
			BeginIntermission (CreateTargetChangeLevel (level.mapname) );
			return;
		}
		BeginIntermission (ent);
	}
}

/*
=================
CheckDMRules
=================
*/
void CheckDMRules (void)
{
	int			i;
	gclient_t	*cl;

	if (level.intermissiontime)
		return;

	if (!deathmatch->value)
		return;

	if (timelimit->value)
	{
		if (level.time >= timelimit->value*60)
		{
			gi.bprintf (PRINT_HIGH, "Timelimit hit.\n");
			EndDMLevel ();
			return;
		}
	}

	if (fraglimit->value)
	{
		for (i=0 ; i<maxclients->value ; i++)
		{
			cl = game.clients + i;
			if (!g_edicts[i+1].inuse)
				continue;

			if (cl->resp.score >= fraglimit->value)
			{
				gi.bprintf (PRINT_HIGH, "Fraglimit hit.\n");
				EndDMLevel ();
				return;
			}
		}
	}
}


/*
=============
ExitLevel
=============
*/
void ExitLevel (void)
{
	int		i;
	edict_t	*ent;
	char	command [256];

	Com_sprintf (command, sizeof(command), "gamemap \"%s\"\n", level.changemap);
	gi.AddCommandString (command);
	level.changemap = NULL;
	level.exitintermission = 0;
	level.intermissiontime = 0;
	ClientEndServerFrames ();

	// clear some things before going to next level
	for (i=0 ; i<maxclients->value ; i++)
	{
		ent = g_edicts + 1 + i;
		if (!ent->inuse)
			continue;
		if (ent->health > ent->client->pers.max_health)
			ent->health = ent->client->pers.max_health;
	}

}

/*
================
G_RunFrame

Advances the world by 0.1 seconds
================
*/
void G_RunFrame (void)
{
	int		i;
	edict_t	*ent;

	level.framenum++;
	level.time = level.framenum*FRAMETIME;

	// choose a client for monsters to target this frame
	AI_SetSightClient ();

	// exit intermissions

	if (level.exitintermission)
	{
		ExitLevel ();
		return;
	}

	//
	// treat each object in turn
	// even the world gets a chance to think
	//
	ent = &g_edicts[0];
	for (i=0 ; i<globals.num_edicts ; i++, ent++)
	{
		if (!ent->inuse)
			continue;

		level.current_entity = ent;

		VectorCopy (ent->s.origin, ent->s.old_origin);

		// if the ground entity moved, make sure we are still on it
		if ((ent->groundentity) && (ent->groundentity->linkcount != ent->groundentity_linkcount))
		{
			ent->groundentity = NULL;
			if ( !(ent->flags & (FL_SWIM|FL_FLY)) && (ent->svflags & SVF_MONSTER) )
			{
				M_CheckGround (ent);
			}
		}

		if (i > 0 && i <= maxclients->value)
		{
			ClientBeginServerFrame (ent);
			continue;
		}

		G_RunEntity (ent);
	}

	edict_t* player = &g_edicts[1];
	if (player->inuse && player->client) {
		int isCrouched = (player->client->ps.pmove.pm_flags & PMF_DUCKED) != 0;
		float speed = sqrtf(player->velocity[0] * player->velocity[0] + player->velocity[1] * player->velocity[1]);

		// sliding
		if (speed > 175 && isCrouched && !wasCrouched && player->groundentity) {
			isSliding = 1;
			player->velocity[0] += player->velocity[0] / speed * 450;
			player->velocity[1] += player->velocity[1] / speed * 450;
			player->velocity[2] = 0;
		}
		// reduce the effect of friction
		if (isSliding && player->groundentity) {
			player->velocity[0] *= 1.45f;
			player->velocity[1] *= 1.45f;
		}

		if ((sqrtf(player->velocity[0] * player->velocity[0] + player->velocity[1] * player->velocity[1]) < 150 && isSliding) || !isCrouched) {
			isSliding = 0;
		}
		wasCrouched = isCrouched;

		// evo
		int evoLevel = 0;
		int evoToNext = 0;
		if (player->client->damageDealt < 50) { // none
			evoLevel = 0;
			evoToNext = 50 - player->client->damageDealt;
			player->max_health = 100;
		}
		else if (player->client->damageDealt < 150) { // white
			evoLevel = 1;
			evoToNext = 150 - player->client->damageDealt;
			player->max_health = 150;
		}
		else if (player->client->damageDealt < 300) { // blue
			evoLevel = 2;
			evoToNext = 300 - player->client->damageDealt;
			player->max_health = 175;
		}
		else { // purple
			evoLevel = 3;
			evoToNext = -1;
			player->max_health = 200;
		}

		// valk passive
		if (player->client->legend == 1) {
			if (player->client->valkfuel > 0 && player->client->ps.pmove.pm_flags & PMF_JUMP_HELD) {
				player->velocity[2] = 100;
				player->client->valkfuel -= 1;
			}
			else {
				player->client->valkfuel += 3;
			}
			if (player->client->valkfuel > 100) {
				player->client->valkfuel = 100;
			}
		}

		// tac
		if (player->client->tacpressed && player->client->taccooldown <= 0.0) {
			if (player->client->legend == 0) { // octane
				player->client->tacduration = 3.0;
				player->client->taccooldown = 5.0;
			}
			else if (player->client->legend == 1) { // valkyrie
				player->client->tacduration = 2.0;
				player->client->taccooldown = 4.0;
			}
			else if (player->client->legend == 2) { // rev
				player->client->tacduration = 0.1;
				player->client->taccooldown = 4.0;
			}
		}

		// do tac
		if (player->client->tacduration > 0.0) {
			if (player->client->legend == 1 && fmod((level.time), 0.5) == 0.0) { // valk
				vec3_t forward, right, up, start;
				AngleVectors(player->client->v_angle, forward, right, up);
				VectorScale(forward, 32, start);
				start[2] += player->viewheight + 8;
				start[1] -= 12;
				VectorAdd(player->s.origin, start, start);
				fire_rocket(player, start, forward, 25, 650, 100, 25);
			}
			if (player->client->legend == 2) { // rev
				vec3_t forward, right, up;
				AngleVectors(player->client->v_angle, forward, right, up);
				forward[2] = 0;
				VectorNormalize(forward);
				player->velocity[0] = forward[0] * 600;
				player->velocity[1] = forward[1] * 600;
				player->velocity[2] = 300;
			}
		}

		// healing
		if (player->client->legend == 0 && fmod(level.time, 0.5) == 0.0 && player->health < player->max_health) {
			player->health++;
		}
		else if (fmod(level.time, 1.0) == 0.0 && player->health < player->max_health) {
			player->health++;
		}

		// dont let health go over max
		if (player->health > player->max_health) {
			player->health = player->max_health;
		}

		// "debug" text
		char text[256];
		Com_sprintf(text, sizeof(text),
			"Level Time: %.2f\n"
			"Speed: %.2f\n"
			"Vel: %.2f, %.2f, %.2f\n"
			"isSliding: %s\n"
			"Damage: %d\n"
			"ValkFuel: %d/100\n"
			"Evo Lv: %d  Next: %d\n"
			"Tac Pressed: %s\n"
			"Duration: %.1f  Cooldown: %.1f\n",
			level.time,
			sqrtf(player->velocity[0] * player->velocity[0] + player->velocity[1] * player->velocity[1]),
			player->velocity[0], player->velocity[1], player->velocity[2],
			isSliding ? "true" : "false",
			player->client->damageDealt,
			player->client->valkfuel,
			evoLevel, evoToNext,
			player->client->tacpressed ? "true" : "false", player->client->tacduration, player->client->taccooldown
		);
		gi.centerprintf(player, "%s\n", text);

		player->client->tacpressed = false;

		player->client->tacduration -= FRAMETIME;
		player->client->taccooldown -= FRAMETIME;

		if (player->client->tacduration < 0.0) {
			player->client->tacduration = 0.0;
		}
		if (player->client->taccooldown < 0.0) {
			player->client->taccooldown = 0.0;
		}


	}

	// see if it is time to end a deathmatch
	CheckDMRules ();

	// build the playerstate_t structures for all players
	ClientEndServerFrames ();
}

