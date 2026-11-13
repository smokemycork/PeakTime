#pragma once
// PeakTime - venue + sound-system models.
//
// Dimensions, reverb times and rig character are acoustic ESTIMATES built from
// public information (capacity, photos, published specs, the type of system the
// room is known for). They are not official measurements and PeakTime is not
// affiliated with any venue. Treat each one as "a room of this size, build and
// rig", which is exactly what you need to test whether a mix translates.

#include <array>
#include <cstddef>
#include <cmath>

namespace pt
{
enum class Category { Club = 0, Arena, Festival };

enum class RigType
{
    HornLoaded = 0, // point-source horn-loaded stacks, flown or stacked tops
    LineArray,      // flown line arrays + sub arrays (arenas, festivals, superclubs)
    Audiophile,     // custom tuned, very flat, extremely deep sub, low distortion
    Vintage,        // older horn rig: mid forward, rolled top, punchy upper bass
    Bodysonic,      // floor-coupled low end (tactile bass transducers)
    Compact         // small bar / lounge system
};

struct RigSpec
{
    const char* name;
    float lowCutHz;      // system low-frequency extension (-3 dB)
    float subTuneHz;     // sub enclosure tuning bump
    float subBumpDb;
    float subQ;
    float xoverHz;       // sub / top crossover
    float presenceHz;    // horn loading / mid character
    float presenceDb;
    float hfShelfDb;     // 10 kHz shelf
    float hfCutHz;       // top-end limit of the boxes
    float topHeightM;    // typical height of the tops
    float directivityQ;  // how focused the rig is (higher = less room excitation)
    float hornDrive;     // how much horn distortion character when pushed
};

inline const RigSpec& rigSpec (RigType t)
{
    static const std::array<RigSpec, 6> specs { {
        { "Horn-loaded point source", 32.f, 50.f, 2.5f, 1.1f, 100.f, 1800.f, 1.5f, -1.0f, 18000.f, 4.5f,  8.f, 1.0f },
        { "Flown line arrays",        28.f, 45.f, 2.0f, 0.9f,  90.f, 3000.f, 0.5f, -1.5f, 17000.f, 9.0f, 12.f, 0.6f },
        { "Custom audiophile rig",    24.f, 38.f, 3.0f, 0.8f,  85.f, 2500.f, 0.3f,  0.0f, 20000.f, 4.0f,  6.f, 0.4f },
        { "Vintage horn system",      38.f, 60.f, 3.0f, 1.3f, 110.f, 1200.f, 2.5f, -3.0f, 14000.f, 4.0f,  5.f, 1.3f },
        { "Tactile floor + horns",    30.f, 45.f, 3.5f, 1.0f,  95.f, 2000.f, 1.0f, -0.5f, 18000.f, 4.0f,  6.f, 0.8f },
        { "Compact install",          55.f, 75.f, 2.0f, 1.2f, 120.f, 2500.f, 2.0f, -2.0f, 16000.f, 2.5f,  4.f, 1.2f },
    } };
    return specs[(size_t) t];
}

struct Venue
{
    const char* name;
    const char* city;
    Category category;
    int capacity;
    float lengthM, widthM, heightM;   // stage wall -> back wall, side to side, floor -> ceiling (0 = open sky)
    float rtLow, rtMid, rtHigh;       // empty-room RT60 (s) at ~125 Hz, 1 kHz, 8 kHz
    bool outdoor;
    RigType rig;
    float subTrimDb;                  // venue-specific low end tuning on top of the rig
    float stereoWidth;                // 0 = mono summed system, 1 = full stereo image
    float wallAbsorb;                 // 0 = bare concrete / tile, 1 = fully treated
    float defaultDistanceM;
    float defaultFill;                // 0..1
    float defaultCrowdDb;             // crowd vs system
    int defaultSystem;                // 0 clean, 1 working, 2 red line
    const char* room;                 // short description shown in the UI
};

// NOTE: keep the order stable once released - the plugin stores the venue index.
inline const std::array<Venue, 40>& venues()
{
    static const std::array<Venue, 40> v { {
        // ------------------------------------------------------------------ CLUBS
        { "Pacha", "Ibiza", Category::Club, 3000, 28, 24, 8, 1.2f, 0.9f, 0.6f, false, RigType::HornLoaded, 1.0f, 0.6f, 0.35f, 12, 0.85f, -26, 1,
          "Main room ringed by balconies, reflective white walls over a packed floor. Warm, bouncy, big-room club sound." },
        { "Stereo", "Montreal", Category::Club, 900, 22, 16, 5, 0.6f, 0.45f, 0.35f, false, RigType::Audiophile, 2.0f, 0.3f, 0.75f, 8, 0.8f, -28, 1,
          "Legendary after-hours room. Heavily treated, dry and loud, with a deep, controlled low end that exposes every kick and bass choice." },
        { "Space - Discoteca", "Ibiza", Category::Club, 5000, 34, 28, 9, 1.4f, 1.0f, 0.7f, false, RigType::Vintage, 1.0f, 0.5f, 0.3f, 15, 0.9f, -24, 2,
          "The classic Space main room (1989-2016). Hard surfaces, long nights, a punchy mid-forward rig pushed hard." },
        { "Club Space - Terrace", "Miami", Category::Club, 2500, 30, 25, 0, 0.5f, 0.4f, 0.3f, true, RigType::HornLoaded, 1.5f, 0.5f, 0.5f, 12, 0.8f, -24, 1,
          "Open-roof terrace surrounded by walls and bars. Little low-end build-up, fast slap from the side walls, sunrise sets." },
        { "Amnesia - Main Room", "Ibiza", Category::Club, 5000, 32, 30, 10, 1.3f, 1.0f, 0.7f, false, RigType::HornLoaded, 1.5f, 0.6f, 0.35f, 14, 0.9f, -24, 2,
          "Huge, high-ceilinged main room with a powerful horn rig and a dense crowd. Lots of energy in the low mids." },
        { "Hi Ibiza - Theatre", "Ibiza", Category::Club, 5000, 30, 26, 12, 1.0f, 0.8f, 0.55f, false, RigType::LineArray, 1.0f, 0.7f, 0.55f, 14, 0.85f, -26, 1,
          "Modern superclub hall, treated and precise. Clean, wide and very even across the floor." },
        { "Ushuaia", "Ibiza", Category::Club, 7000, 60, 45, 0, 0.4f, 0.35f, 0.25f, true, RigType::LineArray, 0.5f, 0.8f, 0.5f, 25, 0.85f, -22, 1,
          "Open-air hotel arena. No roof to hold the bass, reflections only from the hotel walls behind the crowd." },
        { "DC-10 - Main Room", "Ibiza", Category::Club, 1500, 20, 16, 4.5f, 0.8f, 0.6f, 0.45f, false, RigType::HornLoaded, 2.0f, 0.3f, 0.3f, 7, 0.95f, -24, 2,
          "Low ceiling, rammed floor, rig run hot. Bass and kick fight for space; any mud in the low mids shows instantly." },
        { "Berghain - Main Floor", "Berlin", Category::Club, 1500, 30, 25, 18, 2.2f, 1.7f, 1.0f, false, RigType::HornLoaded, 2.0f, 0.4f, 0.15f, 12, 0.85f, -28, 1,
          "Former power-plant turbine hall. Raw concrete, very high ceiling, a long dark tail and a rig with serious weight." },
        { "Tresor - Vault", "Berlin", Category::Club, 600, 24, 10, 3.2f, 0.9f, 0.7f, 0.5f, false, RigType::HornLoaded, 1.0f, 0.2f, 0.1f, 8, 0.9f, -26, 2,
          "Low concrete basement vault. Close, loud, almost mono, with hard early reflections from the ceiling." },
        { "fabric - Room One", "London", Category::Club, 1600, 24, 18, 5, 0.9f, 0.7f, 0.5f, false, RigType::Bodysonic, 1.5f, 0.4f, 0.3f, 9, 0.85f, -26, 1,
          "Brick-arched railway vaults with a tactile floor. The low end is felt as much as heard." },
        { "Ministry of Sound - The Box", "London", Category::Club, 1500, 24, 20, 8, 0.6f, 0.5f, 0.4f, false, RigType::Audiophile, 1.0f, 0.5f, 0.8f, 10, 0.85f, -26, 1,
          "Purpose-built, acoustically isolated box. Tight, controlled and brutally honest." },
        { "Printworks - Press Halls", "London", Category::Club, 6000, 70, 30, 16, 2.6f, 2.0f, 1.2f, false, RigType::LineArray, 0.5f, 0.7f, 0.12f, 25, 0.9f, -22, 2,
          "Cavernous former printing hall. Long reverberant tail, strong back-wall return; big drops bloom, busy mixes smear." },
        { "Drumsheds", "London", Category::Club, 15000, 90, 50, 14, 2.4f, 1.8f, 1.1f, false, RigType::LineArray, 0.5f, 0.8f, 0.15f, 30, 0.9f, -22, 2,
          "Enormous warehouse complex. Delay-fill sound, long tail; a test of how a track survives at scale." },
        { "Warehouse Project - Depot", "Manchester", Category::Club, 10000, 80, 45, 12, 2.2f, 1.7f, 1.0f, false, RigType::LineArray, 0.5f, 0.7f, 0.15f, 30, 0.9f, -22, 2,
          "Former railway depot. Industrial, reverberant, and loud from front to back." },
        { "Fabrik", "Madrid", Category::Club, 5000, 40, 35, 14, 1.6f, 1.3f, 0.8f, false, RigType::LineArray, 1.5f, 0.7f, 0.3f, 18, 0.85f, -22, 2,
          "Big-box techno hall with a famously heavy system and a crowd that likes it loud." },
        { "Rex Club", "Paris", Category::Club, 750, 18, 14, 4, 0.7f, 0.55f, 0.4f, false, RigType::Audiophile, 1.5f, 0.4f, 0.6f, 7, 0.85f, -28, 1,
          "Intimate basement with a reference-grade rig. Detailed and close; small details are audible." },
        { "Fuse", "Brussels", Category::Club, 2000, 20, 14, 4.5f, 0.8f, 0.6f, 0.45f, false, RigType::HornLoaded, 1.5f, 0.4f, 0.35f, 8, 0.85f, -26, 1,
          "Classic techno club, low ceiling, horn rig. Punchy and direct." },
        { "Womb", "Tokyo", Category::Club, 1000, 20, 16, 8, 0.8f, 0.6f, 0.45f, false, RigType::Audiophile, 1.0f, 0.5f, 0.6f, 8, 0.8f, -28, 1,
          "Tall main floor with a precisely tuned rig. Clean and articulate even when loud." },
        { "Zouk", "Singapore", Category::Club, 3000, 28, 22, 9, 0.9f, 0.7f, 0.5f, false, RigType::LineArray, 1.0f, 0.6f, 0.5f, 10, 0.85f, -24, 1,
          "Modern superclub main room. Polished and even with plenty of headroom." },
        { "Echostage", "Washington DC", Category::Club, 3000, 60, 45, 14, 1.8f, 1.4f, 0.9f, false, RigType::LineArray, 1.0f, 0.8f, 0.3f, 22, 0.9f, -22, 1,
          "Arena-scale club hall. Wide stereo arrays, big low end and a noticeable room tail at the back." },
        { "Brooklyn Mirage", "New York", Category::Club, 6000, 70, 50, 0, 0.5f, 0.4f, 0.3f, true, RigType::LineArray, 0.5f, 0.8f, 0.5f, 28, 0.85f, -22, 1,
          "Open-air courtyard with walls on three sides. Slap from the walls, low end escapes upward." },
        { "Exchange LA", "Los Angeles", Category::Club, 1500, 30, 22, 12, 1.9f, 1.5f, 1.0f, false, RigType::HornLoaded, 1.0f, 0.5f, 0.1f, 12, 0.85f, -24, 1,
          "Former stock exchange: stone, marble and a tall ceiling. Live and splashy up top." },
        { "Coda", "Toronto", Category::Club, 1000, 28, 18, 6, 0.8f, 0.6f, 0.45f, false, RigType::Audiophile, 2.0f, 0.4f, 0.55f, 10, 0.85f, -26, 1,
          "Toronto's underground institution. Big custom rig in a mid-sized room: clean, deep and loud." },
        { "Rebel", "Toronto", Category::Club, 3500, 50, 35, 12, 1.5f, 1.2f, 0.8f, false, RigType::LineArray, 1.0f, 0.7f, 0.3f, 20, 0.85f, -22, 1,
          "Large waterfront hall. Big-room sound with a real tail at the back of the floor." },
        { "New City Gas", "Montreal", Category::Club, 4000, 50, 30, 14, 1.8f, 1.4f, 0.9f, false, RigType::LineArray, 1.0f, 0.7f, 0.15f, 20, 0.85f, -22, 1,
          "Restored 1800s gas plant. Brick and steel: lively mids and a long tail." },
        { "Bassiani", "Tbilisi", Category::Club, 1200, 30, 15, 6, 1.6f, 1.3f, 0.8f, false, RigType::HornLoaded, 2.0f, 0.3f, 0.08f, 10, 0.9f, -24, 2,
          "Former swimming pool under a stadium. Tiled, hard and resonant, run hot all night." },
        { "Generic small bar", "Anywhere", Category::Club, 150, 12, 8, 3.2f, 0.6f, 0.5f, 0.4f, false, RigType::Compact, 0.0f, 0.6f, 0.4f, 5, 0.6f, -18, 1,
          "Small bar with an install system. Limited low end: tells you if your track still works with no sub." },
        // ------------------------------------------------------------------ ARENAS
        { "Madison Square Garden", "New York", Category::Arena, 19500, 120, 95, 40, 2.6f, 2.0f, 1.2f, false, RigType::LineArray, 0.5f, 0.85f, 0.35f, 45, 0.9f, -20, 1,
          "Bowl arena. Clear direct sound from the arrays, a distinct return off the far wall from the floor." },
        { "Ziggo Dome", "Amsterdam", Category::Arena, 17000, 110, 90, 35, 2.2f, 1.7f, 1.0f, false, RigType::LineArray, 0.5f, 0.85f, 0.4f, 40, 0.9f, -20, 1,
          "Purpose-built music arena, better controlled than most. Big, wide and clean." },
        { "Alexandra Palace - Great Hall", "London", Category::Arena, 10000, 115, 40, 25, 3.5f, 2.6f, 1.4f, false, RigType::LineArray, 0.5f, 0.7f, 0.1f, 40, 0.9f, -20, 1,
          "Victorian glass-roofed hall, notoriously reverberant. Low mids pile up and transients soften." },
        { "Scotiabank Arena", "Toronto", Category::Arena, 19800, 115, 90, 38, 2.4f, 1.9f, 1.1f, false, RigType::LineArray, 0.5f, 0.85f, 0.35f, 45, 0.9f, -20, 1,
          "Hockey arena configured for shows. Hard ice-level surfaces and a long return from the far end." },
        // ------------------------------------------------------------------ FESTIVALS / OPEN AIR
        { "Tomorrowland - Mainstage", "Boom", Category::Festival, 60000, 250, 200, 0, 0.5f, 0.4f, 0.3f, true, RigType::LineArray, 0.5f, 0.9f, 0.5f, 60, 0.9f, -18, 1,
          "Massive open-air bowl with delay towers. Nothing to reflect off: subs fall away, air eats the top end at distance." },
        { "Ultra - Mainstage", "Miami", Category::Festival, 55000, 220, 160, 0, 0.5f, 0.4f, 0.3f, true, RigType::LineArray, 0.5f, 0.9f, 0.5f, 55, 0.9f, -18, 2,
          "Bayfront park field. Humid air, buildings far behind, rig driven hard." },
        { "EDC Las Vegas - kineticFIELD", "Las Vegas", Category::Festival, 80000, 300, 250, 0, 0.5f, 0.4f, 0.3f, true, RigType::LineArray, 0.5f, 0.9f, 0.5f, 70, 0.9f, -18, 2,
          "Speedway infield. Huge distances, dry desert air and a lot of rig." },
        { "Awakenings - Area V", "Spaarnwoude", Category::Festival, 30000, 150, 120, 0, 0.5f, 0.4f, 0.3f, true, RigType::LineArray, 1.0f, 0.85f, 0.5f, 45, 0.9f, -18, 1,
          "Open-air techno arena in a lakeside park. Flat field, nothing behind the crowd but sky." },
        { "Creamfields - Steel Yard", "Daresbury", Category::Festival, 15000, 100, 60, 22, 2.0f, 1.5f, 0.9f, false, RigType::LineArray, 1.0f, 0.8f, 0.2f, 35, 0.9f, -20, 2,
          "Giant steel and canvas structure. Indoor-style tail with outdoor-scale distances." },
        { "Movement - Hart Plaza", "Detroit", Category::Festival, 10000, 90, 70, 0, 0.8f, 0.6f, 0.4f, true, RigType::LineArray, 1.0f, 0.8f, 0.3f, 30, 0.85f, -20, 1,
          "Concrete riverside plaza amphitheatre. Hard ground and steps give an outdoor slap." },
        { "Forest clearing", "Open air", Category::Festival, 5000, 80, 60, 0, 0.6f, 0.5f, 0.35f, true, RigType::HornLoaded, 1.0f, 0.7f, 0.6f, 25, 0.8f, -20, 1,
          "Woodland stage. Trees scatter the sound into a soft diffuse wash; grass soaks up the highs." },
        { "Beach stage", "Open air", Category::Festival, 8000, 120, 90, 0, 0.4f, 0.3f, 0.25f, true, RigType::LineArray, 0.5f, 0.8f, 0.7f, 35, 0.85f, -20, 1,
          "Sand underfoot, sea behind. The driest, most exposed listening position there is." },
    } };
    return v;
}

inline int numVenues() { return (int) venues().size(); }

inline const char* categoryName (Category c)
{
    switch (c)
    {
        case Category::Club:     return "Club";
        case Category::Arena:    return "Arena";
        case Category::Festival: return "Festival";
    }
    return "";
}

// longest usable listening distance for a venue
inline float maxDistanceFor (const Venue& v) { return v.outdoor ? std::fmin (v.lengthM - 2.0f, 150.0f) : std::fmax (2.0f, v.lengthM - 2.0f); }

} // namespace pt
