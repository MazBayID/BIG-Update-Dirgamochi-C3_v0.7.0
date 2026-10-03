#ifndef SPRITE_FACE_ENGINE_H
#define SPRITE_FACE_ENGINE_H

#include <Adafruit_SSD1306.h>
#include "dirgamochi_config.h"

// Things that happened during update() that deserve a sound; main.cpp
// polls them with takeEvents() so this class stays audio-agnostic.
#define SPRITE_EV_BLINK 0x01  // eyes blinked (also fires for the 2nd blink of a double-blink)
#define SPRITE_EV_GLANCE 0x02 // eyes glanced left/right/up/down
#define SPRITE_EV_MOOD 0x04   // a full mood animation just started

// Plays the bitmap "idle mood" animations converted at build time from
// assets/faces/mochi_*.zip by tools/generate_assets.py (see
// include/generated/face_frames.h). This is the *only* face the firmware
// draws - there is no separate procedural/vector face engine. main.cpp
// still owns a handful of non-face screens for specific moments
// (notification/navigation/phone/finding-phone/the clock), which briefly
// take over the OLED and hand it back to this engine afterwards; a remote
// "petting" touch and BLE events call triggerMoodNow() to react using the
// same bitmap animations rather than a separate expression system.
//
// Behaviour, in order of priority while idle:
//   1. Rest on a static neutral frame (cheap - no redraw while nothing
//      changes, so it doesn't compete with BLE/button handling for I2C
//      time or CPU).
//   2. Every few seconds, blink (one real "eyes closed" frame from the mood
//      loops, FACE_BLINK_* in the generated header), sometimes twice in a
//      row like a real double-blink - snappy, not the full ~17s loop.
//   3. Every several seconds, glance left/right/up/down (the whole face
//      image slides a few pixels - the frames are baked bitmaps).
//   4. Every so often (much rarer), play one full, randomly chosen ~17s
//      mood animation as a little surprise, then return to resting.
// All timing is non-blocking (millis()-driven) - nothing here ever calls
// delay(), matching the rest of the firmware's loop() style.
class SpriteFaceEngine
{
public:
    void begin(Adafruit_SSD1306 *display);
    void update(bool bleConnected); // call every loop() tick while idle

    // Call once when (re-)entering the plain idle state (e.g. after a
    // notification/nav/petting episode ends), so a blink/show that became
    // "due" while this engine wasn't being ticked doesn't fire the instant
    // it regains the screen.
    void resetIdleTimers();

    // Immediately plays a mood animation (random if set<0, otherwise that
    // specific set 0..FACE_ANIM_COUNT-1), skipping the usual random-show
    // timer - used for an immediate reaction (e.g. a remote "petting"
    // touch) instead of waiting for one to come up naturally.
    void triggerMoodNow(int set = -1);

    // Overrides how often a random mood show fires while resting (the
    // "Mood frequency" menu item) - safe to call every tick; a no-op
    // unless the values actually changed.
    void setShowInterval(uint32_t minMs, uint32_t maxMs);

    // Returns (and clears) the SPRITE_EV_* bits raised since the last call.
    uint8_t takeEvents()
    {
        uint8_t e = _events;
        _events = 0;
        return e;
    }

    // Which mood set is currently (or was most recently) playing - valid
    // to read right after takeEvents() returns SPRITE_EV_MOOD.
    int currentAnimSet() const { return _animSet; }

    // True only when sitting on the neutral resting frame - not blinking,
    // glancing, or mid-mood-animation. Lets main.cpp avoid interrupting an
    // in-progress show when deciding whether to switch to the ambient clock.
    bool isResting() const { return _state == STATE_RESTING; }

private:
    enum State
    {
        STATE_RESTING,
        STATE_BLINKING,
        STATE_GLANCING,
        STATE_PLAYING_ANIM
    };

    Adafruit_SSD1306 *_display = nullptr;
    State _state = STATE_RESTING;
    bool _seeded = false;
    bool _everDrawn = false;

    unsigned long _lastFrameAt = 0;
    unsigned long _nextBlinkAt = 0;
    unsigned long _nextShowAt = 0;
    unsigned long _nextGlanceAt = 0;

    uint8_t _events = 0;
    int _glanceDirX = 0; // -1/0/+1
    int _glanceDirY = 0; // -1/0/+1
    int _glanceStep = 0;

    bool _secondBlinkPending = false; // a double-blink's follow-up is queued
    int _animSet = 0;
    int _animFrame = 0;

    uint32_t _showMinMs = SPRITE_SHOW_INTERVAL_MIN_MS;
    uint32_t _showMaxMs = SPRITE_SHOW_INTERVAL_MAX_MS;

    void drawFrame(const unsigned char *bitmap, int dx = 0, int dy = 0);
    void drawConnIcon(bool connected);
    void scheduleNextBlink();
    void scheduleNextShow();
    void scheduleNextGlance();
};

#endif // SPRITE_FACE_ENGINE_H
