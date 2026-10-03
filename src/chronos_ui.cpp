#include "chronos_ui.h"
#include "dirgamochi_config.h"
#include "text_utils.h"
#include <qrcode.h>

void ChronosUI::begin(Adafruit_SSD1306 *display, ChronosESP32 *watch, MenuEngine *menu)
{
    _display = display;
    _watch = watch;
    _menu = menu;
    _screen = SCR_FACE;
}

static bool isCyclable(UiScreen s)
{
    // SCR_FINDING and SCR_MENU are transient/modal - reached directly via
    // goTo()/openMenu(), never by stepping through with PLUS/MINUS.
    return s != SCR_FINDING && s != SCR_MENU;
}

void ChronosUI::nextScreen()
{
    int s = (int)_screen;
    do
    {
        s = (s + 1) % (int)SCR_COUNT;
    } while (!isCyclable((UiScreen)s));
    _screen = (UiScreen)s;
}

void ChronosUI::prevScreen()
{
    int s = (int)_screen;
    do
    {
        s = (s - 1 + (int)SCR_COUNT) % (int)SCR_COUNT;
    } while (!isCyclable((UiScreen)s));
    _screen = (UiScreen)s;
}

void ChronosUI::backToFace()
{
    _screen = SCR_FACE;
}

void ChronosUI::goTo(UiScreen s)
{
    if (s >= SCR_FACE && s < SCR_COUNT)
        _screen = s;
}

void ChronosUI::openMenu()
{
    _screen = SCR_MENU;
    if (_menu)
        _menu->enter();
}

void ChronosUI::notifyConnected()
{
    _connectCelebrationUntil = millis() + CONNECT_CELEBRATION_MS;
}

void ChronosUI::nextQr()
{
    if (_qrCount <= 0)
        return;
    _qrIndex = (_qrIndex + 1) % _qrCount;
}

void ChronosUI::nextContact()
{
    if (_contactCount <= 0)
        return;
    _contactIndex = (_contactIndex + 1) % _contactCount;
}

void ChronosUI::header(const char *title)
{
    _display->setTextSize(1);
    _display->setTextColor(SSD1306_WHITE);
    _display->setCursor(0, 0);
    _display->print(title);
    _display->drawFastHLine(0, 10, OLED_WIDTH, SSD1306_WHITE);
}

// ---- the "cool clock" (normal screen + forced ambient display) ----

void ChronosUI::drawTime()
{
    _display->setTextColor(SSD1306_WHITE);

    // BLE status dot, same spot the old procedural face used, so it stays
    // in a consistent place across every screen that shows one.
    int cx = 122, cy = 4;
    if (_watch->isConnected())
        _display->fillCircle(cx, cy, 3, SSD1306_WHITE);
    else
        _display->drawCircle(cx, cy, 3, SSD1306_WHITE);

    String h = _watch->getHourZ();
    String m = _watch->getTime("%M");
    String ap = _watch->getAmPmC();
    bool colonOn = (_watch->getSecond() % 2) == 0;

    _display->setTextSize(3);
    int x = 8;
    _display->setCursor(x, 12);
    _display->print(h);
    x += 2 * 18;
    _display->setCursor(x, 12);
    _display->print(colonOn ? ":" : " ");
    x += 18;
    _display->setCursor(x, 12);
    _display->print(m);

    if (ap.length() > 0)
    {
        _display->setTextSize(1);
        _display->setCursor(x + 2 * 18 + 2, 14);
        _display->print(ap);
    }

    _display->drawFastHLine(10, 42, OLED_WIDTH - 20, SSD1306_WHITE);

    _display->setTextSize(1);
    String dateLine = _watch->getTime("%a, %d %b %Y"); // e.g. "Tue, 30 Sep 2026"
    int dateX = (OLED_WIDTH - (int)dateLine.length() * 6) / 2;
    if (dateX < 0)
        dateX = 0;
    _display->setCursor(dateX, 48);
    _display->print(dateLine);

    // A slim seconds progress bar - the one piece that visibly moves every
    // second even when the colon isn't blinking, so a glance confirms the
    // clock is actually live.
    int barX = 14, barY = 58, barW = OLED_WIDTH - 28, barH = 3;
    _display->drawRect(barX, barY, barW, barH, SSD1306_WHITE);
    int fillW = (barW - 2) * _watch->getSecond() / 60;
    if (fillW > 0)
        _display->fillRect(barX + 1, barY + 1, fillW, barH - 2, SSD1306_WHITE);
}

void ChronosUI::drawTimeNow()
{
    if (!_display)
        return;
    // A live clock only needs to visibly change once a second; throttling
    // harder than the rest of the UI keeps this cheap to call every tick.
    unsigned long now = millis();
    if (now - _lastClockDrawMs < 500)
        return;
    _lastClockDrawMs = now;

    _display->clearDisplay();
    drawTime();
    _display->display();
}

// ---- other screens ----

void ChronosUI::drawWeather()
{
    header("WEATHER");
    int n = _watch->getWeatherCount();
    if (n <= 0)
    {
        _display->setCursor(0, 25);
        _display->print("No weather data yet");
        return;
    }
    Weather w = _watch->getWeatherAt(0);
    _display->setCursor(0, 15);
    _display->print(sanitizeAsciiOled(_watch->getWeatherCity()));
    _display->setTextSize(2);
    _display->setCursor(0, 28);
    _display->print(String(w.temp) + "C");
    _display->setTextSize(1);
    _display->setCursor(0, 50);
    _display->print("H:" + String(w.high) + " L:" + String(w.low));
}

void ChronosUI::drawNotifications()
{
    header("NOTIFICATIONS");
    int n = _watch->getNotificationCount();
    if (n <= 0)
    {
        _display->setCursor(0, 25);
        _display->print("No notifications");
        return;
    }

    Notification note = _watch->getNotificationAt(0);

    _display->setCursor(0, 13);
    String app = sanitizeAsciiOled(note.app);
    if (app.length() > 15) app = app.substring(0, 15);
    _display->print(app);
    _display->setCursor(106, 13);
    _display->print("1/");
    _display->print(n);

    String content = sanitizeAsciiOled(note.message);
    if (content.length() == 0)
        content = sanitizeAsciiOled(note.title);
    content.trim();

    const int maxChars = 21;
    int pos = 0;
    int y = 25;
    for (int lineNo = 0; lineNo < 4 && pos < (int)content.length(); ++lineNo)
    {
        int remaining = content.length() - pos;
        int take = min(maxChars, remaining);

        if (remaining > maxChars && lineNo < 3)
        {
            int space = content.lastIndexOf(' ', pos + take - 1);
            if (space > pos + 7)
                take = space - pos;
        }

        String line = content.substring(pos, pos + take);
        line.trim();
        if (lineNo == 3 && pos + take < (int)content.length())
        {
            if (line.length() > 18) line = line.substring(0, 18);
            line += "...";
        }

        _display->setCursor(0, y);
        _display->print(line);
        y += 9;
        pos += take;
        while (pos < (int)content.length() && content[pos] == ' ')
            pos++;
    }
}

void ChronosUI::drawNavigation()
{
    header("NAVIGATION");
    Navigation nav = _watch->getNavigation();

    if (!nav.active)
    {
        _display->setCursor(0, 27);
        _display->print("No navigation");
        return;
    }

    if (nav.hasIcon)
    {
        for (int y = 0; y < 48; ++y)
        {
            for (int x = 0; x < 48; ++x)
            {
                int byteIndex = (y * 48 + x) / 8;
                int bitPos = 7 - (x % 8);
                if ((nav.icon[byteIndex] >> bitPos) & 0x01)
                    _display->drawPixel(x, 14 + y, SSD1306_WHITE);
            }
        }
    }
    else
    {
        _display->setCursor(4, 30);
        _display->print(">");
    }

    const int x = 54;
    String dir = sanitizeAsciiOled(nav.directions);
    String title = sanitizeAsciiOled(nav.title);
    if (dir.length() == 0) dir = title;
    dir.trim();

    _display->setTextSize(1);
    _display->setCursor(x, 14);
    _display->print(title.substring(0, 11));

    _display->setCursor(x, 27);
    _display->print(dir.substring(0, 11));

    _display->setCursor(x, 40);
    _display->print(sanitizeAsciiOled(nav.distance).substring(0, 11));

    _display->setCursor(x, 52);
    _display->print(sanitizeAsciiOled(nav.duration).substring(0, 11));
}

void ChronosUI::drawMusic()
{
    header("MUSIC");
    MusicInfo m = _watch->getMusicInfo();
    _display->setCursor(0, 14);
    _display->print(m.state ? "Playing" : "Paused");
    _display->setCursor(0, 28);
    _display->print(sanitizeAsciiOled(m.title).substring(0, 21));
    _display->setCursor(0, 40);
    _display->print(sanitizeAsciiOled(m.artist).substring(0, 21));
}

void ChronosUI::drawPhone()
{
    bool justConnected = _watch->isConnected() && millis() < _connectCelebrationUntil;

    if (justConnected)
    {
        // Big checkmark-in-circle + "Connected", for a few seconds only -
        // then this screen reverts to the normal info below even though
        // still connected.
        const int cx = OLED_WIDTH / 2;
        const int cy = 25;
        const int r = 20;
        _display->drawCircle(cx, cy, r, SSD1306_WHITE);
        _display->drawCircle(cx, cy, r - 1, SSD1306_WHITE);

        for (int off = 0; off <= 1; off++)
        {
            _display->drawLine(cx - 10, cy + 2 + off, cx - 3, cy + 9 + off, SSD1306_WHITE);
            _display->drawLine(cx - 3, cy + 9 + off, cx + 12, cy - 10 + off, SSD1306_WHITE);
        }

        _display->setTextSize(1);
        _display->setTextColor(SSD1306_WHITE);
        _display->setCursor(cx - 27, 52); // "Connected" = 9 chars * 6px
        _display->print("Connected");
        return;
    }

    header("PHONE");
    _display->setCursor(0, 14);
    _display->print(_watch->isConnected() ? "BLE: Connected" : "BLE: Waiting...");
    _display->setCursor(0, 26);
    _display->print("Batt: " + String(_watch->getPhoneBattery()) + "%");
    _display->setCursor(0, 38);
    _display->print(_watch->isPhoneCharging() ? "Charging" : "On battery");
    _display->setCursor(0, 52);
    _display->print("Hold OK: find phone");
}

void ChronosUI::drawContacts()
{
    header("CONTACTS");
    if (_contactCount <= 0)
    {
        _display->setCursor(0, 25);
        _display->print("No contacts synced");
        _display->setCursor(0, 40);
        _display->print("(set in Chronos app)");
        return;
    }

    Contact c = _watch->getContact(_contactIndex);
    bool isSos = _contactIndex == _watch->getSOSContactIndex();

    _display->setCursor(0, 16);
    _display->print(isSos ? "SOS contact:" : "Contact:");
    _display->setTextSize(1);
    _display->setCursor(0, 30);
    _display->print(sanitizeAsciiOled(c.name).substring(0, 21));
    _display->setCursor(0, 42);
    _display->print(sanitizeAsciiOled(c.number).substring(0, 21));

    _display->setCursor(100, 0);
    _display->fillRect(90, 0, OLED_WIDTH - 90, 8, SSD1306_BLACK);
    _display->setCursor(90, 0);
    _display->print(String(_contactIndex + 1) + "/" + String(_contactCount));
}

void ChronosUI::drawFinding()
{
    _display->setTextSize(1);
    _display->setTextColor(SSD1306_WHITE);
    _display->setCursor(16, 20);
    _display->print("Finding Phone...");
    // Three growing rings, like a ping/radar pulse, purely decorative.
    unsigned long t = millis() / 300;
    for (int i = 0; i < 3; i++)
    {
        int r = ((t + i) % 4) * 5 + 4;
        _display->drawCircle(OLED_WIDTH / 2, 44, r, SSD1306_WHITE);
    }
}

void ChronosUI::drawQr()
{
    _display->setTextSize(1);
    _display->setTextColor(SSD1306_WHITE);

    if (_qrCount <= 0)
    {
        _display->setCursor(0, 25);
        _display->print("No QR from phone yet");
        _display->setCursor(0, 40);
        _display->print("(Chronos app: share");
        _display->setCursor(0, 50);
        _display->print(" a QR/link to watch)");
        return;
    }

    String link = _watch->getQrAt(_qrIndex);

    // Auto-pick the smallest QR version (ECC_LOW - most capacity per
    // module) that fits the link, then the largest integer pixel-per-module
    // scale that still fits the 64px-tall OLED - maximizes size on a small
    // 0.96" screen instead of a fixed small code with a lot of wasted
    // margin around it.
    QRCode qrcode;
    uint8_t buf[qrcode_getBufferSize(6)]; // big enough for every version tried below
    bool ok = false;
    for (uint8_t version = 2; version <= 6 && !ok; version++)
        ok = qrcode_initText(&qrcode, buf, version, ECC_LOW, link.c_str()) == 0;

    if (!ok)
    {
        _display->setCursor(0, 25);
        _display->print("Link too long");
        _display->setCursor(0, 37);
        _display->print("to show as a QR");
        return;
    }

    int scale = OLED_HEIGHT / qrcode.size;
    if (scale < 1)
        scale = 1;
    int qrPx = qrcode.size * scale;
    int originX = (OLED_WIDTH - qrPx) / 2;
    int originY = (OLED_HEIGHT - qrPx) / 2;

    // Normal polarity (lit modules on the regular black OLED background) -
    // bigger than before, but not color-inverted.
    for (uint8_t y = 0; y < qrcode.size; y++)
    {
        for (uint8_t x = 0; x < qrcode.size; x++)
        {
            if (qrcode_getModule(&qrcode, x, y))
                _display->fillRect(originX + x * scale, originY + y * scale, scale, scale, SSD1306_WHITE);
        }
    }

    if (_qrCount > 1)
    {
        _display->setCursor(2, 2);
        _display->print(String(_qrIndex + 1) + "/" + String(_qrCount));
    }
}

void ChronosUI::update()
{
    if (_screen == SCR_FACE)
        return; // main.cpp's sprite/clock logic handles drawing + display() itself

    unsigned long now = millis();
    if (now - _lastDrawMs < 200)
        return;
    _lastDrawMs = now;

    _display->clearDisplay();
    switch (_screen)
    {
    case SCR_TIME:
        drawTime();
        break;
    case SCR_WEATHER:
        drawWeather();
        break;
    case SCR_NOTIFICATIONS:
        drawNotifications();
        break;
    case SCR_NAVIGATION:
        drawNavigation();
        break;
    case SCR_MUSIC:
        drawMusic();
        break;
    case SCR_PHONE:
        drawPhone();
        break;
    case SCR_CONTACTS:
        drawContacts();
        break;
    case SCR_QR:
        drawQr();
        break;
    case SCR_FINDING:
        drawFinding();
        break;
    case SCR_MENU:
        if (_menu)
            _menu->draw();
        break;
    default:
        break;
    }
    _display->display();
}
