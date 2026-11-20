#include "MicPermission.h"

#include <juce_events/juce_events.h>
#import <AVFoundation/AVFoundation.h>

namespace howl
{

MicPermission getMicPermission()
{
    switch ([AVCaptureDevice authorizationStatusForMediaType: AVMediaTypeAudio])
    {
        case AVAuthorizationStatusAuthorized:     return MicPermission::granted;
        case AVAuthorizationStatusNotDetermined:  return MicPermission::undetermined;
        default:                                  return MicPermission::denied;
    }
}

void requestMicPermission (std::function<void (bool)> callback)
{
    [AVCaptureDevice requestAccessForMediaType: AVMediaTypeAudio
                             completionHandler: ^(BOOL granted)
                             {
                                 juce::MessageManager::callAsync ([callback, granted] { if (callback) callback (granted); });
                             }];
}

void resetMicPermission()
{
    const auto bundleId = juce::String::fromCFString ((__bridge CFStringRef) [[NSBundle mainBundle] bundleIdentifier]);
    juce::ChildProcess p;
    if (p.start (juce::StringArray { "/usr/bin/tccutil", "reset", "Microphone", bundleId }))
        p.waitForProcessToFinish (5000);
}

void openMicPrivacySettings()
{
    juce::URL ("x-apple.systempreferences:com.apple.preference.security?Privacy_Microphone").launchInDefaultBrowser();
}

} // namespace howl
