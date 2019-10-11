TOP = ..
include $(TOP)/configure/CONFIG

SEABREEZE_LINUX_LIBS = libseabreeze.so


# install libs
# TODO: shouldn't really do this, should get the aravisGgigeApp/src/Makefile to point to the vendor directory
LIB_INSTALLS_Linux  += $(addprefix ../SeaBreeze-3.0.11/lib/, $(SEABREEZE_LINUX_LIBS))
SEABREEZELIB = $(TOP)/vendor/SeaBreeze-3.0.11/lib/$(SEABREEZE_LINUX_LIBS)

ifeq (windows-x64, $(findstring windows-x64, $(T_A)))
#LIB_INSTALLS += ../SeaBreeze-3.0.11/os-support/windows/VisualStudio2017/x64/Release/seabreeze.lib
#BIN_INSTALLS += ../SeaBreeze-3.0.11/os-support/windows/VisualStudio2017/x64/Release/seabreeze.dll
LIB_INSTALLS += ../Library/seabreeze.lib
BIN_INSTALLS += ../Library/seabreeze.dll

endif

# depend on Makefile existing
install: $(SEABREEZELIB)

include $(TOP)/configure/RULES

$(TOP)/vendor/SeaBreeze-%/lib/libseabreeze.so:
	(mkdir -p $(TOP)/vendor/SeaBreeze-$(*F); cd $(TOP)/vendor/SeaBreeze-$(*F); $(ENVEXPORTS) make && chmod -R g+w .)
