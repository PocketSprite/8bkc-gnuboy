#
# This is a project Makefile. It is assumed the directory this Makefile resides in is a
# project subdirectory.
#

EXTRA_COMPONENT_DIRS := $(POCKETSPRITE_PATH)/8bkc-components/
IDF_PATH := $(POCKETSPRITE_PATH)/esp-idf

PROJECT_NAME := gnuboy

include $(IDF_PATH)/make/project.mk

