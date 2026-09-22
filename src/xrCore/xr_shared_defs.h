#pragma once

// Shared app-level constants salvaged from the deleted GameSpy
// xrGameSpy_MainDefs.h (the GameSpy online service shut down in 2014).
// Only the still-needed registry/port/version defines live here;
// everything service-related (game IDs, namespaces, QR2 ports,
// patching IDs, XRGAMESPY_API, FillSecretKey) was removed with the SDK.

#define	GAME_VERSION					"1.6.02"
#define REGISTRY_PATH					"Software\\GSC Game World\\STALKER-COP\\"

#define REGISTRY_BASE					HKEY_LOCAL_MACHINE
#define REGISTRY_VALUE_GSCDKEY			"InstallCDKEY"
#define REGISTRY_VALUE_VERSION			"InstallVers"
#define REGISTRY_VALUE_USERNAME			"InstallUserName"
#define REGISTRY_VALUE_SKU				"InstallSource"
#define REGISTRY_VALUE_INSTALL_PATCH_ID	"InstallPatchID"
#define REGISTRY_VALUE_LANGUAGE			"InstallLang"
#define REGISTRY_VALUE_USEREMAIL		"GPUserEmail"
#define REGISTRY_VALUE_USERPASSWORD		"GPUserPassword"
#define REGISTRY_VALUE_REMEMBER_PROFILE	"GPRememberMe"

#define START_PORT						0
#define END_PORT						65535
#define START_PORT_LAN					5445
#define START_PORT_LAN_SV				START_PORT_LAN + 1
#define START_PORT_LAN_CL				START_PORT_LAN + 2
#define END_PORT_LAN					START_PORT_LAN + 250

// Max unique-nick length carried over from the deleted GameSpy gp.h
// (GP_UNIQUENICK_LEN); only used to clamp player-name input fields.
#define PLAYER_NAME_MAX_LEN				21
