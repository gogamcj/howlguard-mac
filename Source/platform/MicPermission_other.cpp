#include "MicPermission.h"

namespace howl
{

MicPermission getMicPermission() { return MicPermission::granted; }
void requestMicPermission (std::function<void (bool)> callback) { if (callback) callback (true); }
void resetMicPermission() {}
void openMicPrivacySettings() {}

} // namespace howl
