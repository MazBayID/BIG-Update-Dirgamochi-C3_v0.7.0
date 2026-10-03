#ifndef CHRONOS_UI_H
#define CHRONOS_UI_H

#include <Adafruit_SSD1306.h>
#include <ChronosESP32.h>
#include "menu_engine.h"

enum UiScreen
{
    SCR_FACE = 0,
    SCR_TIME,
    SCR_WEATHER,
    SCR_NOTIFICATIONS,
    SCR_NAVIGATION,
    SCR_MUSIC,
    SCR_PHONE,
    SCR_CONTACTS,
    SCR_QR,
    SCR_FINDING, // not part of the PLUS/MINUS cycle - only entered from OK long-press
    SCR_MENU,    // not part of the cycle - only entered via PLUS long-press
    SCR_COUNT    // keep last
};

class ChronosUI
{
public:
    void begin(Adafruit_SSD1306 *display, ChronosESP32 *watch, MenuEngine *menu);

    void nextScreen();       // PLUS short press - cycles the glanceable screens forward
    void prevScreen();       // MINUS short press - cycles backward
    void backToFace();       // jump back to the face/idle screen
    void goTo(UiScreen s);   // jump directly to a screen (e.g. OK -> SCR_PHONE)
    void openMenu();         // PLUS long press - opens the settings menu
    UiScreen current() const { return _screen; }

    // draws the current screen (except SCR_FACE, which main.cpp's sprite/
    // clock logic owns directly) - call every loop(); internally throttled
    void update();

    // Call once right when BLE connects, so the Phone screen's checkmark
    // celebration knows when its few seconds started.
    void notifyConnected();

    // Renders the clock screen without changing current()/the throttle
    // state - used for the forced ambient display during quiet hours/idle
    // timeout, which must keep ticking every second regardless of which
    // screen the user was last browsing.
    void drawTimeNow();

    // let main.cpp tell the QR screen how many links Chronos has sent, and
    // which one to show (cycled with OK while the QR screen is open)
    void setQrCount(int count) { _qrCount = count; }
    void nextQr();

    // let main.cpp tell the Contacts screen how many contacts exist, and
    // cycle which one is shown (cycled with OK while open)
    void setContactCount(int count) { _contactCount = count; }
    void nextContact();

private:
    Adafruit_SSD1306 *_display = nullptr;
    ChronosESP32 *_watch = nullptr;
    MenuEngine *_menu = nullptr;
    UiScreen _screen = SCR_FACE;
    unsigned long _lastDrawMs = 0;
    unsigned long _lastClockDrawMs = 0;

    int _qrCount = 0;
    int _qrIndex = 0;
    int _contactCount = 0;
    int _contactIndex = 0;

    unsigned long _connectCelebrationUntil = 0;

    void drawTime();
    void drawWeather();
    void drawNotifications();
    void drawNavigation();
    void drawMusic();
    void drawPhone();
    void drawQr();
    void drawContacts();
    void drawFinding();

    void header(const char *title);
};

#endif // CHRONOS_UI_H
