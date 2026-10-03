/*
 * Dirgamochi-C3
 * A DasaiMochi-style desk companion firmware for the ESP32-C3 Super Mini,
 * with a bitmap mood-animation face and a Chronos BLE data link
 * (see fbiego/chronos-esp32).
 *
 * v0.7.1
 * Ambient clock state fix:
 *  - Any button activity immediately wakes the face.
 *  - ambientIsSleepy is recalculated while the face is resting.
 *  - Prevents the clock from becoming permanently stuck.
 */

#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <ChronosESP32.h>

#include "dirgamochi_config.h"
#include "sprite_face_engine.h"
#include "generated/voice_clips.h"
#include "chronos_ui.h"
#include "button_engine.h"
#include "audio_engine.h"
#include "buzzer_engine.h"
#include "menu_engine.h"

Adafruit_SSD1306 display(OLED_WIDTH, OLED_HEIGHT, &Wire, -1);
ChronosESP32 watch;
SpriteFaceEngine spriteFace;
ChronosUI ui;
AudioEngine audio;
BuzzerEngine buzzer;
MenuEngine menu;

TouchButton btnOk;
TouchButton btnPlus;
TouchButton btnMinus;

bool oledReady = false;
bool bleConnected = false;
unsigned long notificationUntil = 0;
unsigned long navigationUntil = 0;
unsigned long connectCelebUntil = 0;
unsigned long findingPhoneUntil = 0;
bool spriteActive = false;

int qrLinkCount = 0;

bool callRinging = false;
unsigned long navCheckAt = 0;
String navLastKey;

bool prevTouchState = false;
bool prevAlarmActive = false;
bool ambientIsSleepy = false;
unsigned long lastActivityMs = 0;

// ---------- Navigation direction -> sound ----------

static bool textHasAny(const String &text, const char *const *words, int count)
{
    for (int i = 0; i < count; i++)
        if (text.indexOf(words[i]) >= 0)
            return true;
    return false;
}

NavTurn classifyNavTurn(const Navigation &nav)
{
    String text = nav.directions + " " + nav.title;
    text.toLowerCase();

    static const char *const uturn[] = {"u-turn", "u turn", "putar balik", "balik arah"};
    static const char *const arriveStrong[] = {"arrive", "tiba"};
    static const char *const left[] = {"left", "kiri"};
    static const char *const right[] = {"right", "kanan"};
    static const char *const arriveWeak[] = {"destination", "tujuan"};
    static const char *const straight[] = {"straight", "lurus", "continue", "terus"};

    if (textHasAny(text, uturn, 4))
        return NAV_TURN_UTURN;
    if (textHasAny(text, arriveStrong, 2))
        return NAV_TURN_ARRIVE;
    if (textHasAny(text, left, 2))
        return NAV_TURN_LEFT;
    if (textHasAny(text, right, 2))
        return NAV_TURN_RIGHT;
    if (textHasAny(text, arriveWeak, 2))
        return NAV_TURN_ARRIVE;
    if (textHasAny(text, straight, 4))
        return NAV_TURN_STRAIGHT;

    if (nav.hasIcon)
    {
        long sumX = 0, lit = 0;

        for (int y = 0; y < 48; ++y)
            for (int x = 0; x < 48; ++x)
                if ((nav.icon[(y * 48 + x) / 8] >> (7 - (x % 8))) & 0x01)
                {
                    sumX += x;
                    lit++;
                }

        if (lit > 20)
        {
            float meanX = (float)sumX / (float)lit;

            if (meanX < 21.0f)
                return NAV_TURN_LEFT;
            if (meanX > 26.0f)
                return NAV_TURN_RIGHT;

            return NAV_TURN_STRAIGHT;
        }
    }

    return NAV_TURN_UNKNOWN;
}

// ---------- Chronos callbacks ----------

void onConnectionChange(bool state)
{
    bleConnected = state;
    lastActivityMs = millis();
    ambientIsSleepy = false;

    if (state)
    {
        connectCelebUntil = millis() + CONNECT_CELEBRATION_MS;
        ui.notifyConnected();

        if (oledReady)
            ui.goTo(SCR_PHONE);

        watch.syncRequest();
        watch.setNotifyBattery(true);
    }
    else
    {
        connectCelebUntil = 0;
    }
}

void onNotification(Notification notification)
{
    notificationUntil = millis() + 6000UL;
    lastActivityMs = millis();
    ambientIsSleepy = false;

    if (oledReady)
        ui.goTo(SCR_NOTIFICATIONS);

    if (menu.buzzerEnabled())
        buzzer.playNotification();

    Serial.printf("[NOTIF] icon=0x%02X app=%s title=%s message=%s\n",
                  notification.icon,
                  notification.app.c_str(),
                  notification.title.c_str(),
                  notification.message.c_str());
}

void onRinger(String caller, bool state)
{
    lastActivityMs = millis();
    ambientIsSleepy = false;

    if (state)
    {
        callRinging = true;

        if (menu.buzzerEnabled())
            buzzer.playCall();
    }
    else if (callRinging)
    {
        callRinging = false;
        buzzer.stop();
    }
}

void onConfig(Config config, uint32_t a, uint32_t b)
{
    Serial.printf("[CONFIG] id=%u a=%lu b=%lu\n",
                  (unsigned)config,
                  (unsigned long)a,
                  (unsigned long)b);

    switch (config)
    {
    case CF_NAV_DATA:
        Serial.printf("[NAV] state=%u\n", (unsigned)a);

        if (a)
        {
            navigationUntil = millis() + 9000UL;
            lastActivityMs = millis();
            ambientIsSleepy = false;

            if (oledReady)
                ui.goTo(SCR_NAVIGATION);

            navCheckAt = millis() + 500UL;
        }
        else
        {
            navigationUntil = 0;
            navCheckAt = 0;
            navLastKey = "";

            if (oledReady && ui.current() == SCR_NAVIGATION)
                ui.backToFace();
        }
        break;

    case CF_NAV_ICON:
        if (watch.getNavigation().active)
        {
            navCheckAt = millis() + 500UL;
            navigationUntil = millis() + 9000UL;
            lastActivityMs = millis();
            ambientIsSleepy = false;

            if (oledReady)
                ui.goTo(SCR_NAVIGATION);
        }
        break;

    case CF_FIND:
        buzzer.playFindPhone();
        spriteFace.triggerMoodNow();
        lastActivityMs = millis();
        ambientIsSleepy = false;
        break;

    case CF_PBAT:
        Serial.printf("[PHONE] battery=%u%% charging=%u\n",
                      (unsigned)b,
                      (unsigned)a);
        break;

    case CF_MUSIC:
        Serial.println("[MUSIC] metadata/state updated");
        break;

    case CF_QR:
        if (a == 1)
        {
            qrLinkCount = (int)b;
            ui.setQrCount(qrLinkCount);
            Serial.printf("[QR] %d link(s) received from phone\n", qrLinkCount);
        }
        break;

    default:
        break;
    }
}

void onData(uint8_t *data, int length)
{
    Serial.printf("[DATA] len=%d type=", length);

    if (length > 4)
        Serial.printf("0x%02X", data[4]);
    else
        Serial.print("??");

    Serial.print(" bytes=");

    int n = min(length, 24);

    for (int i = 0; i < n; ++i)
        Serial.printf("%02X ", data[i]);

    if (length > n)
        Serial.print("...");

    Serial.println();
}

void onRawData(uint8_t *data, int length)
{
    Serial.printf("[RAW] len=%d ", length);

    int n = min(length, 40);

    for (int i = 0; i < n; ++i)
        Serial.printf("%02X ", data[i]);

    if (length > n)
        Serial.print("...");

    Serial.println();
}

// ---------- ambient idle behaviour ----------

void updateAmbientFace()
{
    bool wantCalm =
        watch.isSleepActive() ||
        watch.isQuietActive();

    unsigned long idleTimeout =
        menu.idleTimeoutMinutes() * 60UL * 1000UL;

    bool idle =
        (millis() - lastActivityMs) > idleTimeout;

    ambientIsSleepy = wantCalm || idle;
}

void updateRemoteTouch()
{
    RemoteTouch t = watch.getTouch();

    if (t.state)
    {
        lastActivityMs = millis();
        ambientIsSleepy = false;
    }
    else if (prevTouchState && !t.state)
    {
        spriteFace.triggerMoodNow();
        lastActivityMs = millis();
        ambientIsSleepy = false;
    }

    prevTouchState = t.state;
}

// ---------- setup ----------

void setup()
{
    Serial.begin(115200);

    pinMode(BTN_OK_PIN, INPUT);
    pinMode(BTN_PLUS_PIN, INPUT);
    pinMode(BTN_MINUS_PIN, INPUT);

    btnOk.begin(BTN_OK_PIN);
    btnPlus.begin(BTN_PLUS_PIN);
    btnMinus.begin(BTN_MINUS_PIN);

    Wire.begin(OLED_SDA_PIN, OLED_SCL_PIN);
    Wire.setClock(OLED_I2C_CLOCK);

    oledReady =
        display.begin(
            SSD1306_SWITCHCAPVCC,
            OLED_ADDRESS);

    audio.begin();
    buzzer.begin(&audio);
    menu.begin(&display, &watch, &buzzer);

    if (oledReady)
    {
        display.clearDisplay();
        display.setTextColor(SSD1306_WHITE);
        display.setTextSize(1);
        display.setCursor(0, 0);
        display.println(DIRGA_FW_NAME);
        display.println(DIRGA_FW_VERSION);
        display.println("Booting...");
        display.display();

        spriteFace.begin(&display);
    }
    else
    {
        Serial.println("[Dirgamochi] OLED not found at 0x3C - continuing without display");
    }

    watch.setConnectionCallback(onConnectionChange);
    watch.setNotificationCallback(onNotification);
    watch.setRingerCallback(onRinger);
    watch.setConfigurationCallback(onConfig);
    watch.setDataCallback(onData);
    watch.setRawDataCallback(onRawData);

    watch.setName(DIRGA_BLE_NAME);
    watch.setScreen(CF_ESP32_240x240);
    watch.setChunkedTransfer(true);

    watch.begin();

    watch.setNotifyBattery(true);
    watch.setBattery(100, false);

    Serial.print("[Dirgamochi] BLE address: ");
    Serial.println(watch.getAddress());

    if (oledReady)
        ui.begin(&display, &watch, &menu);

    lastActivityMs = millis();
    ambientIsSleepy = false;

    Serial.println("[Dirgamochi] setup complete");
}

// ---------- main loop ----------

void loop()
{
    watch.loop();
    ui.setContactCount(watch.getContactCount());

    ButtonEvent okEv = btnOk.update();
    ButtonEvent plusEv = btnPlus.update();
    ButtonEvent minusEv = btnMinus.update();

    // FIX:
    // Any button activity is user activity and must wake the face.
    bool buttonActivity =
        okEv != BTN_NONE ||
        plusEv != BTN_NONE ||
        minusEv != BTN_NONE;

    if (buttonActivity)
    {
        lastActivityMs = millis();
        ambientIsSleepy = false;
    }

    if ((prevAlarmActive || callRinging) && buttonActivity)
        buzzer.stop();

    bool inMenu =
        oledReady &&
        ui.current() == SCR_MENU;

    bool inQr =
        oledReady &&
        ui.current() == SCR_QR;

    bool inContacts =
        oledReady &&
        ui.current() == SCR_CONTACTS;

    if (inMenu)
    {
        if (menu.isShowingDetail())
        {
            if (okEv == BTN_SHORT_PRESS ||
                minusEv == BTN_SHORT_PRESS)
            {
                menu.closeDetail();
            }
        }
        else
        {
            if (plusEv == BTN_SHORT_PRESS)
                menu.moveNext();

            if (minusEv == BTN_SHORT_PRESS)
                menu.movePrev();

            if (okEv == BTN_SHORT_PRESS)
                menu.activate();

            if (minusEv == BTN_LONG_PRESS)
                ui.backToFace();
        }
    }
    else
    {
        if (okEv == BTN_SHORT_PRESS)
        {
            if (inQr)
                ui.nextQr();
            else if (inContacts)
                ui.nextContact();
            else if (oledReady)
                ui.goTo(SCR_PHONE);
        }
        else if (okEv == BTN_LONG_PRESS)
        {
            watch.findPhone(true);
            findingPhoneUntil =
                millis() + FINDING_PHONE_DISPLAY_MS;

            if (oledReady)
                ui.goTo(SCR_FINDING);

            buzzer.playFindPhone();
        }

        if (plusEv == BTN_SHORT_PRESS)
        {
            if (oledReady)
                ui.nextScreen();
        }
        else if (plusEv == BTN_LONG_PRESS)
        {
            if (oledReady)
                ui.openMenu();
        }

        if (minusEv == BTN_SHORT_PRESS)
        {
            if (oledReady)
                ui.prevScreen();
        }
        else if (minusEv == BTN_LONG_PRESS)
        {
            if (oledReady)
                ui.goTo(SCR_TIME);
        }
    }

    // ---------- Alarm ----------

    bool alarmActive =
        watch.isAnyAlarmActive();

    if (alarmActive && !prevAlarmActive)
    {
        if (menu.buzzerEnabled())
            buzzer.playAlarm();
    }
    else if (!alarmActive && prevAlarmActive)
    {
        buzzer.stop();
    }

    prevAlarmActive = alarmActive;

    // ---------- Navigation sound ----------

    if (navCheckAt && millis() >= navCheckAt)
    {
        navCheckAt = 0;

        Navigation nav =
            watch.getNavigation();

        if (nav.active)
        {
            String key =
                nav.directions +
                "|" +
                String((unsigned long)nav.iconCRC);

            if (key != navLastKey)
            {
                navLastKey = key;

                NavTurn turn =
                    classifyNavTurn(nav);

                Serial.printf(
                    "[NAV] turn=%d "
                    "(0=?,1=L,2=R,3=straight,4=U,5=arrive) "
                    "dir=\"%s\"\n",
                    (int)turn,
                    nav.directions.c_str());

                if (menu.buzzerEnabled())
                    buzzer.playNavTurn(turn);
            }
        }
    }

    // ---------- OLED ----------

    if (oledReady)
    {
        bool overrideActive = false;

        if (watch.getNavigation().active)
        {
            if (ui.current() != SCR_NAVIGATION)
                ui.goTo(SCR_NAVIGATION);

            overrideActive = true;
        }
        else if (
            notificationUntil &&
            millis() < notificationUntil)
        {
            if (ui.current() != SCR_NOTIFICATIONS)
                ui.goTo(SCR_NOTIFICATIONS);

            overrideActive = true;
        }
        else if (
            findingPhoneUntil &&
            millis() < findingPhoneUntil)
        {
            if (ui.current() != SCR_FINDING)
                ui.goTo(SCR_FINDING);

            overrideActive = true;
        }
        else if (
            connectCelebUntil &&
            millis() < connectCelebUntil)
        {
            if (ui.current() != SCR_PHONE)
                ui.goTo(SCR_PHONE);

            overrideActive = true;
        }

        if (!overrideActive)
        {
            if (
                ui.current() == SCR_NOTIFICATIONS &&
                notificationUntil &&
                millis() >= notificationUntil)
            {
                notificationUntil = 0;
                ui.backToFace();
            }
            else if (
                ui.current() == SCR_FINDING &&
                findingPhoneUntil &&
                millis() >= findingPhoneUntil)
            {
                findingPhoneUntil = 0;
                ui.backToFace();
            }
            else if (
                ui.current() == SCR_PHONE &&
                connectCelebUntil &&
                millis() >= connectCelebUntil)
            {
                connectCelebUntil = 0;
                ui.backToFace();
            }
        }

        // ---------- FACE ----------

        if (ui.current() == SCR_FACE)
        {
            updateRemoteTouch();

            /*
             * CRITICAL FIX:
             *
             * The old code only called updateAmbientFace() while
             * ambientIsSleepy was false.
             *
             * That caused:
             *
             * FACE -> idle -> CLOCK -> ambientIsSleepy=true
             *
             * and then updateAmbientFace() was never called again.
             *
             * Now ambient state is recalculated whenever the sprite
             * is resting, allowing CLOCK -> FACE after activity.
             */
            if (!spriteActive ||
                spriteFace.isResting())
            {
                updateAmbientFace();
            }

            if (ambientIsSleepy)
            {
                ui.drawTimeNow();
            }
            else
            {
                if (!spriteActive)
                {
                    spriteFace.resetIdleTimers();
                    spriteActive = true;
                }

                spriteFace.setShowInterval(
                    menu.moodShowMinMs(),
                    menu.moodShowMaxMs());

                spriteFace.update(bleConnected);

                uint8_t ev =
                    spriteFace.takeEvents();

                if (ev & SPRITE_EV_MOOD)
                {
                    int set =
                        spriteFace.currentAnimSet();

                    if (
                        set >= 0 &&
                        set < VOICE_MOOD_COUNT &&
                        moodVoiceData[set])
                    {
                        buzzer.playVoice(
                            moodVoiceData[set],
                            moodVoiceLength[set],
                            VOICE_BLOCK_ALIGN,
                            1);
                    }
                    else
                    {
                        buzzer.playBoink();
                    }
                }
                else if (ev & SPRITE_EV_GLANCE)
                {
                    buzzer.playBlup();
                }
                else if (ev & SPRITE_EV_BLINK)
                {
                    if (blinkVoiceData_)
                    {
                        buzzer.playVoice(
                            blinkVoiceData_,
                            blinkVoiceLength,
                            VOICE_BLOCK_ALIGN,
                            0);
                    }
                    else
                    {
                        buzzer.playBlink();
                    }
                }
            }
        }
        else
        {
            spriteActive = false;
            ui.update();
        }
    }

    audio.loop();
    buzzer.loop();
}
