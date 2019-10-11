TOP=../..
include $(TOP)/configure/CONFIG
#----------------------------------------
#  ADD MACRO DEFINITIONS AFTER THIS LINE

ifeq (windows-x64, $(findstring windows-x64, $(T_A)))
USR_CPPFLAGS += -D__X64
endif

USR_CPPFLAGS += -I$(TOP)/vendor/SeaBreeze-3.0.11/include

LIBRARY_IOC = ADSeaBreezeSpec
LIB_SRCS += SeaBreezeSpec.cpp

ifeq (windows-x64, $(findstring windows-x64, $(T_A)))
LIB_LIBS += SeaBreeze
endif

ifeq (linux-x86_64, $(findstring linux-x86_64, $(T_A)))
USR_INCLUDES += -I$(TOP)/vendor/SeaBreeze-3.0.11/include
LIB_LIBS += seabreeze
# Arun: January 18,2019
# Added the following per Mark's suggestion. Is the syntax correct? 
#USR_CXXFLAGS += -std=c++11

endif

DBD += SeaBreezeSupport.dbd

include $(ADCORE)/ADApp/commonLibraryMakefile

#=============================

include $(TOP)/configure/RULES
#----------------------------------------
#  ADD RULES AFTER THIS LINE

