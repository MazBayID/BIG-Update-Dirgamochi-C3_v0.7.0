#include "menu_engine.h"
#include "dirgamochi_config.h"

void MenuEngine::begin(Adafruit_SSD1306 *display, ChronosESP32 *watch, BuzzerEngine *buzzer)
{
    _display = display;
    _watch = watch;
    _buzzer = buzzer;
    loadPrefs();

    // Apply every persisted setting immediately at boot so a choice made
    // in the menu survives a power cycle without recompiling.
    if (_buzzer)
    {
        _buzzer->setEnabled(_soundMode >= 1);
        _buzzer->setAmbientEnabled(_soundMode >= 2);
        _buzzer->setVolume(_volumeLevel);
    }
    if (_display)
        _display->setRotation(_oledRotation);
    if (_watch)
        _watch->set24Hour(_hour24);
}

void MenuEngine::loadPrefs()
{
    _prefs.begin(SETTINGS_NAMESPACE, false);
    // "snd" replaced the older on/off "buz" flag; carry an old setting over
    // once so nobody's mute choice is lost on upgrade.
    _soundMode = _prefs.getUChar("snd", 255);
    if (_soundMode > 2)
        _soundMode = _prefs.getBool("buz", true) ? 2 : 0;

    _oledRotation = _prefs.getUChar("rot", OLED_ROTATION);
    if (_oledRotation != 0 && _oledRotation != 2)
        _oledRotation = OLED_ROTATION; // guard against garbage/first-run NVS

    _volumeLevel = _prefs.getUChar("vol", 1);
    if (_volumeLevel > 2)
        _volumeLevel = 1;

    _hour24 = _prefs.getBool("hr24", true);

    _idleTimeoutIdx = _prefs.getUChar("idle", 1);
    if (_idleTimeoutIdx >= IDLE_TIMEOUT_PRESET_COUNT)
        _idleTimeoutIdx = 1;

    _moodFreqIdx = _prefs.getUChar("mood", 2);
    if (_moodFreqIdx >= SPRITE_SHOW_PRESET_COUNT)
        _moodFreqIdx = 2;

    _prefs.end();
}

void MenuEngine::enter()
{
    _selected = 0;
    _statusMsg = "";
    _showingDetail = false;
}

void MenuEngine::moveNext()
{
    _selected = (_selected + 1) % MENU_COUNT;
    if (_buzzer)
        _buzzer->playClick();
}

void MenuEngine::movePrev()
{
    _selected = (_selected - 1 + MENU_COUNT) % MENU_COUNT;
    if (_buzzer)
        _buzzer->playClick();
}

void MenuEngine::setStatus(const char *msg, unsigned long durationMs)
{
    _statusMsg = msg;
    _statusUntil = millis() + durationMs;
}

void MenuEngine::activate()
{
    _prefs.begin(SETTINGS_NAMESPACE, false);
    bool silent = (_selected == MENU_DEVICE_INFO);

    switch (_selected)
    {
    case MENU_SOUND:
        _soundMode = (_soundMode + 1) % 3; // OFF -> ALRT -> ALL -> OFF
        if (_buzzer)
        {
            _buzzer->setEnabled(_soundMode >= 1);
            _buzzer->setAmbientEnabled(_soundMode >= 2);
        }
        _prefs.putUChar("snd", _soundMode);
        if (_soundMode == 0)
            silent = true; // would be muted anyway
        break;

    case MENU_VOLUME:
        _volumeLevel = (_volumeLevel + 1) % 3;
        if (_buzzer)
            _buzzer->setVolume(_volumeLevel);
        _prefs.putUChar("vol", _volumeLevel);
        break;

    case MENU_ROTATION:
        _oledRotation = (_oledRotation == 0) ? 2 : 0;
        if (_display)
            _display->setRotation(_oledRotation);
        _prefs.putUChar("rot", _oledRotation);
        setStatus("Saved");
        break;

    case MENU_TIME_FORMAT:
        _hour24 = !_hour24;
        if (_watch)
            _watch->set24Hour(_hour24);
        _prefs.putBool("hr24", _hour24);
        break;

    case MENU_IDLE_TIMEOUT:
        _idleTimeoutIdx = (_idleTimeoutIdx + 1) % IDLE_TIMEOUT_PRESET_COUNT;
        _prefs.putUChar("idle", _idleTimeoutIdx);
        break;

    case MENU_MOOD_FREQUENCY:
        _moodFreqIdx = (_moodFreqIdx + 1) % SPRITE_SHOW_PRESET_COUNT;
        _prefs.putUChar("mood", _moodFreqIdx);
        break;

    case MENU_RESET_PAIRING:
        if (_watch)
        {
            // Drop the current BLE connection/advertising and start fresh -
            // useful if the phone's Chronos app has a stale pairing entry.
            _watch->stop(false); // keep saved app-side data (name/settings)
            _watch->begin();
        }
        setStatus("Reset");
        break;

    case MENU_DEVICE_INFO:
        _showingDetail = true;
        break;

    default:
        break;
    }

    _prefs.end();

    if (_buzzer && !silent)
        _buzzer->playClick();
}

void MenuEngine::drawDeviceInfo()
{
    _display->setTextSize(1);
    _display->setTextColor(SSD1306_WHITE);
    _display->setCursor(0, 0);
    _display->print("DEVICE INFO");
    _display->drawFastHLine(0, 10, OLED_WIDTH, SSD1306_WHITE);

    _display->setCursor(0, 13);
    _display->print("Name: ");
    _display->print(DIRGA_BLE_NAME);

    _display->setCursor(0, 23);
    _display->print("BLE: ");
    _display->print(_watch && _watch->isConnected() ? "Connected" : "Advertising");

    _display->setCursor(0, 33);
    _display->print("MAC ");
    _display->print(_watch ? _watch->getAddress() : "n/a");

    _display->setCursor(0, 43);
    _display->print("FW: ");
    _display->print(DIRGA_FW_VERSION);

    _display->setCursor(0, 53);
    if (_watch && _watch->isConnected())
    {
        PhoneInfo pi = _watch->getPhoneInfo();
        String model = pi.model.length() > 0 ? pi.model : pi.manufacturer;
        _display->print("Phone: ");
        _display->print(model.substring(0, 15));
    }
    else
    {
        _display->print("Phone: n/a");
    }
}

void MenuEngine::draw()
{
    if (!_display)
        return;

    if (_showingDetail)
    {
        drawDeviceInfo();
        return;
    }

    _display->setTextSize(1);
    _display->setTextColor(SSD1306_WHITE);
    _display->setCursor(0, 0);
    _display->print("MENU");
    _display->drawFastHLine(0, 9, OLED_WIDTH, SSD1306_WHITE);

    const char *labels[MENU_COUNT] = {
        "Sound",
        "Volume",
        "Rotate",
        "Time fmt",
        "Idle time",
        "Mood freq",
        "Reset BLE",
        "Device info"};

    const int valueX = 92;
    const int visibleRows = 5;
    const int rowH = 10;
    const int firstY = 12;

    // Stateless scroll window: keeps the selected row inside the visible
    // slice, recomputed fresh every draw from _selected alone.
    int scrollOffset = _selected - visibleRows + 1;
    if (scrollOffset < 0)
        scrollOffset = 0;
    int maxOffset = MENU_COUNT - visibleRows;
    if (scrollOffset > maxOffset)
        scrollOffset = maxOffset;

    for (int row = 0; row < visibleRows; row++)
    {
        int i = scrollOffset + row;
        int y = firstY + row * rowH;
        bool isSelected = (i == _selected);

        if (isSelected)
        {
            _display->fillRect(0, y - 1, OLED_WIDTH, rowH - 1, SSD1306_WHITE);
            _display->setTextColor(SSD1306_BLACK);
        }
        else
        {
            _display->setTextColor(SSD1306_WHITE);
        }

        _display->setCursor(2, y);
        _display->print(labels[i]);

        _display->setCursor(valueX, y);
        if (isSelected && _statusMsg.length() > 0 && millis() < _statusUntil)
        {
            _display->print(_statusMsg);
        }
        else
        {
            switch (i)
            {
            case MENU_SOUND:
                _display->print(_soundMode == 0 ? "OFF" : (_soundMode == 1 ? "ALRT" : "ALL"));
                break;
            case MENU_VOLUME:
                _display->print(_volumeLevel == 0 ? "Low" : (_volumeLevel == 1 ? "Med" : "High"));
                break;
            case MENU_ROTATION:
                _display->print(_oledRotation == 2 ? "ON" : "OFF");
                break;
            case MENU_TIME_FORMAT:
                _display->print(_hour24 ? "24h" : "12h");
                break;
            case MENU_IDLE_TIMEOUT:
                _display->print(String(IDLE_TIMEOUT_PRESETS_MIN[_idleTimeoutIdx]) + "m");
                break;
            case MENU_MOOD_FREQUENCY:
            {
                const char *names[SPRITE_SHOW_PRESET_COUNT] = {"Off", "Low", "Norm", "High"};
                _display->print(names[_moodFreqIdx]);
                break;
            }
            case MENU_RESET_PAIRING:
            case MENU_DEVICE_INFO:
                _display->print("OK");
                break;
            }
        }
    }
    _display->setTextColor(SSD1306_WHITE);

    // Scroll hints - only drawn when there's actually more content that way.
    if (scrollOffset > 0)
    {
        _display->setCursor(OLED_WIDTH - 6, 0);
        _display->print("^");
    }
    if (scrollOffset + visibleRows < MENU_COUNT)
    {
        _display->setCursor(OLED_WIDTH - 6, firstY + visibleRows * rowH - rowH + 2);
        _display->print("v");
    }
}
