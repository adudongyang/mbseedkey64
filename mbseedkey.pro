QT += quick
CONFIG += c++17

GIT_LATEST_TAG = 1.0.0.4
SW_VERSION = \\\"1.0.0.4\\\"
DEFINES += SW_VERSION=$$(SW_VERSION)

VERSION_MAJOR = 1
VERSION_MINOR = 0
VERSION_PATCH = 0

RC_ICONS = "Resources/icon.ico"
RC_LANG = "en"

VERSION = 1.0.0.4
VERSION_PE_HEADER = 1.0

message("VER: $$VERSION")

DEFINES += QT_NO_DEBUG_OUTPUT

TARGET = MBSeedKey
DESTDIR = build/release

SOURCES += \
        ecuseedkeydll.cpp \
        exutils.cpp \
        main.cpp

HEADERS += \
    ecuseedkeydll.h \
    exutils.h

RESOURCES += qml.qrc

TRANSLATIONS += \
    mbseedkey_en.ts

win32 {
    CONFIG += embed_manifest_exe
    win32-msvc*: QMAKE_LFLAGS += /LARGEADDRESSAWARE
    win32-g++: QMAKE_LFLAGS += -Wl,--large-address-aware
}
