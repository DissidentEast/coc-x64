#include "stdafx.h"
#include "reward_event_generator.h"

// Hollowed (GameSpy removal, 6b): see reward_event_generator.h.
// All award submission paths required the dead online service.

namespace award_system
{

reward_event_generator::reward_event_generator(u32 const max_rewards_per_game) :
	m_max_rewards(max_rewards_per_game)
{
	m_local_player			= NULL;
	m_rewarded				= 0;
	m_state_accum			= NULL;
	m_state_event_checker	= NULL;
	m_event_handlers		= NULL;
	m_best_scores_helper	= NULL;
	m_submit_queue			= NULL;
}

reward_event_generator::~reward_event_generator()
{
}

void reward_event_generator::init_player(game_PlayerState* local_player)
{
	m_local_player = local_player;
}

void reward_event_generator::init_bone_groups(CActor* first_spawned_actor)
{
	(void)first_spawned_actor;
}

void reward_event_generator::update()
{
}

void reward_event_generator::OnWeapon_Fire(u16 sender, u16 sender_weapon_id)
{
	(void)sender; (void)sender_weapon_id;
}

void reward_event_generator::OnBullet_Fire(u16 sender, u16 sender_weapon_id, const Fvector& position, const Fvector& direction)
{
	(void)sender; (void)sender_weapon_id; (void)position; (void)direction;
}

void reward_event_generator::OnBullet_Hit(CObject const * hitter, CObject const * victim, CObject* weapon, u16 const bone)
{
	(void)hitter; (void)victim; (void)weapon; (void)bone;
}

void reward_event_generator::OnArtefactSpawned()
{
}

void reward_event_generator::OnPlayerTakeArtefact(game_PlayerState const * ps)
{
	(void)ps;
}

void reward_event_generator::OnPlayerDropArtefact(game_PlayerState const * ps)
{
	(void)ps;
}

void reward_event_generator::OnPlayerBringArtefact(game_PlayerState const * ps)
{
	(void)ps;
}

void reward_event_generator::OnPlayerSpawned(game_PlayerState const * ps)
{
	(void)ps;
}

void reward_event_generator::OnPlayerKilled(u16 killer_id, u16 target_id, u16 weapon_id, std::pair<KILL_TYPE, SPECIAL_KILL_TYPE> kill_type)
{
	(void)killer_id; (void)target_id; (void)weapon_id; (void)kill_type;
}

void reward_event_generator::OnPlayerChangeTeam(s8 team)
{
	(void)team;
}

void reward_event_generator::OnPlayerRankdChanged()
{
}

void reward_event_generator::OnRoundEnd()
{
}

void reward_event_generator::OnRoundStart()
{
	m_rewarded = 0;
}

void __stdcall reward_event_generator::AddRewardTask(u32 award_id)
{
	(void)award_id;
}

void reward_event_generator::CommitBestResults()
{
}

} //namespace award_system
