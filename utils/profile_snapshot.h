#ifndef PDW_PROFILE_SNAPSHOT_H
#define PDW_PROFILE_SNAPSHOT_H
#ifndef STRICT
#define STRICT 1
#endif
#include <windows.h>
#include <mmsystem.h>
#include "../Headers/pdw.h"
#include "../Headers/sound_in.h"

inline PROFILE SnapshotProfile(const PROFILE& profile)
{
	PdwSignalDecoderStateGuard guard;
	return profile;
}
inline void RestoreProfile(PROFILE& target, const PROFILE& source)
{
	PdwSignalDecoderStateGuard guard;
	target = source;
}
#endif
