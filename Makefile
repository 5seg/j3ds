#---------------------------------------------------------------------------------
.SUFFIXES:
#---------------------------------------------------------------------------------

ifeq ($(strip $(DEVKITARM)),)
$(error "Please set DEVKITARM in your environment. export DEVKITARM=<path to>devkitARM")
endif

TOPDIR ?= $(CURDIR)
include $(DEVKITARM)/3ds_rules

#---------------------------------------------------------------------------------
# TARGET is the name of the output
# BUILD is the directory where object files & intermediate files will be placed
# SOURCES is a list of directories containing source code
# DATA is a list of directories containing data files
# INCLUDES is a list of directories containing header files
# GRAPHICS is a list of directories containing graphics files
# GFXBUILD is the directory where converted graphics files will be placed
#   If set to $(BUILD), it will statically link in the converted
#   files as if they were data files.
#
# NO_SMDH: if set to anything, no SMDH file is generated.
# ROMFS is the directory which contains the RomFS, relative to the Makefile (Optional)
# APP_TITLE is the name of the app stored in the SMDH file (Optional)
# APP_DESCRIPTION is the description of the app stored in the SMDH file (Optional)
# APP_AUTHOR is the author of the app stored in the SMDH file (Optional)
# ICON is the filename of the icon (.png), relative to the project folder.
#   If not set, it attempts to use one of the following (in this order):
#     - <Project name>.png
#     - icon.png
#     - <libctru folder>/default_icon.png
#---------------------------------------------------------------------------------
TARGET		:=	j3ds
BUILD		?=	build
OUTPUT		?=	$(TARGET)
SOURCES		:=	source source/ui source/net source/audio source/sys source/storage source/utils
DATA		:=	data
INCLUDES	:=	include source
GRAPHICS	:=	gfx
GFXBUILD	:=	$(BUILD)
ROMFS		:=	romfs

APP_TITLE		:=	Jellyfin 3DS
APP_DESCRIPTION	:=	Jellyfin music client
APP_AUTHOR		:=	j3ds
ICON			:=	meta/icon.png

BANNER		:=	meta/banner.png
BANNER_AUDIO	:=	meta/audio.wav
BANNER_BIN		:=	$(BUILD)/banner.bin
ICON_BIN		:=	$(BUILD)/icon.bin
RSF				:=	meta/j3ds.rsf

#---------------------------------------------------------------------------------
# options for code generation
#---------------------------------------------------------------------------------
ARCH	:=	-march=armv6k -mtune=mpcore -mfloat-abi=hard -mtp=soft

CFLAGS	:=	-g -Wall -O2 -mword-relocations \
			-ffunction-sections \
			$(ARCH)

CFLAGS	+=	$(INCLUDE) -D__3DS__

CXXFLAGS	:=	$(CFLAGS) -fno-rtti -fno-exceptions -std=gnu++11

ASFLAGS	:=	-g $(ARCH)
LDFLAGS	=	-specs=3dsx.specs -g $(ARCH) -Wl,-Map,$(BUILD)/$(basename $(notdir $@)).map

LIBS	:=	-ljansson -lmpg123 -lvorbisidec -lopusfile -lopus -logg -ljpeg -lpng -lz -lm -lctru

#---------------------------------------------------------------------------------
# list of directories containing libraries, this must be the top level containing
# include and lib
#---------------------------------------------------------------------------------
LIBDIRS	:=	$(CTRULIB) $(DEVKITPRO)/portlibs/3ds

#---------------------------------------------------------------------------------
# no real need to edit anything past this point unless you need to add additional
# rules for different file extensions
#---------------------------------------------------------------------------------
VPATH	:=	$(CURDIR) \
			$(foreach dir,$(SOURCES),$(CURDIR)/$(dir)) \
			$(foreach dir,$(GRAPHICS),$(CURDIR)/$(dir)) \
			$(foreach dir,$(DATA),$(CURDIR)/$(dir))

DEPSDIR	:=	$(BUILD)

CFILES		:=	$(foreach dir,$(SOURCES),$(wildcard $(dir)/*.c))
CPPFILES	:=	$(foreach dir,$(SOURCES),$(wildcard $(dir)/*.cpp))
SFILES		:=	$(foreach dir,$(SOURCES),$(wildcard $(dir)/*.s))
PICAFILES	:=	$(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.v.pica)))
SHLISTFILES	:=	$(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.shlist)))
GFXFILES	:=	$(foreach dir,$(GRAPHICS),$(notdir $(wildcard $(dir)/*.t3s)))
BINFILES	:=	$(foreach dir,$(DATA),$(notdir $(wildcard $(dir)/*.*)))

#---------------------------------------------------------------------------------
# use CXX for linking C++ projects, CC for standard C
#---------------------------------------------------------------------------------
ifeq ($(strip $(CPPFILES)),)
	LD	:=	$(CC)
else
	LD	:=	$(CXX)
endif
#---------------------------------------------------------------------------------

#---------------------------------------------------------------------------------
ifeq ($(GFXBUILD),$(BUILD))
#---------------------------------------------------------------------------------
T3XFILES :=	$(GFXFILES:.t3s=.t3x)
#---------------------------------------------------------------------------------
else
#---------------------------------------------------------------------------------
ROMFS_T3XFILES	:=	$(patsubst %.t3s, $(GFXBUILD)/%.t3x, $(GFXFILES))
T3XHFILES		:=	$(patsubst %.t3s, $(BUILD)/%.h, $(GFXFILES))
#---------------------------------------------------------------------------------
endif
#---------------------------------------------------------------------------------

OFILES_SOURCES 	:=	$(patsubst %.cpp,$(BUILD)/%.o,$(CPPFILES)) \
					$(patsubst %.c,$(BUILD)/%.o,$(CFILES)) \
					$(patsubst %.s,$(BUILD)/%.o,$(SFILES))

OFILES_BIN	:=	$(addprefix $(BUILD)/,$(addsuffix .o,$(BINFILES))) \
			$(addprefix $(BUILD)/,$(PICAFILES:.v.pica=.shbin.o)) \
			$(addprefix $(BUILD)/,$(SHLISTFILES:.shlist=.shbin.o)) \
			$(addprefix $(BUILD)/,$(addsuffix .o,$(T3XFILES)))

OFILES	:=	$(OFILES_BIN) $(OFILES_SOURCES)

HFILES	:=	$(addprefix $(BUILD)/,$(PICAFILES:.v.pica=_shbin.h)) \
			$(addprefix $(BUILD)/,$(SHLISTFILES:.shlist=_shbin.h)) \
			$(addprefix $(BUILD)/,$(addsuffix .h,$(subst .,_,$(BINFILES)))) \
			$(addprefix $(BUILD)/,$(GFXFILES:.t3s=.h))

INCLUDE	:=	$(foreach dir,$(INCLUDES),-I$(CURDIR)/$(dir)) \
			$(foreach dir,$(LIBDIRS),-I$(dir)/include) \
			-I$(CURDIR)/$(BUILD)

LIBPATHS	:=	$(foreach dir,$(LIBDIRS),-L$(dir)/lib)

_3DSXDEPS	:=	$(if $(NO_SMDH),,$(OUTPUT).smdh)

ifeq ($(strip $(ICON)),)
	icons := $(wildcard *.png)
	ifneq (,$(findstring $(TARGET).png,$(icons)))
		APP_ICON := $(TOPDIR)/$(TARGET).png
	else
		ifneq (,$(findstring icon.png,$(icons)))
			APP_ICON := $(TOPDIR)/icon.png
		endif
	endif
else
	APP_ICON := $(TOPDIR)/$(ICON)
endif

ifeq ($(strip $(NO_SMDH)),)
	_3DSXFLAGS += --smdh=$(OUTPUT).smdh
endif

ifneq ($(ROMFS),)
	_3DSXFLAGS += --romfs=$(TOPDIR)/$(ROMFS)
endif

#---------------------------------------------------------------------------------
.PHONY: all clean cia
#---------------------------------------------------------------------------------
all: $(OUTPUT).3dsx $(OUTPUT).cia

cia: $(OUTPUT).cia

#---------------------------------------------------------------------------------
$(BUILD):
	@mkdir -p $@

ifneq ($(GFXBUILD),$(BUILD))
$(GFXBUILD):
	@mkdir -p $@
endif

ifneq ($(DEPSDIR),$(BUILD))
$(DEPSDIR):
	@mkdir -p $@
endif

#---------------------------------------------------------------------------------
clean:
	@echo clean ...
	@rm -fr $(BUILD) $(OUTPUT).3dsx $(OUTPUT).smdh $(OUTPUT).elf $(OUTPUT).cia $(GFXBUILD)
	@rm -f $(BANNER_BIN) $(ICON_BIN)

#---------------------------------------------------------------------------------
$(GFXBUILD)/%.t3x	$(BUILD)/%.h	:	%.t3s
#---------------------------------------------------------------------------------
	@echo $(notdir $<)
	@tex3ds -i $< -H $(BUILD)/$*.h -d $(DEPSDIR)/$*.d -o $(GFXBUILD)/$*.t3x

#---------------------------------------------------------------------------------
# main targets
#---------------------------------------------------------------------------------
$(patsubst %.c,$(BUILD)/%.o,$(CFILES)): $(BUILD)/%.o: %.c $(HFILES)
	@mkdir -p $(dir $@)
	$(SILENTCMD)$(CC) $(CFLAGS) $(INCLUDE) -MMD -MP -MF $(BUILD)/$*.d -c $< -o $@

$(patsubst %.cpp,$(BUILD)/%.o,$(CPPFILES)): $(BUILD)/%.o: %.cpp $(HFILES)
	@mkdir -p $(dir $@)
	$(SILENTCMD)$(CXX) $(CXXFLAGS) $(INCLUDE) -MMD -MP -MF $(BUILD)/$*.d -c $< -o $@

$(patsubst %.s,$(BUILD)/%.o,$(SFILES)): $(BUILD)/%.o: %.s $(HFILES)
	@mkdir -p $(dir $@)
	$(SILENTCMD)$(CC) $(ASFLAGS) -MMD -MP -MF $(BUILD)/$*.d -c $< -o $@

$(OUTPUT).elf	:	$(OFILES)
	$(SILENTCMD)$(LD) $(LDFLAGS) $(OFILES) $(LIBPATHS) $(LIBS) -o $@

$(OUTPUT).3dsx	:	$(OUTPUT).elf $(_3DSXDEPS)

$(OUTPUT).smdh: $(APP_ICON)
	$(SILENTCMD)smdhtool --create "$(APP_TITLE)" "$(APP_DESCRIPTION)" "$(APP_AUTHOR)" $< $@

$(OUTPUT).cia	:	$(OUTPUT).elf $(BANNER_BIN) $(ICON_BIN) $(TOPDIR)/$(ROMFS) $(TOPDIR)/$(RSF)
	@echo building cia...
	$(SILENTCMD)makerom -f cia -o $@ -target t -elf $(OUTPUT).elf -icon $(ICON_BIN) -banner $(BANNER_BIN) -rsf $(TOPDIR)/$(RSF) -DROMFS_PATH=$(TOPDIR)/$(ROMFS)

$(BANNER_BIN) : $(TOPDIR)/$(BANNER) $(TOPDIR)/$(BANNER_AUDIO)
	@mkdir -p $(BUILD)
	$(SILENTCMD)bannertool makebanner -i $(TOPDIR)/$(BANNER) -a $(TOPDIR)/$(BANNER_AUDIO) -o $@

$(ICON_BIN) : $(APP_ICON)
	@mkdir -p $(BUILD)
	$(SILENTCMD)bannertool makesmdh -s "$(APP_TITLE)" -l "$(APP_DESCRIPTION)" -p "$(APP_AUTHOR)" -i $(APP_ICON) -o $@

#---------------------------------------------------------------------------------
# you need a rule like this for each extension you use as binary data
#---------------------------------------------------------------------------------
$(BUILD)/%.bin.o	$(BUILD)/%_bin.h :	%.bin
#---------------------------------------------------------------------------------
	@echo $(notdir $<)
	@$(bin2o)

#---------------------------------------------------------------------------------
.PRECIOUS	:	$(BUILD)/%.t3x $(BUILD)/%.shbin
#---------------------------------------------------------------------------------
$(BUILD)/%.t3x.o	$(BUILD)/%_t3x.h :	%.t3x
#---------------------------------------------------------------------------------
	$(SILENTMSG) $(notdir $<)
	$(bin2o)

#---------------------------------------------------------------------------------
$(BUILD)/%.shbin.o $(BUILD)/%_shbin.h : %.shbin
#---------------------------------------------------------------------------------
	$(SILENTMSG) $(notdir $<)
	$(bin2o)

-include $(shell find $(DEPSDIR) -name '*.d' 2>/dev/null)

#---------------------------------------------------------------------------------------
