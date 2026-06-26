#---------------------------------------------------------------------------------
.SUFFIXES:
#---------------------------------------------------------------------------------

ifeq ($(strip $(DEVKITARM)),)
$(error "Please set DEVKITARM in your environment. export DEVKITARM=<path to>devkitARM")
endif

TOPDIR ?= $(CURDIR)
include $(DEVKITARM)/3ds_rules

#---------------------------------------------------------------------------------
# TARGET      : name of the output (savesync.3dsx)
# BUILD       : directory for object/intermediate files
# SOURCES     : directories containing source code
# DATA        : directories containing binary data files
# INCLUDES    : directories containing header files
# APP_*       : SMDH metadata (title/description/author shown in Homebrew Launcher)
#---------------------------------------------------------------------------------
TARGET          :=  savesync
BUILD           :=  build
SOURCES         :=  source libs/tomlc99
DATA            :=  data
INCLUDES        :=  include libs/tomlc99

APP_TITLE       :=  suzinho's OFF save sync
APP_DESCRIPTION :=  on a mission to purify the world
APP_AUTHOR      :=  xsu

#---------------------------------------------------------------------------------
# options for code generation
#---------------------------------------------------------------------------------
ARCH    :=  -march=armv6k -mtune=mpcore -mfloat-abi=hard -mtp=soft

CFLAGS  :=  -g -Wall -Wextra -O2 -mword-relocations -std=c11 \
            -ffunction-sections \
            $(ARCH)

CFLAGS  +=  $(INCLUDE) -D__3DS__

CXXFLAGS    :=  $(CFLAGS) -fno-rtti -fno-exceptions -std=gnu++11

ASFLAGS :=  -g $(ARCH)

LDFLAGS =   -specs=3dsx.specs -g $(ARCH) -Wl,-Map,$(notdir $*.map)

LIBS    :=  -lcurl -lmbedtls -lmbedx509 -lmbedcrypto -lz -lctru -lm

#---------------------------------------------------------------------------------
# list of directories containing libraries (top level with include/ and lib/)
#---------------------------------------------------------------------------------
LIBDIRS :=  $(CTRULIB) $(PORTLIBS)

#---------------------------------------------------------------------------------
# no need to edit past this point unless adding rules for new file extensions
#---------------------------------------------------------------------------------
ifneq ($(BUILD),$(notdir $(CURDIR)))
#---------------------------------------------------------------------------------

export OUTPUT   :=  $(CURDIR)/$(TARGET)
export TOPDIR   :=  $(CURDIR)

export VPATH    :=  $(foreach dir,$(SOURCES),$(CURDIR)/$(dir)) \
                    $(foreach dir,$(DATA),$(CURDIR)/$(dir))

export DEPSDIR  :=  $(CURDIR)/$(BUILD)

CFILES      :=  $(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.c)))
CPPFILES    :=  $(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.cpp)))
SFILES      :=  $(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.s)))
BINFILES    :=  $(foreach dir,$(DATA),$(notdir $(wildcard $(dir)/*.*)))

#---------------------------------------------------------------------------------
# use CXX for linking C++ projects, CC for standard C
#---------------------------------------------------------------------------------
ifeq ($(strip $(CPPFILES)),)
export LD   :=  $(CC)
else
export LD   :=  $(CXX)
endif

export OFILES_SOURCES  :=  $(CPPFILES:.cpp=.o) $(CFILES:.c=.o) $(SFILES:.s=.o)
export OFILES_BIN      :=  $(addsuffix .o,$(BINFILES))
export OFILES          :=  $(OFILES_BIN) $(OFILES_SOURCES)

export HFILES :=

export INCLUDE  :=  $(foreach dir,$(INCLUDES),-I$(CURDIR)/$(dir)) \
                    $(foreach dir,$(LIBDIRS),-I$(dir)/include) \
                    -I$(CURDIR)/$(BUILD)

export LIBPATHS :=  $(foreach dir,$(LIBDIRS),-L$(dir)/lib)

export _3DSXDEPS    :=  $(if $(NO_SMDH),,$(OUTPUT).smdh)

ifeq ($(strip $(ICON)),)
    icons := $(wildcard *.png)
    ifneq (,$(findstring $(TARGET).png,$(icons)))
        export APP_ICON := $(TOPDIR)/$(TARGET).png
    else
        ifneq (,$(findstring icon.png,$(icons)))
            export APP_ICON := $(TOPDIR)/icon.png
        endif
    endif
else
    export APP_ICON := $(TOPDIR)/$(ICON)
endif

ifeq ($(strip $(NO_SMDH)),)
    export _3DSXFLAGS += --smdh=$(CURDIR)/$(TARGET).smdh
endif

.PHONY: all clean

#---------------------------------------------------------------------------------
all: $(BUILD) $(DEPSDIR)
	@$(MAKE) --no-print-directory -C $(BUILD) -f $(CURDIR)/Makefile

$(BUILD):
	@mkdir -p $@

ifneq ($(DEPSDIR),$(BUILD))
$(DEPSDIR):
	@mkdir -p $@
endif

#---------------------------------------------------------------------------------
clean:
	@echo clean ...
	@rm -fr $(BUILD) $(TARGET).3dsx $(OUTPUT).smdh $(TARGET).elf $(TARGET).cia

#---------------------------------------------------------------------------------
# send: upload the .3dsx to a 3DS running the Homebrew Launcher netloader (press Y).
# Usage: make send IP=192.168.1.42
#---------------------------------------------------------------------------------
send: $(BUILD) $(DEPSDIR)
	@$(MAKE) --no-print-directory -C $(BUILD) -f $(CURDIR)/Makefile
	@if [ -z "$(IP)" ]; then \
		echo "Usage: make send IP=<3DS-IP>  (enable netloader in Homebrew Launcher with Y)"; \
		exit 1; \
	fi
	@echo "sending $(TARGET).3dsx to $(IP) ..."
	@3dslink -a $(IP) $(CURDIR)/$(TARGET).3dsx

.PHONY: send

#---------------------------------------------------------------------------------
# cia: build a .cia for Home Menu installation.
# Requires makerom + bannertool in PATH (installed to devkitPro tools/bin).
# Usage: make cia
#---------------------------------------------------------------------------------
cia: $(BUILD) $(DEPSDIR)
	@$(MAKE) --no-print-directory -C $(BUILD) -f $(CURDIR)/Makefile
	@echo "building CIA..."
	@bannertool makebanner -i $(CURDIR)/banner.png -a $(CURDIR)/audio.wav -o $(CURDIR)/$(BUILD)/banner.bnr
	@bannertool makesmdh -s "$(APP_TITLE)" -l "$(APP_DESCRIPTION)" -p "$(APP_AUTHOR)" -i $(CURDIR)/icon.png -o $(CURDIR)/$(BUILD)/$(TARGET).smdh
	@makerom -f cia -o $(CURDIR)/$(TARGET).cia -elf $(CURDIR)/$(TARGET).elf -banner $(CURDIR)/$(BUILD)/banner.bnr -icon $(CURDIR)/$(BUILD)/$(TARGET).smdh -rsf $(CURDIR)/cia.rsf -target t
	@echo "built ... $(TARGET).cia"

.PHONY: cia

#---------------------------------------------------------------------------------
else

#---------------------------------------------------------------------------------
# main targets
#---------------------------------------------------------------------------------
$(OUTPUT).3dsx  :   $(OUTPUT).elf $(_3DSXDEPS)

$(OFILES_SOURCES) : $(HFILES)

$(OUTPUT).elf   :   $(OFILES)

#---------------------------------------------------------------------------------
# rule for binary data files
#---------------------------------------------------------------------------------
%.bin.o %_bin.h :   %.bin
	@echo $(notdir $<)
	@$(bin2o)

-include $(DEPSDIR)/*.d

#---------------------------------------------------------------------------------------
endif
#---------------------------------------------------------------------------------------
