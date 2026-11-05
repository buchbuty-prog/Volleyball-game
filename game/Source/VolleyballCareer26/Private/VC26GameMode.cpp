#include "VC26GameMode.h"

#include "VC26Arena.h"
#include "VC26Hud.h"

AVC26GameMode::AVC26GameMode()
{
	DefaultPawnClass = AVC26Arena::StaticClass();
	HUDClass = AVC26Hud::StaticClass();
}
