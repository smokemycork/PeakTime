#pragma once
// PeakTime - builds a stereo room response for a venue / listening position.
//
// The response is physically motivated:
//  * direct sound from left / right speaker stacks to the listener
//  * early reflections from an image-source model of the room (order 3),
//    with the crowd absorbing the floor and the horns' directivity reducing
//    stage-wall and ceiling energy
//  * a diffuse late tail with per-octave decay (RT60 shortened by bodies)
//    whose level against the direct sound follows the room constant /
//    critical distance of the room for the rig's directivity
//  * open-air venues get ground + sparse scatter reflections and almost no tail
// The left channel is the left stack heard at the listening position, the
// right channel the right stack (ear crosstalk is handled by the headphone
// crossfeed stage).

#include "Venues.h"
#include "Biquad.h"
#include <vector>

namespace pt
{
struct IRSettings
{
    int venueIndex = 0;
    float distanceM = 10.0f;
    float crowdFill = 0.8f;  // 0..1
    double sampleRate = 48000.0;

    bool operator== (const IRSettings& o) const
    {
        return venueIndex == o.venueIndex && distanceM == o.distanceM && crowdFill == o.crowdFill && sampleRate == o.sampleRate;
    }
    bool operator!= (const IRSettings& o) const { return ! (*this == o); }
};

struct IRResult
{
    std::vector<float> left, right;
    double sampleRate = 48000.0;
    float reverbToDirect = 0.0f;   // energy ratio (diagnostic / UI)
    float criticalDistanceM = 0.0f;
    float effectiveRtMid = 0.0f;
};

struct IRGenerator
{
    // effective RT60 for each octave band 125..8k with the crowd in the room
    static void effectiveRT (const Venue& v, float fill, float out[7]);
    static IRResult generate (const IRSettings& s);
};

} // namespace pt
