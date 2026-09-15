# gravestone
#
# One line per source file. C pulls nothing in by itself, so a file absent
# from this list does not exist as far as the program is concerned.
#
# Four ways to run the tests, cheapest first.
#
#   make check          the tests that touch nothing outside this process,
#                       plus the parts of the window test that need no
#                       window. Opens nothing on screen. Run it after
#                       every edit.
#   make quick          only the tests that can see what changed, working
#                       that out from the compiler's own header lists.
#                       Names the ones it left out. SLOW=1 runs those too,
#                       SINCE=<revision> compares against a commit instead
#                       of against the last build.
#   make t-<name>       one test on its own, so make t-store runs
#                       build/store_test.
#
# The editor's own list of header directories is rebuilt by any ordinary
# build whenever this file changes, so a new module never shows up as a
# red underline on a file that compiles.
#   make test           every test, still with windows turned off. Run
#                       it before a commit.
#   make test-visible   the same sweep with the windows really drawn.
#                       Takes over the screen while it runs, so it is
#                       asked for by name and never done by accident.

CC      ?= gcc
CSTD     = -std=c11
WARN     = -Wall -Wextra -Werror -Wshadow -Wconversion -Wpointer-arith \
           -Wstrict-prototypes -Wmissing-prototypes
OPT     ?= -O2 -g
DEFS     = -D_POSIX_C_SOURCE=200809L
INCLUDE  = -Iinclude -Isrc/core -Isrc/lib/window -Isrc/lib/db \
           -Isrc/lib/paths -Isrc/lib/proc -Isrc/lib/http -Isrc/lib/sig -Isrc/lib/ghost -Isrc/lib/picker -Isrc/catalogue -Isrc/library -Isrc/detect -Isrc/mount -Isrc/store -Isrc/session -Isrc/provider -Isrc/verify -Isrc/harness -Isrc/ui
CFLAGS   = $(CSTD) $(WARN) $(OPT) $(DEFS) $(INCLUDE)
LDFLAGS  =
LDLIBS   = -lpthread -lm

# The vendored SQLite amalgamation is somebody else's code and does not pass
# the settings above, so it gets its own. The feature flags drop the parts
# this project never uses and turn off the double quoted string misfeature.
SQLITE_WARN = -w
SQLITE_DEFS = -DSQLITE_THREADSAFE=1 \
              -DSQLITE_OMIT_LOAD_EXTENSION \
              -DSQLITE_OMIT_DEPRECATED \
              -DSQLITE_DQS=0 \
              -DSQLITE_DEFAULT_MEMSTATUS=0 \
              -DSQLITE_DEFAULT_WAL_SYNCHRONOUS=1 \
              -DSQLITE_LIKE_DOESNT_MATCH_BLOBS \
              -DSQLITE_MAX_EXPR_DEPTH=0 \
              -DSQLITE_USE_ALLOCA

BUILD    = build
BIN      = $(BUILD)/gravestone

# ---- sources, one line each ----
SRC  = app.c
SRC += src/core/log.c
SRC += src/core/hash.c
SRC += src/core/str.c
SRC += src/lib/paths/paths.c
SRC += src/lib/proc/proc.c
SRC += src/lib/http/http.c
SRC += src/lib/sig/sig.c
SRC += src/lib/ghost/ghost.c
SRC += src/lib/ghost/ghost_encode.c
SRC += src/lib/picker/picker.c
# The machine is read differently on each platform, and both readings
# fill in the same report.
ifeq ($(OS),Windows_NT)
DETECT_SRC = src/detect/detect_win32.c
else
DETECT_SRC = src/detect/detect_linux.c
endif
SRC += $(DETECT_SRC)
SRC += src/detect/detect_common.c
DETECT_OBJ = $(DETECT_SRC:%.c=$(BUILD)/%.o) $(BUILD)/src/detect/detect_common.o

# Putting a Windows drive back is a thing only WSL can do, and the
# Windows build has no drives to put back, so the half that touches the
# machine is chosen the same way the machine reading is.
ifeq ($(OS),Windows_NT)
MOUNT_SRC = src/mount/mount_win32.c
else
MOUNT_SRC = src/mount/mount_linux.c src/mount/mount_road.c \
            src/mount/mount_repair.c
endif
SRC += src/mount/mount.c
SRC += src/mount/mount_store.c
SRC += src/mount/mount_fstab.c
SRC += $(MOUNT_SRC)
MOUNT_OBJ = $(BUILD)/src/mount/mount.o $(BUILD)/src/mount/mount_store.o \
            $(BUILD)/src/mount/mount_fstab.o \
            $(MOUNT_SRC:%.c=$(BUILD)/%.o)
SRC += src/catalogue/catalogue.c
SRC += src/catalogue/catalogue_room.c
SRC += src/catalogue/catalogue_report.c
SRC += src/library/library.c
SRC += src/library/library_gguf.c
SRC += src/store/store.c
SRC += src/store/schema.c
SRC += src/store/store_settings.c
SRC += src/session/session.c
SRC += src/session/session_read.c
SRC += src/provider/provider.c
SRC += src/provider/provider_json.c
SRC += src/verify/verify.c
SRC += src/verify/verify_text.c
SRC += src/verify/verify_corpus.c
SRC += src/harness/harness.c
SRC += src/harness/harness_history.c
# The window is drawn by whatever the machine has. X11 speaks to a
# server over a socket, and Windows draws through the system itself.
ifeq ($(OS),Windows_NT)
WINDOW_SRC = src/lib/window/window_win32.c src/lib/window/window_win32_draw.c
LDLIBS += -lgdi32 -luser32 -lcomdlg32
else
WINDOW_SRC = src/lib/window/window_x11.c src/lib/window/window_x11_conn.c \
             src/lib/window/window_x11_draw.c \
             src/lib/window/window_x11_state.c \
             src/lib/window/window_x11_glyph.c \
             src/lib/window/window_face.c
endif
SRC += $(WINDOW_SRC)
WINDOW_OBJ = $(WINDOW_SRC:%.c=$(BUILD)/%.o)

# Every test of the interface links the same objects. Naming them once
# means a new file under src/ui/ costs one line here rather than three.
UI_OBJ = $(MOUNT_OBJ) $(BUILD)/src/ui/ui.o $(BUILD)/src/ui/ui_record.o $(BUILD)/src/ui/ui_scroll.o $(BUILD)/src/ui/ui_paint.o $(BUILD)/src/ui/ui_panes.o $(BUILD)/src/ui/ui_models.o $(BUILD)/src/ui/ui_models_draw.o $(BUILD)/src/ui/ui_models_text.o $(BUILD)/src/ui/ui_confirm.o $(BUILD)/src/ui/ui_home.o $(BUILD)/src/ui/ui_chat.o $(BUILD)/src/ui/ui_chat_layout.o $(BUILD)/src/ui/ui_fleet.o $(BUILD)/src/ui/ui_settings.o $(BUILD)/src/ui/ui_secret.o $(BUILD)/src/ui/ui_mount.o $(BUILD)/src/ui/ui_actions.o $(BUILD)/src/ui/ui_chat_actions.o $(BUILD)/src/ui/ui_specs.o $(WINDOW_OBJ) $(BUILD)/src/store/store.o $(BUILD)/src/store/schema.o $(BUILD)/src/store/store_settings.o $(BUILD)/src/session/session.o $(BUILD)/src/session/session_read.o $(BUILD)/src/provider/provider.o $(BUILD)/src/provider/provider_json.o $(BUILD)/src/verify/verify.o $(BUILD)/src/verify/verify_text.o $(BUILD)/src/verify/verify_corpus.o $(BUILD)/src/harness/harness.o $(BUILD)/src/harness/harness_history.o $(DETECT_OBJ) $(BUILD)/src/catalogue/catalogue.o $(BUILD)/src/catalogue/catalogue_room.o $(BUILD)/src/catalogue/catalogue_report.o $(BUILD)/src/library/library.o $(BUILD)/src/library/library_gguf.o $(BUILD)/src/lib/http/http.o $(BUILD)/src/lib/sig/sig.o $(BUILD)/src/lib/ghost/ghost.o $(BUILD)/src/lib/ghost/ghost_encode.o $(BUILD)/src/lib/picker/picker.o $(BUILD)/src/lib/paths/paths.o $(BUILD)/src/lib/proc/proc.o $(BUILD)/src/lib/db/db.o $(BUILD)/src/lib/db/sqlite3.o $(BUILD)/src/core/log.o $(BUILD)/src/core/hash.o $(BUILD)/src/core/str.o
SRC += src/lib/db/db.c
SRC += src/lib/db/sqlite3.c
SRC += src/ui/ui.c
SRC += src/ui/ui_paint.c
SRC += src/ui/ui_panes.c
SRC += src/ui/ui_models.c
SRC += src/ui/ui_models_draw.c
SRC += src/ui/ui_models_text.c
SRC += src/ui/ui_confirm.c
SRC += src/ui/ui_home.c
SRC += src/ui/ui_chat.c
SRC += src/ui/ui_chat_layout.c
SRC += src/ui/ui_record.c
SRC += src/ui/ui_scroll.c
SRC += src/ui/ui_fleet.c
SRC += src/ui/ui_settings.c
SRC += src/ui/ui_secret.c
SRC += src/ui/ui_mount.c
SRC += src/ui/ui_actions.c
SRC += src/ui/ui_chat_actions.c
SRC += src/ui/ui_specs.c

# The editor keeps its own list of where headers live, and a module added
# to the Makefile without it lands as a red underline on a file that
# builds. The list depends on this file, so an ordinary build rebuilds it
# whenever the include line changes and the two cannot drift apart.
EDITOR_PATHS = .vscode/c_cpp_properties.json

OBJ  = $(SRC:%.c=$(BUILD)/%.o)
DEP  = $(OBJ:.o=.d)

# Every test binary writes its own dependency file next to itself, naming
# the headers it read. Leaving those out of the include below meant a
# changed header rebuilt the library objects and left the test binary
# alone, so a test could pass against code it was never compiled with.
TEST_DEP = $(FAST_TEST:=.d) $(SLOW_TEST:=.d)

# ---- test binaries, one line each ----
# Tests that touch nothing outside this process. Together they take about
# a fifth of a second, so they are the ones to run after every edit.
FAST_TEST  = $(BUILD)/paths_test
FAST_TEST += $(BUILD)/db_test
FAST_TEST += $(BUILD)/http_test
FAST_TEST += $(BUILD)/catalogue_test
FAST_TEST += $(BUILD)/store_test
FAST_TEST += $(BUILD)/library_test
FAST_TEST += $(BUILD)/session_test
FAST_TEST += $(BUILD)/provider_test
FAST_TEST += $(BUILD)/verify_test
FAST_TEST += $(BUILD)/harness_test
FAST_TEST += $(BUILD)/sig_test
FAST_TEST += $(BUILD)/str_test
FAST_TEST += $(BUILD)/mount_test
FAST_TEST += $(BUILD)/ui_paint_test

# Tests that open a window, start another program, or speak to the
# Windows side. Each one costs between a fifth of a second and two
# seconds, and the ones needing a screen are at the mercy of whatever
# compositor is running.
SLOW_TEST  = $(BUILD)/window_test
SLOW_TEST += $(BUILD)/ui_test
SLOW_TEST += $(BUILD)/ui_panes_test
SLOW_TEST += $(BUILD)/proc_test
SLOW_TEST += $(BUILD)/detect_test
SLOW_TEST += $(BUILD)/ghost_test
SLOW_TEST += $(BUILD)/picker_test

TEST_BIN = $(FAST_TEST) $(SLOW_TEST)

.PHONY: all clean test run

all: $(BIN) $(EDITOR_PATHS)

$(EDITOR_PATHS): Makefile
	@$(MAKE) --no-print-directory vscode >/dev/null

$(BIN): $(OBJ)
	@mkdir -p $(dir $@)
	$(CC) $(LDFLAGS) -o $@ $^ $(LDLIBS)

$(BUILD)/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -MMD -MP -c $< -o $@

# Vendored code, compiled on its own terms.
$(BUILD)/src/lib/db/sqlite3.o: src/lib/db/sqlite3.c
	@mkdir -p $(dir $@)
	$(CC) $(CSTD) $(SQLITE_WARN) $(OPT) $(SQLITE_DEFS) -MMD -MP -c $< -o $@

$(BUILD)/window_test: src/lib/window/window_test.c $(WINDOW_OBJ) $(BUILD)/src/core/log.o
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -MMD -MP -MF $@.d $(LDFLAGS) -o $@ $^ $(LDLIBS)

$(BUILD)/proc_test: src/lib/proc/proc_test.c $(BUILD)/src/lib/proc/proc.o $(BUILD)/src/core/log.o $(BUILD)/src/core/hash.o $(BUILD)/src/core/str.o
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -MMD -MP -MF $@.d $(LDFLAGS) -o $@ $^ $(LDLIBS)

$(BUILD)/sig_test: src/lib/sig/sig_test.c $(BUILD)/src/lib/sig/sig.o $(BUILD)/src/core/log.o $(BUILD)/src/core/hash.o $(BUILD)/src/core/str.o
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -MMD -MP -MF $@.d $(LDFLAGS) -o $@ $^ $(LDLIBS)

$(BUILD)/picker_test: src/lib/picker/picker_test.c $(BUILD)/src/lib/picker/picker.o $(BUILD)/src/core/log.o $(BUILD)/src/core/hash.o $(BUILD)/src/core/str.o
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -MMD -MP -MF $@.d $(LDFLAGS) -o $@ $^ $(LDLIBS)

$(BUILD)/ghost_test: src/lib/ghost/ghost_test.c $(BUILD)/src/lib/ghost/ghost.o $(BUILD)/src/lib/ghost/ghost_encode.o $(BUILD)/src/core/log.o $(BUILD)/src/core/hash.o $(BUILD)/src/core/str.o
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -MMD -MP -MF $@.d $(LDFLAGS) -o $@ $^ $(LDLIBS)

$(BUILD)/harness_test: src/harness/harness_test.c $(BUILD)/src/harness/harness.o $(BUILD)/src/harness/harness_history.o $(BUILD)/src/verify/verify.o $(BUILD)/src/verify/verify_text.o $(BUILD)/src/verify/verify_corpus.o $(BUILD)/src/provider/provider.o $(BUILD)/src/provider/provider_json.o $(BUILD)/src/session/session.o $(BUILD)/src/session/session_read.o $(BUILD)/src/lib/http/http.o $(BUILD)/src/lib/db/db.o $(BUILD)/src/lib/db/sqlite3.o $(BUILD)/src/lib/paths/paths.o $(BUILD)/src/lib/proc/proc.o $(BUILD)/src/core/log.o $(BUILD)/src/core/hash.o $(BUILD)/src/core/str.o
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -MMD -MP -MF $@.d $(LDFLAGS) -o $@ $^ $(LDLIBS)

$(BUILD)/verify_test: src/verify/verify_test.c $(BUILD)/src/verify/verify.o $(BUILD)/src/verify/verify_text.o $(BUILD)/src/verify/verify_corpus.o $(BUILD)/src/core/log.o $(BUILD)/src/core/hash.o $(BUILD)/src/core/str.o
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -MMD -MP -MF $@.d $(LDFLAGS) -o $@ $^ $(LDLIBS)

$(BUILD)/provider_test: src/provider/provider_test.c $(BUILD)/src/provider/provider.o $(BUILD)/src/provider/provider_json.o $(BUILD)/src/lib/http/http.o $(BUILD)/src/lib/paths/paths.o $(BUILD)/src/lib/proc/proc.o $(BUILD)/src/core/log.o $(BUILD)/src/core/hash.o $(BUILD)/src/core/str.o
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -MMD -MP -MF $@.d $(LDFLAGS) -o $@ $^ $(LDLIBS)

$(BUILD)/mount_test: src/mount/mount_test.c $(MOUNT_OBJ) $(BUILD)/src/lib/db/db.o $(BUILD)/src/lib/db/sqlite3.o $(BUILD)/src/lib/paths/paths.o $(BUILD)/src/lib/proc/proc.o $(BUILD)/src/core/log.o $(BUILD)/src/core/hash.o $(BUILD)/src/core/str.o
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -MMD -MP -MF $@.d $(LDFLAGS) -o $@ $^ $(LDLIBS)

$(BUILD)/session_test: src/session/session_test.c $(BUILD)/src/session/session.o $(BUILD)/src/session/session_read.o $(BUILD)/src/lib/db/db.o $(BUILD)/src/lib/db/sqlite3.o $(BUILD)/src/lib/paths/paths.o $(BUILD)/src/core/log.o $(BUILD)/src/core/hash.o $(BUILD)/src/core/str.o
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -MMD -MP -MF $@.d $(LDFLAGS) -o $@ $^ $(LDLIBS)

$(BUILD)/str_test: src/core/str_test.c $(BUILD)/src/core/str.o $(BUILD)/src/core/log.o $(BUILD)/src/core/hash.o
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -MMD -MP -MF $@.d $(LDFLAGS) -o $@ $^ $(LDLIBS)

$(BUILD)/paths_test: src/lib/paths/paths_test.c $(BUILD)/src/lib/paths/paths.o $(BUILD)/src/core/log.o $(BUILD)/src/core/hash.o $(BUILD)/src/core/str.o
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -MMD -MP -MF $@.d $(LDFLAGS) -o $@ $^ $(LDLIBS)

$(BUILD)/detect_test: src/detect/detect_test.c $(DETECT_OBJ) $(BUILD)/src/lib/paths/paths.o $(BUILD)/src/lib/proc/proc.o $(BUILD)/src/core/log.o $(BUILD)/src/core/hash.o $(BUILD)/src/core/str.o
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -MMD -MP -MF $@.d $(LDFLAGS) -o $@ $^ $(LDLIBS)

$(BUILD)/store_test: src/store/store_test.c $(BUILD)/src/store/store.o $(BUILD)/src/store/schema.o $(BUILD)/src/store/store_settings.o $(BUILD)/src/session/session.o $(BUILD)/src/session/session_read.o $(BUILD)/src/provider/provider.o $(BUILD)/src/provider/provider_json.o $(BUILD)/src/verify/verify.o $(BUILD)/src/verify/verify_text.o $(BUILD)/src/verify/verify_corpus.o $(BUILD)/src/harness/harness.o $(BUILD)/src/harness/harness_history.o $(DETECT_OBJ) $(BUILD)/src/lib/http/http.o $(BUILD)/src/lib/paths/paths.o $(BUILD)/src/lib/proc/proc.o $(BUILD)/src/lib/db/db.o $(BUILD)/src/lib/db/sqlite3.o $(BUILD)/src/core/log.o $(BUILD)/src/core/hash.o $(BUILD)/src/core/str.o
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -MMD -MP -MF $@.d $(LDFLAGS) -o $@ $^ $(LDLIBS)

$(BUILD)/db_test: src/lib/db/db_test.c $(BUILD)/src/lib/db/db.o $(BUILD)/src/lib/db/sqlite3.o $(BUILD)/src/core/log.o $(BUILD)/src/core/hash.o $(BUILD)/src/core/str.o
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -MMD -MP -MF $@.d $(LDFLAGS) -o $@ $^ $(LDLIBS)

$(BUILD)/ui_test: src/ui/ui_test.c $(UI_OBJ)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -MMD -MP -MF $@.d $(LDFLAGS) -o $@ $^ $(LDLIBS)

$(BUILD)/ui_paint_test: src/ui/ui_paint_test.c $(UI_OBJ)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -MMD -MP -MF $@.d $(LDFLAGS) -o $@ $^ $(LDLIBS)

$(BUILD)/ui_panes_test: src/ui/ui_panes_test.c $(UI_OBJ)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -MMD -MP -MF $@.d $(LDFLAGS) -o $@ $^ $(LDLIBS)

$(BUILD)/catalogue_test: src/catalogue/catalogue_test.c src/catalogue/catalogue_test_room.c $(BUILD)/src/catalogue/catalogue.o $(BUILD)/src/catalogue/catalogue_room.o $(BUILD)/src/catalogue/catalogue_report.o $(DETECT_OBJ) $(BUILD)/src/lib/paths/paths.o $(BUILD)/src/lib/proc/proc.o $(BUILD)/src/core/log.o $(BUILD)/src/core/hash.o $(BUILD)/src/core/str.o
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -MMD -MP -MF $@.d $(LDFLAGS) -o $@ $^ $(LDLIBS)

$(BUILD)/library_test: src/library/library_test.c $(BUILD)/src/library/library.o $(BUILD)/src/library/library_gguf.o $(BUILD)/src/lib/http/http.o $(BUILD)/src/lib/proc/proc.o $(BUILD)/src/lib/paths/paths.o $(BUILD)/src/core/log.o $(BUILD)/src/core/hash.o $(BUILD)/src/core/str.o
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -MMD -MP -MF $@.d $(LDFLAGS) -o $@ $^ $(LDLIBS)

$(BUILD)/http_test: src/lib/http/http_test.c $(BUILD)/src/lib/http/http.o $(BUILD)/src/core/log.o
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -MMD -MP -MF $@.d $(LDFLAGS) -o $@ $^ $(LDLIBS)

# Every test, including the ones that open a window and the ones that
# start another program. This is the pass to run before a commit.
test: $(BIN) $(TEST_BIN)
	@for t in $(TEST_BIN); do echo "--- $$t"; GS_TEST_NO_WINDOW=1 $$t || \
		{ echo "*** $$t exited $$?"; exit 1; }; done
	@echo "all tests passed, and no window was opened"

# The tests that touch nothing outside this process. About a fifth of a
# second in total, so this is the one to run after every edit.
.PHONY: check
check: $(FAST_TEST) $(BUILD)/window_test
	@for t in $(FAST_TEST); do echo "--- $$t"; $$t || { echo "*** $$t exited $$?"; exit 1; }; done
	@echo "--- $(BUILD)/window_test, with windows turned off"
	@GS_TEST_NO_WINDOW=1 $(BUILD)/window_test || \
		{ echo "*** window_test exited $$?"; exit 1; }
	@echo "fast tests passed and no window was opened"

# Only the tests that can see the change. The compiler writes down every
# header each object read, and make knows which objects each test links,
# so the answer is worked out rather than kept in a list here. Compares
# against the last commit by default, or against SINCE=<revision>, or
# against file names given as FILES=<paths>.
.PHONY: quick print-tests
print-tests:
	@echo $(TEST_BIN)

quick:
	@hit=`sh tools/affected.sh $(SINCE) $(FILES)`; \
	if [ -z "$$hit" ]; then echo "nothing to do, the build is up to date"; exit 0; fi; \
	fast=""; slow=""; \
	for t in $$hit; do \
		case " $(SLOW_TEST) " in \
		*" $$t "*) slow="$$slow $$t";; \
		*) fast="$$fast $$t";; \
		esac; \
	done; \
	if [ -n "$(SLOW)" ]; then fast="$$fast$$slow"; slow=""; fi; \
	if [ -n "$$fast" ]; then \
		$(MAKE) --no-print-directory $$fast || exit 1; \
		for t in $$fast; do echo "--- $$t"; GS_TEST_NO_WINDOW=1 $$t || \
			{ echo "*** $$t exited $$?"; exit 1; }; done; \
	fi; \
	if [ -n "$$slow" ]; then \
		echo "left out, since each opens a window or starts another program:"; \
		for t in $$slow; do echo "    $$t"; done; \
		echo "run them with make quick SLOW=1, or make test for everything"; \
	else \
		echo "every test that can see the change passed"; \
	fi

# One test on its own, named without the build directory or the suffix,
# so make t-window runs build/window_test.
.PHONY: t-%
t-%: $(BUILD)/%_test
	@./$<

# The same sweep with the windows really drawn. Takes over the screen
# for as long as it runs, so it is asked for by name rather than being
# what any ordinary target does.
.PHONY: test-visible
test-visible: $(BIN) $(TEST_BIN)
	@for t in $(TEST_BIN); do echo "--- $$t"; $$t || \
		{ echo "*** $$t exited $$?"; exit 1; }; done
	@echo "all tests passed, windows and all"
	@$(MAKE) --no-print-directory ghosts >/dev/null 2>&1 || true

# A screen has to exist before the window opens, and on WSL the screen
# can claim to exist while carrying no pixels. tools/display.sh finds
# that case and repairs it, printing the address to draw on.
.PHONY: display display-install
display:
	@sh tools/display.sh >/dev/null || true

display-install:
	@GS_DISPLAY_INSTALL=1 sh tools/display.sh

run: $(BIN)
	@line=$$(sh tools/display.sh 2>/dev/null | grep '^DISPLAY=' || true); \
	if [ -n "$$line" ]; then \
		echo "$$line"; \
		env $$line $(BIN); \
	else \
		$(BIN); \
	fi

clean:
	rm -rf $(BUILD)

# WSLg keeps a Windows side mirror of every Linux window, and a window
# destroyed at the wrong moment leaves its mirror behind for ever, showing
# whatever the desktop held beneath it. The compositor holding the stale
# record sits outside this distribution, so the mirror cannot be destroyed
# from here, and hiding it is what removes it from the screen and the
# taskbar. Refuses to run while gravestone is up, since a live window
# matches the same pattern.
.PHONY: ghosts
ghosts:
	@if pgrep -f 'build/gravestone$$|build/.*_test$$' >/dev/null 2>&1; then \
		echo "gravestone is running, close it first"; exit 1; fi
	@if ! command -v powershell.exe >/dev/null 2>&1; then \
		echo "no Windows side reachable, nothing to do"; exit 0; fi
	@mkdir -p $(BUILD)
	@printf '%s\n' \
	  'Add-Type @"' \
	  'using System;' \
	  'using System.Text;' \
	  'using System.Runtime.InteropServices;' \
	  'public class H {' \
	  '  public delegate bool Cb(IntPtr h, IntPtr l);' \
	  '  [DllImport("user32.dll")] public static extern bool EnumWindows(Cb cb, IntPtr l);' \
	  '  [DllImport("user32.dll")] public static extern int GetWindowText(IntPtr h, StringBuilder s, int n);' \
	  '  [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr h);' \
	  '  [DllImport("user32.dll")] public static extern bool ShowWindowAsync(IntPtr h, int c);' \
	  '}' \
	  '"@' \
	  '$$n = 0' \
	  '$$cb = [H+Cb]{ param($$h, $$l)' \
	  '  $$sb = New-Object System.Text.StringBuilder 256' \
	  '  [void][H]::GetWindowText($$h, $$sb, 256)' \
	  '  $$t = $$sb.ToString()' \
	  '  if ($$t -like "Gravestone*(Debian)*" -and [H]::IsWindowVisible($$h)) {' \
	  '    [void][H]::ShowWindowAsync($$h, 0)' \
	  '    $$script:n++' \
	  '    Write-Output "hid: $$t"' \
	  '  }' \
	  '  return $$true' \
	  '}' \
	  '[void][H]::EnumWindows($$cb, [IntPtr]::Zero)' \
	  'Write-Output "lingering windows hidden: $$n"' \
	  > $(BUILD)/ghosts.ps1
	@powershell.exe -NoProfile -ExecutionPolicy Bypass -File "$$(wslpath -w $(BUILD)/ghosts.ps1)"

# Cross builds the Windows binary from Linux, which is how the Win32
# drawing backend gets compiled without leaving this machine. The
# compiler comes from the mingw packages, and CROSS names its prefix.
CROSS ?= x86_64-w64-mingw32-gcc-posix
WIN_BUILD = build/win

# Every source except the ones that speak to Linux, with the Windows
# ones put in their place. SQLite compiles on its own terms, since a
# vendored amalgamation answers to nobody's warning flags.
WIN_SRC = $(filter-out src/lib/window/window_x11% src/detect/detect_linux.c \
                       src/mount/mount_linux.c \
                       src/mount/mount_road.c \
                       src/mount/mount_repair.c \
                       src/lib/db/sqlite3.c, $(filter %.c,$(SRC))) \
          src/lib/window/window_win32.c src/lib/window/window_win32_draw.c \
          src/detect/detect_win32.c src/mount/mount_win32.c

.PHONY: windows
windows:
	@command -v $(CROSS) >/dev/null 2>&1 || { \
		echo "no Windows compiler. Install gcc-mingw-w64-x86-64, or set"; \
		echo "CROSS to one that is on the path."; exit 1; }
	@mkdir -p $(WIN_BUILD)
	@mkdir -p $(WIN_BUILD)
	@echo "  cross  sqlite3.c"
	@$(CROSS) -std=c11 -w -O2 $(SQLITE_DEFS) -Isrc/lib/db \
		-c src/lib/db/sqlite3.c -o $(WIN_BUILD)/sqlite3.o
	@echo "  cross  gravestone.exe"
	$(CROSS) -std=c11 -Wall -Wextra -Werror -Wshadow -Wconversion \
		-Wpointer-arith -Wstrict-prototypes -Wmissing-prototypes -O2 \
		-D_WIN32_WINNT=0x0601 $(INCLUDE) \
		-o $(WIN_BUILD)/gravestone.exe \
		$(WIN_SRC) $(WIN_BUILD)/sqlite3.o \
		-static -lgdi32 -luser32 -lcomdlg32 -lws2_32
	@echo "  cross  window_win32_test.exe"
	$(CROSS) -std=c11 -Wall -Wextra -Werror -Wshadow -Wconversion \
		-Wpointer-arith -Wstrict-prototypes -Wmissing-prototypes -O2 \
		-D_WIN32_WINNT=0x0601 $(INCLUDE) \
		-o $(WIN_BUILD)/window_win32_test.exe \
		src/lib/window/window_win32_test.c \
		src/lib/window/window_win32.c src/lib/window/window_win32_draw.c \
		src/core/log.c \
		-static -lgdi32 -luser32
	@echo "built $(WIN_BUILD)/gravestone.exe and $(WIN_BUILD)/window_win32_test.exe"

# Regenerates the editor's include list from the one the compiler uses, so
# adding a module never leaves VS Code underlining a header that builds.
.PHONY: vscode
vscode:
	@mkdir -p .vscode
	@python3 -c 'import json,sys; \
	paths=[p[2:] for p in sys.argv[1].split() if p.startswith("-I")]; \
	json.dump({"version":4,"configurations":[{"name":"gravestone",\
	"compilerPath":"/usr/bin/gcc","cStandard":"c11",\
	"intelliSenseMode":"linux-gcc-x64",\
	"includePath":["$${workspaceFolder}/"+p for p in paths],\
	"defines":["_POSIX_C_SOURCE=200809L"]}]},\
	open(".vscode/c_cpp_properties.json","w"),indent=2)' "$(INCLUDE)"
	@echo "editor include paths refreshed from the Makefile"
	@python3 -c 'import json; [print("  "+p) for p in json.load(open(".vscode/c_cpp_properties.json"))["configurations"][0]["includePath"]]'

-include $(DEP) $(TEST_DEP)

# Address and undefined behaviour sanitisers. Slower, and they catch memory
# faults that a clean compile and passing tests both miss.
# Sanitised objects go to their own directory, since linking one against a
# plain build fails with missing runtime symbols.
.PHONY: sanitise
sanitise:
	$(MAKE) test BUILD=build/asan \
	             OPT="-O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer" \
	             LDFLAGS="-fsanitize=address,undefined"
