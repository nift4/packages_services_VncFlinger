//
// vncflinger - Copyright (C) 2021 Stefanie Kondik
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program.  If not, see <http://www.gnu.org/licenses/>.
//

#define LOG_TAG "VNCFlinger:InputDevice"
#include <utils/Log.h>

#include <future>

#include "InputDevice.h"

#include <fcntl.h>
#include <stdio.h>

#include <sys/ioctl.h>
#include <sys/system_properties.h>

#include <linux/input.h>
#include <linux/uinput.h>

#define XK_MISCELLANY
#define XK_LATIN1
#define XK_CURRENCY
#include <rfb/keysymdef.h>

using namespace android;

static bool touch = false; // true = touchscreen, false = mouse
static bool useRelativeInput = false; // false = touchscreen, true = mouse
// Relative Input tracking is unreliable BECAUSE Android adds fling to relative mouse input
// I solved this by patching absolute mouse support into frameworks/native, but if you really want to use this
// Do mind that viewer cursor and server cursor need to be same pos at the beginning
// Or it will cause confusion

static const struct UInputOptions {
    int cmd;
    int bit;
} kOptions[] = {
    {UI_SET_EVBIT, EV_KEY},
    {UI_SET_EVBIT, EV_REP},
    {UI_SET_EVBIT, EV_REL},
    {UI_SET_RELBIT, REL_X},
    {UI_SET_RELBIT, REL_Y},
    {UI_SET_RELBIT, REL_WHEEL},
    {UI_SET_EVBIT, EV_ABS},
    {UI_SET_ABSBIT, ABS_X},
    {UI_SET_ABSBIT, ABS_Y},
    {UI_SET_EVBIT, EV_SYN},
    {UI_SET_PROPBIT, INPUT_PROP_DIRECT},
};
static const struct UInputOptions mOptions[] = {
    {UI_SET_EVBIT, EV_KEY},
    {UI_SET_EVBIT, EV_REP},
    {UI_SET_EVBIT, EV_REL},
    {UI_SET_RELBIT, REL_X},
    {UI_SET_RELBIT, REL_Y},
    {UI_SET_RELBIT, REL_WHEEL},
    {UI_SET_EVBIT, EV_ABS},
    {UI_SET_ABSBIT, ABS_X},
    {UI_SET_ABSBIT, ABS_Y},
    {UI_SET_EVBIT, EV_SYN},
};


// The keysymToAscii table transforms a couple of awkward keysyms into their
// ASCII equivalents.
struct keysymToAscii_t {
    uint32_t keysym;
    uint32_t ascii;
};

keysymToAscii_t keysymToAscii[] = {
    {XK_KP_Space, ' '},
    {XK_KP_Equal, '='},
};

uint32_t latin1DeadChars[] = {
    XK_grave,
    XK_acute,
    XK_asciicircum,
    XK_diaeresis,
    XK_degree,
    XK_cedilla,
    XK_asciitilde
};

struct deadCharsToAltChar_t {
    uint32_t deadChar;
    uint32_t altChar;
};

deadCharsToAltChar_t deadCharsToAltChar[] = {
    {XK_grave, KEY_GRAVE},
    {XK_acute, KEY_E},
    {XK_asciicircum, KEY_I},
    {XK_diaeresis, KEY_U},
    {XK_asciitilde, KEY_N}
};

struct latin1ToDeadChars_t {
    uint32_t latin1Char;
    uint32_t deadChar;
    uint32_t baseChar;
};

latin1ToDeadChars_t latin1ToDeadChars[] = {
    {XK_Agrave, XK_grave, XK_A},
    {XK_Egrave, XK_grave, XK_E},
    {XK_Igrave, XK_grave, XK_I},
    {XK_Ograve, XK_grave, XK_O},
    {XK_Ugrave, XK_grave, XK_U},
    {XK_agrave, XK_grave, XK_a},
    {XK_egrave, XK_grave, XK_e},
    {XK_igrave, XK_grave, XK_i},
    {XK_ograve, XK_grave, XK_o},
    {XK_ugrave, XK_grave, XK_u},

    {XK_Aacute, XK_acute, XK_A},
    {XK_Eacute, XK_acute, XK_E},
    {XK_Iacute, XK_acute, XK_I},
    {XK_Oacute, XK_acute, XK_O},
    {XK_Uacute, XK_acute, XK_U},
    {XK_Yacute, XK_acute, XK_Y},
    {XK_aacute, XK_acute, XK_a},
    {XK_eacute, XK_acute, XK_e},
    {XK_iacute, XK_acute, XK_i},
    {XK_oacute, XK_acute, XK_o},
    {XK_uacute, XK_acute, XK_u},
    {XK_yacute, XK_acute, XK_y},

    {XK_Acircumflex, XK_asciicircum, XK_A},
    {XK_Ecircumflex, XK_asciicircum, XK_E},
    {XK_Icircumflex, XK_asciicircum, XK_I},
    {XK_Ocircumflex, XK_asciicircum, XK_O},
    {XK_Ucircumflex, XK_asciicircum, XK_U},
    {XK_acircumflex, XK_asciicircum, XK_a},
    {XK_ecircumflex, XK_asciicircum, XK_e},
    {XK_icircumflex, XK_asciicircum, XK_i},
    {XK_ocircumflex, XK_asciicircum, XK_o},
    {XK_ucircumflex, XK_asciicircum, XK_u},

    {XK_Adiaeresis, XK_diaeresis, XK_A},
    {XK_Ediaeresis, XK_diaeresis, XK_E},
    {XK_Idiaeresis, XK_diaeresis, XK_I},
    {XK_Odiaeresis, XK_diaeresis, XK_O},
    {XK_Udiaeresis, XK_diaeresis, XK_U},
    {XK_adiaeresis, XK_diaeresis, XK_a},
    {XK_ediaeresis, XK_diaeresis, XK_e},
    {XK_idiaeresis, XK_diaeresis, XK_i},
    {XK_odiaeresis, XK_diaeresis, XK_o},
    {XK_udiaeresis, XK_diaeresis, XK_u},
    {XK_ydiaeresis, XK_diaeresis, XK_y},

    {XK_Atilde, XK_asciitilde, XK_A},
    {XK_Ntilde, XK_asciitilde, XK_N},
    {XK_Otilde, XK_asciitilde, XK_O},
    {XK_atilde, XK_asciitilde, XK_a},
    {XK_ntilde, XK_asciitilde, XK_n},
    {XK_otilde, XK_asciitilde, XK_o},
};

struct spicialKeysymToDirectKey_t {
    uint32_t keysym;
    uint32_t directKey;
};

static spicialKeysymToDirectKey_t spicialKeysymToDirectKey[] = {
    {XK_BackSpace, KEY_BACKSPACE},
    {XK_Tab, KEY_TAB},
    {XK_Return, KEY_ENTER},
    {XK_Pause, KEY_PAUSE},
    {XK_Escape, KEY_ESC},
    {XK_Delete, KEY_DELETE},

    // Cursor control & motion
    {XK_Home, KEY_HOME},
    {XK_Left, KEY_LEFT},
    {XK_Up, KEY_UP},
    {XK_Right, KEY_RIGHT},
    {XK_Down, KEY_DOWN},
    {XK_Page_Up, KEY_PAGEUP},
    {XK_Page_Down, KEY_PAGEDOWN},
    {XK_End, KEY_END},

    // Misc functions
    {XK_Insert, KEY_INSERT},

    // Auxiliary Functions - must come before XK_KP_F1, etc
    {XK_F1, KEY_F1},
    {XK_F2, KEY_F2},
    {XK_F3, KEY_F3},
    {XK_F4, KEY_F4},
    {XK_F5, KEY_F5},
    {XK_F6, KEY_F6},
    {XK_F7, KEY_F7},
    {XK_F8, KEY_F8},
    {XK_F9, KEY_F9},
    {XK_F10, KEY_F10},
    {XK_F11, KEY_F11},
    {XK_F12, KEY_F12},
    {XK_F13, KEY_F13},
    {XK_F14, KEY_F14},
    {XK_F15, KEY_F15},
    {XK_F16, KEY_F16},
    {XK_F17, KEY_F17},
    {XK_F18, KEY_F18},
    {XK_F19, KEY_F19},
    {XK_F20, KEY_F20},
    {XK_F21, KEY_F21},
    {XK_F22, KEY_F22},
    {XK_F23, KEY_F23},
    {XK_F24, KEY_F24},

    // Keypad Functions, keypad numbers
    {XK_KP_Tab, KEY_TAB},
    {XK_KP_Enter, KEY_KPENTER},
    {XK_KP_F1, KEY_F1},
    {XK_KP_F2, KEY_F2},
    {XK_KP_F3, KEY_F3},
    {XK_KP_F4, KEY_F4},
    {XK_KP_Home, KEY_HOME},
    {XK_KP_Left, KEY_LEFT},
    {XK_KP_Up, KEY_UP},
    {XK_KP_Right, KEY_RIGHT},
    {XK_KP_Down, KEY_DOWN},
    {XK_KP_End, KEY_END},
    {XK_KP_Page_Up, KEY_PAGEUP},
    {XK_KP_Page_Down, KEY_NEXT},
    {XK_KP_Insert, KEY_INSERT},
    {XK_KP_Delete, KEY_DELETE},
    {XK_KP_Multiply, KEY_KPASTERISK},
    {XK_KP_Add, KEY_KPPLUS},
    {XK_KP_Separator, KEY_KPCOMMA},
    {XK_KP_Subtract, KEY_KPMINUS},
    {XK_KP_Decimal, KEY_KPDOT},
    {XK_KP_Divide, KEY_KPSLASH},

    {XK_KP_0, KEY_KP0},
    {XK_KP_1, KEY_KP1},
    {XK_KP_2, KEY_KP2},
    {XK_KP_3, KEY_KP3},
    {XK_KP_4, KEY_KP4},
    {XK_KP_5, KEY_KP5},
    {XK_KP_6, KEY_KP6},
    {XK_KP_7, KEY_KP7},
    {XK_KP_8, KEY_KP8},
    {XK_KP_9, KEY_KP9},

    // Modifiers
    {XK_Shift_L, KEY_LEFTSHIFT},
    {XK_Shift_R, KEY_RIGHTSHIFT},
    {XK_Control_L, KEY_LEFTCTRL},
    {XK_Control_R, KEY_RIGHTCTRL},
    {XK_Alt_L, KEY_LEFTALT},
    {XK_Alt_R, KEY_RIGHTALT},
    {XK_Meta_L, KEY_LEFTALT},
    {XK_Meta_R, KEY_RIGHTALT},

    // Left & Right Windows keys & Windows Menu Key
    {XK_Super_L, 0xDB},
    {XK_Super_R, 0xDC},
    {XK_Menu, 0xDD},

    // Japanese stuff - almost certainly wrong...
    {XK_Kanji, 0x94},
    {XK_Kana_Shift, 0x70},
};

struct symbolKeysymToDirectKey_t {
    uint32_t keysym;
    uint32_t directKey;
    bool needShift;
    bool needAlt;
};

static symbolKeysymToDirectKey_t symbolKeysymToDirectKey[] = {
    {XK_space, KEY_SPACE, false, false},
    {XK_exclam, KEY_1, true, false},
    {XK_quotedbl, KEY_APOSTROPHE, true, false},
    {XK_numbersign, KEY_3, true, false},
    {XK_dollar, KEY_4, true, false},
    {XK_percent, KEY_5, true, false},
    {XK_ampersand, KEY_7, true, false},
    {XK_apostrophe, KEY_APOSTROPHE, false, false},
    {XK_parenleft, KEY_9, true, false},
    {XK_parenright, KEY_0, true, false},
    {XK_asterisk, KEY_8, true, false},
    {XK_plus, KEY_EQUAL, true, false},
    {XK_comma, KEY_COMMA, false, false},
    {XK_minus, KEY_MINUS, false, false},
    {XK_period, KEY_DOT, false, false},
    {XK_slash, KEY_SLASH, false, false},

    {XK_colon, KEY_SEMICOLON, true, false},
    {XK_semicolon, KEY_SEMICOLON, false, false},
    {XK_less, KEY_COMMA, true, false},
    {XK_equal, KEY_EQUAL, false, false},
    {XK_greater, KEY_DOT, true, false},
    {XK_question, KEY_SLASH, true, false},
    {XK_at, KEY_2, true, false},

    {XK_bracketleft, KEY_LEFTBRACE, false, false},
    {XK_backslash, KEY_BACKSLASH, false, false},
    {XK_bracketright, KEY_RIGHTBRACE, false, false},
    {XK_asciicircum, KEY_6, true, false},
    {XK_underscore, KEY_MINUS, true, false},
    {XK_grave, KEY_GRAVE, false, false},

    {XK_braceleft, KEY_LEFTBRACE, true, false},
    {XK_bar, KEY_BACKSLASH, true, false},
    {XK_braceright, KEY_RIGHTBRACE, true, false},
    {XK_asciitilde, KEY_GRAVE, true, false},

    {XK_Aring, KEY_A, true, true},
    {XK_aring, KEY_A, false, true},

    {XK_Ccedilla, KEY_C, true, true},
    {XK_ccedilla, KEY_C, false, true},

    {XK_EuroSign, KEY_2, true, true},
    {XK_masculine, KEY_0, false, true},

    {163, KEY_3, false, true},
    {223, KEY_S, false, true},
    {167, KEY_6, false, true}};

// q,w,e,r,t,y,u,i,o,p,a,s,d,f,g,h,j,k,l,z,x,c,v,b,n,m
static const int qwerty[] = {30, 48, 46, 32, 18, 33, 34, 35, 23, 36, 37, 38, 50,
                             49, 24, 25, 16, 19, 31, 20, 22, 47, 17, 45, 21, 44};

status_t InputDevice::start_async(uint32_t width, uint32_t height, bool istouch, bool relative) {
    // don't block the caller since this can take a few seconds
    std::async(&InputDevice::start, this, width, height, istouch, relative);

    return NO_ERROR;
}

status_t InputDevice::start(uint32_t width, uint32_t height, bool istouch, bool relative) {
    Mutex::Autolock _l(mLock);

    mLeftClicked = mMiddleClicked = mRightClicked = false;
    mLastX = mLastY = 0;
	touch = istouch; useRelativeInput = relative;

    struct input_id id = {
        BUS_VIRTUAL, /* Bus type */
        1,           /* Vendor */
        1,           /* Product */
        4,           /* Version */
    };

    if (mFD >= 0) {
        ALOGE("Input device already open!");
        return NO_INIT;
    }

    mFD = open(UINPUT_DEVICE, O_WRONLY | O_NONBLOCK);
    if (mFD < 0) {
        ALOGE("Failed to open %s: err=%d", UINPUT_DEVICE, mFD);
        return NO_INIT;
    }

    const auto options = touch ? kOptions : mOptions;

    unsigned int idx = 0;
    for (idx = 0; idx < (touch ? sizeof(kOptions) : sizeof(mOptions)) / (touch ? sizeof(kOptions[0]) : sizeof(mOptions[0])); idx++) {
        if (ioctl(mFD, options[idx].cmd, options[idx].bit) < 0) {
            ALOGE("uinput ioctl failed: %d %d", options[idx].cmd, options[idx].bit);
            goto err_ioctl;
        }
    }

    for (idx = 0; idx < KEY_MAX; idx++) {
        if (!touch && idx == BTN_TOUCH)
            continue;
        if (touch && idx == BTN_MOUSE)
            continue;
        if (ioctl(mFD, UI_SET_KEYBIT, idx) < 0) {
            ALOGE("UI_SET_KEYBIT failed");
            goto err_ioctl;
        }
    }

    memset(&mUserDev, 0, sizeof(mUserDev));
    strncpy(mUserDev.name, "VNC-RemoteInput", UINPUT_MAX_NAME_SIZE);

    mUserDev.id = id;

    if (!useRelativeInput) {
        mUserDev.absmin[ABS_X] = 0;
        mUserDev.absmax[ABS_X] = width;
        mUserDev.absmin[ABS_Y] = 0;
        mUserDev.absmax[ABS_Y] = height;
    }

    if (write(mFD, &mUserDev, sizeof(mUserDev)) != sizeof(mUserDev)) {
        ALOGE("Failed to configure uinput device");
        goto err_ioctl;
    }

    if (ioctl(mFD, UI_DEV_CREATE) == -1) {
        ALOGE("UI_DEV_CREATE failed");
        goto err_ioctl;
    }

    mOpened = true;

    ALOGD("Virtual input device created successfully (%dx%d)", width, height);
    return NO_ERROR;

err_ioctl:
    int prev_errno = errno;
    ::close(mFD);
    errno = prev_errno;
    mFD = -1;
    return NO_INIT;
}

status_t InputDevice::reconfigure(uint32_t width, uint32_t height, bool istouch, bool relative) {
    stop();
    return start_async(width, height, istouch, relative);
}

status_t InputDevice::stop() {
    Mutex::Autolock _l(mLock);

    mOpened = false;

    if (mFD < 0) {
        return OK;
    }

    ioctl(mFD, UI_DEV_DESTROY);
    close(mFD);
    mFD = -1;

    return OK;
}

status_t InputDevice::inject(uint16_t type, uint16_t code, int32_t value) {
    struct input_event event;
    memset(&event, 0, sizeof(event));
    gettimeofday(&event.time, 0); /* This should not be able to fail ever.. */
    event.type = type;
    event.code = code;
    event.value = value;
    if (write(mFD, &event, sizeof(event)) != sizeof(event)) return BAD_VALUE;
    return OK;
}

status_t InputDevice::injectSyn(uint16_t type, uint16_t code, int32_t value) {
    if (inject(type, code, value) != OK) {
        return BAD_VALUE;
    }
    return inject(EV_SYN, SYN_REPORT, 0);
}

status_t InputDevice::movePointer(int32_t x, int32_t y) {
    ALOGV("movePointer: x=%d y=%d", x, y);
    mLastX = x;
    mLastY = y;
    if (inject(EV_REL, REL_X, x) != OK) {
        return BAD_VALUE;
    }
    return injectSyn(EV_REL, REL_Y, y);
}

status_t InputDevice::setPointer(int32_t x, int32_t y) {
    ALOGV("setPointer: x=%d y=%d", x, y);
    if (useRelativeInput) {
        if (inject(EV_REL, REL_X, x - mLastX) != OK) {
            return BAD_VALUE;
        }
        if (inject(EV_REL, REL_Y, y - mLastY) != OK) {
            return BAD_VALUE;
        }
        mLastX = x;
        mLastY = y;
        return OK;
    }
    mLastX = x;
    mLastY = y;
    if (inject(EV_ABS, ABS_X, x) != OK) {
        return BAD_VALUE;
    }
    return injectSyn(EV_ABS, ABS_Y, y);
}

status_t InputDevice::press(uint16_t code) {
    return inject(EV_KEY, code, 1);
}

status_t InputDevice::release(uint16_t code) {
    return inject(EV_KEY, code, 0);
}

status_t InputDevice::click(uint16_t code) {
    if (press(code) != OK) {
        return BAD_VALUE;
    }
    return release(code);
}

status_t InputDevice::doKeyboardEvent(uint16_t code, bool down) {
    inject(EV_SYN, SYN_REPORT, 0);
    if (down) {
        if (press(code) != OK) {
            ALOGE("Failed to inject key %d press event:%d (%s)", code, errno, strerror(errno));
            return errno;
        }
    } else {
        if (release(code) != OK) {
            ALOGE("Failed to inject key %d release event:%d (%s)", code, errno, strerror(errno));
            return errno;
        }
    }
    inject(EV_SYN, SYN_REPORT, 0);
    return OK;
}

status_t InputDevice::doBasicKeyEvent(uint16_t code, bool down) {
    bool needShift = false;
    bool needAlt = false;
    int scanCode = 0;

    // QWERTY and Numbers
    if ('a' <= code && code <= 'z') scanCode = qwerty[code - 'a'];
    if ('A' <= code && code <= 'Z') {
        needShift = true;
        scanCode = qwerty[code - 'A'];
    }
    if ('1' <= code && code <= '9') scanCode = (code - '1' + 2);
    if (code == '0') scanCode = KEY_0;

    // Symbols
    for (unsigned int i = 0; i < sizeof(symbolKeysymToDirectKey) / sizeof(symbolKeysymToDirectKey_t); i++) {
        if (symbolKeysymToDirectKey[i].keysym == code) {
            scanCode = symbolKeysymToDirectKey[i].directKey;
            needShift = symbolKeysymToDirectKey[i].needShift;
            needAlt = symbolKeysymToDirectKey[i].needAlt;
            break;
        }
    }

    if (scanCode == 0) {
        ALOGE("Unknown keysym %d", code);
        return BAD_VALUE;
    }
    if (down) {
        doKeyboardEvent(KEY_LEFTALT, needAlt);
        doKeyboardEvent(KEY_LEFTSHIFT, needShift);
    }
    doKeyboardEvent(scanCode, down);
    if (down) {
        doKeyboardEvent(KEY_LEFTSHIFT, false);
        doKeyboardEvent(KEY_LEFTALT, false);
    }
    return OK;
}

void InputDevice::keyEvent(bool down, uint32_t keysym) {
    int code;
    int sh = 0;
    int alt = 0;

    Mutex::Autolock _l(mLock);
    if (!mOpened) return;

    // Fix unknown keys
    for (unsigned int i = 0; i < sizeof(keysymToAscii) / sizeof(keysymToAscii_t); i++) {
        if (keysymToAscii[i].keysym == keysym) {
            keysym = keysymToAscii[i].ascii;
            break;
        }
    }

    // Latin Keys
    for (unsigned int j = 0; j < sizeof(latin1ToDeadChars) / sizeof(latin1ToDeadChars_t); j++) {
        if (keysym == latin1ToDeadChars[j].latin1Char) {
            for (unsigned int i = 0; i < sizeof(deadCharsToAltChar) / sizeof(deadCharsToAltChar_t); i++) {
                if (latin1ToDeadChars[j].deadChar == deadCharsToAltChar[i].deadChar) {
                    // Alt + AltChar, BaseChar
                    if (down) {
                        doKeyboardEvent(KEY_LEFTALT, true);
                        doKeyboardEvent(deadCharsToAltChar[i].altChar, true);
                        doKeyboardEvent(KEY_LEFTALT, false);
                        doKeyboardEvent(deadCharsToAltChar[i].altChar, false);
                    }
                    doBasicKeyEvent(latin1ToDeadChars[j].baseChar, down);
                    return;
                }
            }
        }
    }

    // Special Keys
    for (unsigned int i = 0; i < sizeof(spicialKeysymToDirectKey) / sizeof(spicialKeysymToDirectKey_t); i++) {
        if (spicialKeysymToDirectKey[i].keysym == keysym) {
            doKeyboardEvent(spicialKeysymToDirectKey[i].directKey, down);
            return;
        }
    }

    // Basic Keys
    doBasicKeyEvent(keysym, down);
}

void InputDevice::pointerEvent(int buttonMask, int x, int y) {
    Mutex::Autolock _l(mLock);
    if (!mOpened) return;

    ALOGV("pointerEvent: buttonMask=%x x=%d y=%d", buttonMask, x, y);

    int32_t diffX = (x - mLastX);
    int32_t diffY = (y - mLastY);
    mLastX = x;
    mLastY = y;
    if (!touch) {
        if (!useRelativeInput) {
            inject(EV_ABS, ABS_X, x);
            inject(EV_ABS, ABS_Y, y);
        } else {
            inject(EV_REL, REL_X, diffX);
            inject(EV_REL, REL_Y, diffY);
        }
        inject(EV_SYN, SYN_REPORT, 0);
    }

    if ((buttonMask & 1) && !mLeftClicked) {  // left btn clicked
        mLeftClicked = true;
        if (!useRelativeInput) {
            inject(EV_ABS, ABS_X, x);
            inject(EV_ABS, ABS_Y, y);
        }
        if (touch) {
            inject(EV_KEY, BTN_TOUCH, 1);
        } else {
            inject(EV_KEY, BTN_LEFT, 1);
        }
        inject(EV_SYN, SYN_REPORT, 0);
    } else if (!(buttonMask & 1) && mLeftClicked) {  // left btn released
        mLeftClicked = false;
        if (!useRelativeInput) {
            inject(EV_ABS, ABS_X, x);
            inject(EV_ABS, ABS_Y, y);
        }
        if (touch) {
            inject(EV_KEY, BTN_TOUCH, 0);
        } else {
            inject(EV_KEY, BTN_LEFT, 0);
        }
        inject(EV_SYN, SYN_REPORT, 0);
    } else if (mLeftClicked && !useRelativeInput) { // dragclick
        inject(EV_ABS, ABS_X, x);
        inject(EV_ABS, ABS_Y, y);
        inject(EV_SYN, SYN_REPORT, 0);
    }

    if ((buttonMask & 4) && !mRightClicked)  // right btn clicked
    {
        mRightClicked = true;
        if (touch) {
            press(158);  // back key
        } else {
            inject(EV_KEY, BTN_RIGHT, 1);
        }
        inject(EV_SYN, SYN_REPORT, 0);
    } else if (!(buttonMask & 4) && mRightClicked)  // right button released
    {
        mRightClicked = false;
        if (touch) {
            release(158);
        } else {
            inject(EV_KEY, BTN_RIGHT, 0);
        }
        inject(EV_SYN, SYN_REPORT, 0);
    } else if (mRightClicked && !useRelativeInput) { // dragclick
        inject(EV_ABS, ABS_X, x);
        inject(EV_ABS, ABS_Y, y);
        inject(EV_SYN, SYN_REPORT, 0);
    }

    if ((buttonMask & 2) && !mMiddleClicked) {  // mid btn clicked
        mMiddleClicked = true;
        if (touch) {
            press(KEY_END);
        } else {
            inject(EV_KEY, BTN_MIDDLE, 1);
        }
        inject(EV_SYN, SYN_REPORT, 0);
    } else if (!(buttonMask & 2) && mMiddleClicked)  // mid btn released
    {
        mMiddleClicked = false;
        if (touch) {
            release(KEY_END);
        } else {
            inject(EV_KEY, BTN_MIDDLE, 0);
        }
        inject(EV_SYN, SYN_REPORT, 0);
    } else if (mMiddleClicked && !useRelativeInput) { // dragclick
        inject(EV_ABS, ABS_X, x);
        inject(EV_ABS, ABS_Y, y);
        inject(EV_SYN, SYN_REPORT, 0);
    }

    if (buttonMask & 8) {
        inject(EV_REL, REL_WHEEL, 1);
        inject(EV_SYN, SYN_REPORT, 0);
    }

    if (buttonMask & 0x10) {
        inject(EV_REL, REL_WHEEL, -1);
        inject(EV_SYN, SYN_REPORT, 0);
    }
}
