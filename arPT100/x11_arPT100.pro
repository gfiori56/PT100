SHARED_FOLDER = "../../000_common"
TARGET    = arPT100
include($$SHARED_FOLDER/common.pri)

SOURCES += main.cpp mainwindow.cpp
HEADERS += mainwindow.h myglobal.h
FORMS += mainwindow.ui

QT+=network

