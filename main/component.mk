#
# Main Makefile. This is basically the same as a component makefile.
#
# This Makefile should, at the very least, just include $(SDK_PATH)/make/component_common.mk. By default, 
# this will take the sources in the src/ directory, compile them and link them into 
# lib(subdirectory_name).a in the build directory. This behaviour is entirely configurable,
# please read the ESP-IDF documents if you need to do this.
#

COMPILEDATE:=\"$(shell date "+%d %b %Y")\"
GITREV:=\"$(shell git rev-parse HEAD | cut -b 1-10)\"

CFLAGS += -DIS_LITTLE_ENDIAN -DGNUBOYDBG -O3 -Wno-error=strict-aliasing 
CFLAGS += -DCOMPILEDATE="$(COMPILEDATE)" -DGITREV="$(GITREV)"


include $(IDF_PATH)/make/component_common.mk
