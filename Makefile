#
# This is a project Makefile. It is assumed the directory this Makefile resides in is a
# project subdirectory.
#

EXTRA_COMPONENT_DIRS := /home/jeroen/esp8266/esp32/8bkc-sdk/8bkc-components/

PROJECT_NAME := gnuboy

APPFS_EXTRA_FILES := rom/*

include $(IDF_PATH)/make/project.mk

