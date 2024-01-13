#os detection
ifeq ($(OS),Windows_NT)     # is Windows_NT on XP, 2000, 7, Vista, 10...
    SYS_OS := Windows
else
    SYS_OS := $(shell uname)  # same as "uname -s"
endif
SYS_OS := $(patsubst CYGWIN%,Windows,$(SYS_OS))
SYS_OS := $(patsubst MSYS%,Windows,$(SYS_OS))
SYS_OS := $(patsubst MINGW%,Windows,$(SYS_OS))

$(info ~~~~~ Makefile configured for $(SYS_OS))
$(info -----)

#build default
ifeq ($(SYS_OS),Windows)
.DEFAULT_GOAL := win
CLEAN_DIR := ./wobj
endif
ifeq ($(SYS_OS),Linux)
.DEFAULT_GOAL := linux
CLEAN_DIR := ./lobj
endif
ifeq ($(SYS_OS),Darwin)
.DEFAULT_GOAL := mac
CLEAN_DIR := ./mobj
endif

#build with clang
CC := clang
CXX := clang++

#universal cfg
SRC_DIRS := ./src
SRCS := $(shell find $(SRC_DIRS) -name *.cpp -or -name *.c -or -name *.s)
DEPS := $(OBJS:.o=.d)
INC_DIRS := $(shell find $(SRC_DIRS) -type d)
INC_FLAGS := $(addprefix -I,$(INC_DIRS)) -I./include
MKDIR_P ?= mkdir -p

#windows cfg
WBUILD_DIR := ./wobj
WOBJS := $(SRCS:%=$(WBUILD_DIR)/%.o)
WCPPFLAGS ?= $(INC_FLAGS) -std=c11 -Wall -m64 -O2

#linux cfg
LBUILD_DIR := ./lobj
LOBJS := $(SRCS:%=$(LBUILD_DIR)/%.o)
LCPPFLAGS ?= $(INC_FLAGS) -std=gnu11 -Wall -m64 -O2

#macos cfg
MBUILD_DIR := ./mobj
MOBJS := $(SRCS:%=$(MBUILD_DIR)/%.o)
MCPPFLAGS ?= $(INC_FLAGS) -std=gnu11 -Wall -m64 -O2

# assembly
./wobj/%.s.o: %.s
	$(MKDIR_P) $(dir $@)
	$(AS) $(ASFLAGS) -c $< -o $@

./lobj/%.s.o: %.s
	$(MKDIR_P) $(dir $@)
	$(AS) $(ASFLAGS) -c $< -o $@

# c source, windows
./wobj/%.c.o: %.c %.h
	$(MKDIR_P) $(dir $@)
	$(CC) $(WCPPFLAGS) $(CFLAGS) -c $< -o $@

# c++ source, windows
./wobj/%.cpp.o: %.cpp
	$(MKDIR_P) $(dir $@)
	$(CXX) $(WCPPFLAGS) $(CXXFLAGS) -c $< -o $@

# c source, linux
./lobj/%.c.o: %.c %.h
	$(MKDIR_P) $(dir $@)
	$(CC) $(LCPPFLAGS) $(CFLAGS) -c $< -o $@

# c++ source, linux
./lobj/%.cpp.o: %.cpp
	$(MKDIR_P) $(dir $@)
	$(CXX) $(LCPPFLAGS) $(CXXFLAGS) -c $< -o $@

# c source, macos
./mobj/%.c.o: %.c %.h
	$(MKDIR_P) $(dir $@)
	$(CC) $(MCPPFLAGS) $(CFLAGS) -c $< -o $@

# c++ source, macos
./mobj/%.cpp.o: %.cpp
	$(MKDIR_P) $(dir $@)
	$(CXX) $(MCPPFLAGS) $(CXXFLAGS) -c $< -o $@

.PHONY: clean

linux: $(LOBJS)
	$(eval LDFLAGS=$(LLDFLAGS))
	$(info -----)
	$(CC) $(LOBJS) -o $(LBUILD_DIR)/sweeps $(LDFLAGS)

mac: $(MOBJS)
	$(eval LDFLAGS=$(MLDFLAGS))
	$(info -----)
	$(CC) $(MOBJS) -o $(MBUILD_DIR)/sweeps $(LDFLAGS)

win: $(WOBJS)
	$(eval LDFLAGS=$(WLDFLAGS))
	$(info -----)
	$(CC) $(WOBJS) -o $(WBUILD_DIR)/sweeps.exe /ucrt64/lib/libwinpthread.a $(LDFLAGS)

clean:
	$(RM) -r $(CLEAN_DIR)

-include $(DEPS)

