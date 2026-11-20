#pragma once

#include <functional>

namespace howl
{

// macOS microphone privacy (TCC). On other platforms access is always reported as granted.
enum class MicPermission { granted, denied, undetermined };

MicPermission getMicPermission();

// Shows the system prompt if the user hasn't answered yet. The callback runs on the message thread.
void requestMicPermission (std::function<void (bool granted)> callback);

// Forgets a stale grant (e.g. from an older build with a different signature) so macOS asks again.
void resetMicPermission();

void openMicPrivacySettings();

} // namespace howl
