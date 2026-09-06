# gravestone
#
# One line per source file. C pulls nothing in by itself, so a file absent
# from this list does not exist as far as the program is concerned.

CC      ?= gcc
CSTD     = -std=c11
WARN     = -Wall -Wextra -Werror -Wshadow -Wconversion -Wpointer-arith \
           -Wstrict-prototypes -Wmissing-prototypes
OPT     ?= -O2 -g
DEFS     = -D_POSIX_C_SOURCE=200809L
INCLUDE  = -Iinclude -Isrc/core -Isrc/lib/window -Isrc/lib/db \
           -Isrc/lib/paths -Isrc/lib/proc -Isrc/lib/http -Isrc/catalogue -Isrc/library -Isrc/detect -Isrc/store -Isrc/ui
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
SRC += src/detect/detect.c
SRC += src/catalogue/catalogue.c
SRC += src/library/library.c
SRC += src/store/store.c
SRC += src/store/schema.c
SRC += src/lib/window/window_x11.c
SRC += src/lib/window/window_x11_conn.c
SRC += src/lib/window/window_x11_draw.c
SRC += src/lib/db/db.c
SRC += src/lib/db/sqlite3.c
SRC += src/ui/ui.c
SRC += src/ui/ui_paint.c
SRC += src/ui/ui_panes.c
SRC += src/ui/ui_models.c
SRC += src/ui/ui_models_draw.c
SRC += src/ui/ui_confirm.c
SRC += src/ui/ui_actions.c
SRC += src/ui/ui_specs.c

OBJ  = $(SRC:%.c=$(BUILD)/%.o)
DEP  = $(OBJ:.o=.d)

# ---- test binaries, one line each ----
TEST_BIN  = $(BUILD)/window_test
TEST_BIN += $(BUILD)/ui_test
TEST_BIN += $(BUILD)/ui_paint_test
TEST_BIN += $(BUILD)/ui_panes_test
TEST_BIN += $(BUILD)/db_test
TEST_BIN += $(BUILD)/paths_test
TEST_BIN += $(BUILD)/proc_test
TEST_BIN += $(BUILD)/http_test
TEST_BIN += $(BUILD)/detect_test
TEST_BIN += $(BUILD)/store_test
TEST_BIN += $(BUILD)/catalogue_test
TEST_BIN += $(BUILD)/library_test

.PHONY: all clean test run

all: $(BIN)

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

$(BUILD)/window_test: src/lib/window/window_test.c $(BUILD)/src/lib/window/window_x11.o $(BUILD)/src/lib/window/window_x11_conn.o $(BUILD)/src/lib/window/window_x11_draw.o $(BUILD)/src/core/log.o
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $^ $(LDLIBS)

$(BUILD)/proc_test: src/lib/proc/proc_test.c $(BUILD)/src/lib/proc/proc.o $(BUILD)/src/core/log.o $(BUILD)/src/core/hash.o $(BUILD)/src/core/str.o
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $^ $(LDLIBS)

$(BUILD)/paths_test: src/lib/paths/paths_test.c $(BUILD)/src/lib/paths/paths.o $(BUILD)/src/core/log.o $(BUILD)/src/core/hash.o $(BUILD)/src/core/str.o
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $^ $(LDLIBS)

$(BUILD)/detect_test: src/detect/detect_test.c $(BUILD)/src/detect/detect.o $(BUILD)/src/lib/paths/paths.o $(BUILD)/src/lib/proc/proc.o $(BUILD)/src/core/log.o $(BUILD)/src/core/hash.o $(BUILD)/src/core/str.o
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $^ $(LDLIBS)

$(BUILD)/store_test: src/store/store_test.c $(BUILD)/src/store/store.o $(BUILD)/src/store/schema.o $(BUILD)/src/detect/detect.o $(BUILD)/src/lib/paths/paths.o $(BUILD)/src/lib/proc/proc.o $(BUILD)/src/lib/db/db.o $(BUILD)/src/lib/db/sqlite3.o $(BUILD)/src/core/log.o $(BUILD)/src/core/hash.o $(BUILD)/src/core/str.o
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $^ $(LDLIBS)

$(BUILD)/db_test: src/lib/db/db_test.c $(BUILD)/src/lib/db/db.o $(BUILD)/src/lib/db/sqlite3.o $(BUILD)/src/core/log.o $(BUILD)/src/core/hash.o $(BUILD)/src/core/str.o
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $^ $(LDLIBS)

$(BUILD)/ui_test: src/ui/ui_test.c $(BUILD)/src/ui/ui.o $(BUILD)/src/ui/ui_paint.o $(BUILD)/src/ui/ui_panes.o $(BUILD)/src/ui/ui_models.o $(BUILD)/src/ui/ui_models_draw.o $(BUILD)/src/ui/ui_confirm.o $(BUILD)/src/ui/ui_actions.o $(BUILD)/src/ui/ui_specs.o $(BUILD)/src/lib/window/window_x11.o $(BUILD)/src/lib/window/window_x11_conn.o $(BUILD)/src/lib/window/window_x11_draw.o $(BUILD)/src/store/store.o $(BUILD)/src/store/schema.o $(BUILD)/src/detect/detect.o $(BUILD)/src/catalogue/catalogue.o $(BUILD)/src/library/library.o $(BUILD)/src/lib/http/http.o $(BUILD)/src/lib/paths/paths.o $(BUILD)/src/lib/proc/proc.o $(BUILD)/src/lib/db/db.o $(BUILD)/src/lib/db/sqlite3.o $(BUILD)/src/core/log.o $(BUILD)/src/core/hash.o $(BUILD)/src/core/str.o
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $^ $(LDLIBS)

$(BUILD)/ui_paint_test: src/ui/ui_paint_test.c $(BUILD)/src/ui/ui.o $(BUILD)/src/ui/ui_paint.o $(BUILD)/src/ui/ui_panes.o $(BUILD)/src/ui/ui_models.o $(BUILD)/src/ui/ui_models_draw.o $(BUILD)/src/ui/ui_confirm.o $(BUILD)/src/ui/ui_actions.o $(BUILD)/src/ui/ui_specs.o $(BUILD)/src/lib/window/window_x11.o $(BUILD)/src/lib/window/window_x11_conn.o $(BUILD)/src/lib/window/window_x11_draw.o $(BUILD)/src/store/store.o $(BUILD)/src/store/schema.o $(BUILD)/src/detect/detect.o $(BUILD)/src/catalogue/catalogue.o $(BUILD)/src/library/library.o $(BUILD)/src/lib/http/http.o $(BUILD)/src/lib/paths/paths.o $(BUILD)/src/lib/proc/proc.o $(BUILD)/src/lib/db/db.o $(BUILD)/src/lib/db/sqlite3.o $(BUILD)/src/core/log.o $(BUILD)/src/core/hash.o $(BUILD)/src/core/str.o
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $^ $(LDLIBS)

$(BUILD)/ui_panes_test: src/ui/ui_panes_test.c $(BUILD)/src/ui/ui.o $(BUILD)/src/ui/ui_paint.o $(BUILD)/src/ui/ui_panes.o $(BUILD)/src/ui/ui_models.o $(BUILD)/src/ui/ui_models_draw.o $(BUILD)/src/ui/ui_confirm.o $(BUILD)/src/ui/ui_actions.o $(BUILD)/src/ui/ui_specs.o $(BUILD)/src/lib/window/window_x11.o $(BUILD)/src/lib/window/window_x11_conn.o $(BUILD)/src/lib/window/window_x11_draw.o $(BUILD)/src/store/store.o $(BUILD)/src/store/schema.o $(BUILD)/src/detect/detect.o $(BUILD)/src/catalogue/catalogue.o $(BUILD)/src/library/library.o $(BUILD)/src/lib/http/http.o $(BUILD)/src/lib/paths/paths.o $(BUILD)/src/lib/proc/proc.o $(BUILD)/src/lib/db/db.o $(BUILD)/src/lib/db/sqlite3.o $(BUILD)/src/core/log.o $(BUILD)/src/core/hash.o $(BUILD)/src/core/str.o
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $^ $(LDLIBS)

$(BUILD)/catalogue_test: src/catalogue/catalogue_test.c $(BUILD)/src/catalogue/catalogue.o $(BUILD)/src/detect/detect.o $(BUILD)/src/lib/paths/paths.o $(BUILD)/src/lib/proc/proc.o $(BUILD)/src/core/log.o $(BUILD)/src/core/hash.o $(BUILD)/src/core/str.o
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $^ $(LDLIBS)

$(BUILD)/library_test: src/library/library_test.c $(BUILD)/src/library/library.o $(BUILD)/src/lib/http/http.o $(BUILD)/src/lib/proc/proc.o $(BUILD)/src/core/log.o $(BUILD)/src/core/hash.o $(BUILD)/src/core/str.o
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $^ $(LDLIBS)

$(BUILD)/http_test: src/lib/http/http_test.c $(BUILD)/src/lib/http/http.o $(BUILD)/src/core/log.o
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $^ $(LDLIBS)

test: $(BIN) $(TEST_BIN)
	@for t in $(TEST_BIN); do echo "--- $$t"; $$t || exit 1; done
	@echo "all tests passed"
	@$(MAKE) --no-print-directory ghosts >/dev/null 2>&1 || true

run: $(BIN)
	$(BIN)

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

-include $(DEP)

# Address and undefined behaviour sanitisers. Slower, and they catch memory
# faults that a clean compile and passing tests both miss.
# Sanitised objects go to their own directory, since linking one against a
# plain build fails with missing runtime symbols.
.PHONY: sanitise
sanitise:
	$(MAKE) test BUILD=build/asan \
	             OPT="-O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer" \
	             LDFLAGS="-fsanitize=address,undefined"
