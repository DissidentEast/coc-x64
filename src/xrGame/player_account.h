#ifndef PLAYER_ACCOUNT_H
#define PLAYER_ACCOUNT_H

// Hollowed (GameSpy removal, 6b): the online profile service is gone
// (GameSpy shut down in 2014; CoC is single-player only). Keeps the plain
// player data and the network serialization used by game_PlayerState so
// core gameplay files compile and link unchanged. Awards/profile-store
// integration was removed with the service.

class player_account
{
public:
							player_account	();
							~player_account	();

	shared_str		const &	name			() const { return m_player_name; };
	shared_str		const &	clan_name		() const { return m_clan_name; };
	u32				const	profile_id		() const { return m_profile_id; };
	bool			const	is_clan_leader	() const { return m_clan_leader; };

	void					net_Import		(NET_Packet & P);
	void					net_Export		(NET_Packet & P);
	static			void	skip_Import		(NET_Packet & P);
	void					load_account	();
	bool					is_online		() const { return m_online_account; };

	void					set_player_name	(char const * new_name);
protected:
	shared_str						m_player_name;
	shared_str						m_clan_name;
	u32								m_profile_id;
	bool							m_clan_leader;
	bool							m_online_account;

}; //class player_account


#endif //#ifndef PLAYER_ACCOUNT_H
