######################################################################
# Automatically generated by qmake (1.02a) Tue Jul 16 20:40:47 2002
######################################################################

include ( ../../settings.pro )

TEMPLATE = lib
TARGET = myth-$$LIBVERSION
CONFIG += thread dll
target.path = $${PREFIX}/lib
INSTALLS = target

VERSION = 0.8.0

# Input
HEADERS += dialogbox.h guidegrid.h infodialog.h infostructs.h lcddevice.h 
HEADERS += mythcontext.h mythwidgets.h oldsettings.h programinfo.h 
HEADERS += remotefile.h settings.h themedmenu.h util.h remoteencoder.h
HEADERS += mythwizard.h

SOURCES += dialogbox.cpp guidegrid.cpp infodialog.cpp infostructs.cpp 
SOURCES += lcddevice.cpp mythcontext.cpp mythwidgets.cpp oldsettings.cpp 
SOURCES += programinfo.cpp remotefile.cpp settings.cpp themedmenu.cpp
SOURCES += util.cpp remoteencoder.cpp mythwizard.cpp

inc.path = $${PREFIX}/include/mythtv/
inc.files  = dialogbox.h lcddevice.h programinfo.h themedmenu.h mythcontext.h 
inc.files += mythwidgets.h remotefile.h util.h

INSTALLS += inc
