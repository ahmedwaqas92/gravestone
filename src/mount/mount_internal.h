/* mount_internal.h
 *
 * Shared between the portable half of this module, the platform half,
 * and the test. Nothing outside src/mount/ may include this.
 *
 * The parsing and the checking live here rather than behind the public
 * header so the test can work every one of them without a single command
 * ever running. A test that unmounted a drive would run on the machine
 * of whoever typed `make check`.
 */
#ifndef GS_MOUNT_INTERNAL_H
#define GS_MOUNT_INTERNAL_H

#include <stddef.h>

#include "db.h"
#include "mount.h"

/* The system table of filesystems, and the options written against a
 * drive in it. user hands an ordinary person the right to mount that one
 * line, and turns on noexec, nosuid and nodev by itself, so nothing can
 * be run from the drive and no file on it can gain rights. noauto keeps
 * the system from trying at boot.
 *
 * None of this helps under WSL. The drvfs mounting there is done by a
 * helper of WSL's own that answers only to root, so user grants nothing,
 * and noauto stops WSL putting the drive back at boot, which is worse
 * than nothing. The table is written only where no road to root exists,
 * and under WSL one nearly always does. See mount_road.c. */
#define GS_MOUNT_FSTAB        "/etc/fstab"
#define GS_MOUNT_FSTAB_BACKUP ".gravestone.bak"
#define GS_MOUNT_FSTAB_BASE  "defaults"
#define GS_MOUNT_FSTAB_RIGHT ",noauto,user"

/* How long any one command may take. A Windows drive that has gone to
 * sleep answers slowly, a drive that has gone away does not answer at
 * all, and the first command down the wsl.exe road starts a session. */
#define GS_MOUNT_SECONDS 20

/* Reads one line of the system table. Returns 1 when it names a whole
 * Windows drive. A comment and a shared folder both give 0. */
int gs_mount_fstab_parse(const char *line, gs_mount_fstab_t *out);

/* True when that word stands on its own among the comma separated
 * options. Looking for the letters anywhere would find user inside
 * nouser, which means the opposite. */
int gs_mount_option_present(const char *options, const char *want);

/* Builds what the whole system table should say once the drive has its
 * line, replacing a line already naming that drive rather than adding a
 * second one. */
int gs_mount_fstab_rewrite(char letter, const char *point, long uid, long gid,
                           char *out, size_t cap, size_t *len_out);

/* The one handle every stored value goes through. NULL while closed. */
extern gs_db_t *gs_mount_db;

/* Seconds since the epoch, which is what every stored time holds. */
long long gs_mount_now(void);

/* The kernel writes a space, a tab, a newline and a backslash in its
 * mount table as a backslash followed by three digits in base eight, so
 * the Windows drive C: arrives written as C:\134. Turning those back is
 * what makes the first character the drive letter. Works in place and
 * never grows the text. */
void gs_mount_unescape(char *text);

/* Reads one line of the kernel mount table. Returns 1 when the line
 * describes a whole Windows drive, and 0 for everything else.
 *
 * Two things have to agree before a line counts. The options have to
 * carry aname=drvfs, which is the name Windows gives the share, and the
 * device has to be one letter followed by a colon once the escaping is
 * undone. The filesystem type alone decides nothing, because WSL reports
 * 9p there and 9p is an ordinary Linux filesystem carrying real shares
 * on other machines. A recogniser keyed on the type would aim the
 * unmount command at one of those.
 *
 * The mount point is taken from the line as written, which is what makes
 * a drive mounted somewhere unusual visible. Nothing built from it ever
 * reaches a command. */
int gs_mount_parse_line(const char *line, gs_mount_drive_t *out);

/* True when a drive of that letter could sit behind that point, meaning
 * the point is exactly the one the letter works out to. A row naming
 * anything else is refused rather than repaired, since the only thing
 * that could have written it is somebody editing the database. */
int gs_mount_pair_ok(char letter, const char *point);

/* Copies text into out, dropping anything below a space and anything a
 * shell reads as an instruction. A warning telling a person which
 * command to run lands in a terminal, and a terminal does read those
 * characters, so the warning must not become the payload. */
void gs_mount_plain(const char *text, char *out, size_t cap);

/* The platform half. Everything below is written once per platform and
 * does nothing at all where drvfs has no meaning. */

/* The first of those paths that names a real program, or NULL. A tool
 * moves about between distributions, so each place it might live is
 * tried and nothing is ever looked up on the search path. */
const char *gs_mount_first_real(const char *const *paths, int count);

/* Non zero when the kernel says it is the one Windows ships. */
int gs_mount_kernel_is_windows(void);

/* Fills out from the kernel mount table. */
int gs_mount_table_read(gs_mount_drive_t *out, int max);

/* Non zero when something is really mounted at that path. An ordinary
 * empty directory is told apart from a mount by its device number, since
 * every mounted filesystem carries its own and an ordinary directory
 * shares the one belonging to the filesystem above it. */
int gs_mount_is_mounted(const char *point);

/* Reads the drive letters out of what the Windows tool printed. Kept
 * apart from the running of that tool so the reading can be checked
 * against real answers without Windows being present. */
int gs_mount_read_drive_list(const char *text, char *out, size_t cap);

/* The drive letters Windows has attached right now, written as capitals
 * with no separator, so "CD" means C: and D:. A drive plugged in after
 * this Linux started is absent from the kernel mount table, and asking
 * Windows is the only way to learn it is there. */
int gs_mount_windows_letters(char *out, size_t cap);

/* Non zero when the filesystem at that point answers a question about
 * its size. A mount left behind by a drive that went away stays in the
 * table and fails this. */
int gs_mount_answers(const char *point);

/* Non zero when the path exists and is a directory. exists_out receives
 * whether anything is there at all. */
int gs_mount_dir_at(const char *point, int *exists_out);

/* Non zero when the directory holds anything at all. A point holding
 * real files is never mounted over, because mounting hides them while
 * their bytes go on filling the disk underneath. */
int gs_mount_dir_has_content(const char *point);

/* How many running programs are holding that point open. A dead mount
 * somebody is still reading is left alone, since taking it down under a
 * program that has a file mapped into its memory kills that program. */
int gs_mount_holders(const char *point, char *first, size_t cap);

/* Builds the argument list that carries program and tail down a road,
 * ending in NULL. Kept apart from the running so the list can be checked
 * without anything being run. Returns how many entries were written
 * before the NULL, or a negative error when the list will not fit, the
 * program is not an absolute path, or the road wants a helper or a
 * distribution name it was not given. */
int gs_mount_root_argv(gs_mount_road_t road, const char *helper,
                       const char *distro, const char *program,
                       const char *const *tail, int tail_len,
                       const char **argv, int cap);

/* True when one line of what a program printed is exactly a nought. The
 * whole buffer up to cap is read, with nought bytes stepped over, since
 * wsl.exe writes its warnings two bytes a character down the same pipe
 * and a walk that stopped at the first nought byte would never reach
 * the answer. */
int gs_mount_line_is_nought(const char *text, size_t cap);

/* Runs one command as the administrator down whichever road this machine
 * has. Every argument arrives as its own string and nothing parses any
 * of them. Returns GS_ERR when there is no road, and otherwise what
 * gs_proc_run returned, with code holding the exit status. */
int gs_mount_as_root(const char *program, const char *const *tail,
                     int tail_len, char *out, size_t cap, int *code);

/* Runs the repair. Every argument is worked out from the letter by the
 * caller and nothing stored reaches this. */
gs_mount_outcome_t gs_mount_repair(char letter, const char *point,
                                   gs_mount_state_t state,
                                   char *why, size_t cap);

/* Takes the lock that stops two copies of the program repairing the same
 * machine at once. Returns GS_OK when the lock is held. */
int  gs_mount_lock(void);
void gs_mount_unlock(void);

#endif /* GS_MOUNT_INTERNAL_H */
