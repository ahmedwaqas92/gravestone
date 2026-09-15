/* mount.h
 *
 * The Windows drives this machine had attached last time, put back when
 * the program starts.
 *
 * WSL is Windows running a Linux system inside itself. A Windows drive
 * reaches that Linux side through a filesystem called drvfs, which is
 * asked for by hand with `mount -t drvfs D: /mnt/d`. Windows drops those
 * mounts whenever the drive is unplugged, whenever the machine sleeps
 * badly, and whenever the Linux side is restarted, and the row stays in
 * the kernel mount table afterwards while every read of it fails. The
 * drive letter is written down on a run that worked, so the next run
 * knows which one to put back without asking anybody.
 *
 * Nothing here ever asks for a password. Putting a drive back needs
 * administrator rights, and the only form of sudo used is the one that
 * refuses rather than prompting, so a machine that would want a password
 * gets a warning naming the command to run by hand. A drive that has
 * left the machine gets a warning as well. Neither one stops the program.
 */
#ifndef GS_MOUNT_H
#define GS_MOUNT_H

#include <stddef.h>

#include "gravestone.h"

/* A mount point is always /mnt/ plus one lowercase letter, so seven
 * bytes hold it with room for the terminator. The field is wider than
 * that on purpose, since a reading taken off the machine may name
 * something longer and the value is still worth showing. */
#define GS_MOUNT_POINT_MAX  128
#define GS_MOUNT_MAX_DRIVES  26

/* How many runs in a row may fail to find a drive before it is dropped
 * from the list. A drive deliberately unplugged should stop costing a
 * mount attempt at every startup. */
#define GS_MOUNT_MISS_LIMIT 5

typedef struct {
    char      letter;                     /* 'D', one capital A to Z */
    char      point[GS_MOUNT_POINT_MAX];  /* "/mnt/d" */
    char      distro[72];                 /* "Debian", for the record */
    long long seen_at;                    /* seconds, last time it worked */
    int       misses;                     /* runs in a row it was missing */
} gs_mount_drive_t;

/* One line of the system table of filesystems that names a whole
 * Windows drive. */
typedef struct {
    char letter;                        /* 'D' */
    char point[GS_MOUNT_POINT_MAX];     /* "/mnt/d" */
    char options[512];                  /* "defaults,noauto,user,..." */
    int  mountable_by_user;             /* the word user is among them */
} gs_mount_fstab_t;

/* Where a drive stands right now, read from the kernel mount table and
 * from asking the filesystem for its size. */
typedef enum {
    GS_MOUNT_ABSENT = 0, /* the mount table holds no row for that point */
    GS_MOUNT_STALE,      /* a row is there and every read of it fails */
    GS_MOUNT_HEALTHY,    /* a row is there and it answers */
    GS_MOUNT_FOREIGN     /* something is mounted there that is not a drive */
} gs_mount_state_t;

/* What an attempt to put a drive back ended up doing. The ones that
 * changed nothing and the ones that changed something are kept apart,
 * because only a drive taken down without being replaced leaves the
 * machine worse off than it was. */
typedef enum {
    GS_MOUNT_OK_ALREADY = 0, /* it was already there and working */
    GS_MOUNT_OK_REMOUNTED,   /* the dead one came down and a live one went up */
    GS_MOUNT_SKIPPED,        /* this machine is not one where drvfs applies */
    GS_MOUNT_BUSY,           /* a program is holding the dead mount open */
    GS_MOUNT_NO_RIGHTS,      /* sudo would have asked for a password */
    GS_MOUNT_GONE,           /* the drive is not in the machine any more */
    GS_MOUNT_DETACHED,       /* the dead one came down and nothing replaced it */
    GS_MOUNT_TIMED_OUT,      /* a command was still running at the deadline */
    GS_MOUNT_FAILED          /* it refused and nothing changed */
} gs_mount_outcome_t;

/* The database this module keeps its own table in. It is the same file
 * the rest of the program uses, opened through the database wrapper
 * rather than through another module, since a wrapper is the only thing
 * allowed to speak to the engine. */
/* Every whole Windows drive the system table names. This is what the
 * mounting works from, since a line there carrying the word user is both
 * the record of which drive belongs where and the right to mount it. */
int gs_mount_fstab_read(gs_mount_fstab_t *out, int max);

/* One letter out of that table. Returns 1 when it was found. */
int gs_mount_fstab_find(char letter, gs_mount_fstab_t *found);

/* Whether the system table already lets an ordinary person mount that
 * drive, which is the whole question the setting up answers. */
int gs_mount_fstab_grants(char letter);

/* Writes the line the system table needs so that mounting never asks for
 * anything again. Built from the letter alone, with the numbers naming
 * whoever is running. */
int gs_mount_fstab_line(char letter, const char *point, long uid, long gid,
                        char *out, size_t cap);

/* Puts that line into the system table, which needs administrator rights
 * this once. password is written straight to sudo down a pipe, is copied
 * nowhere, and never reaches a log or an argument list. The caller wipes
 * its own copy afterwards.
 *
 * why receives a sentence for the person, carrying nothing that was
 * typed. Returns GS_OK when the table now grants the right. */
int gs_mount_fstab_grant(char letter, const char *password,
                         size_t password_len, char *why, size_t cap);

int  gs_mount_open(void);
void gs_mount_close(void);

/* Non zero on a Linux running inside Windows. Two signals have to agree,
 * the kernel naming Microsoft and a Windows drive being present, because
 * a wrong answer here points the unmount command at a real disk on a
 * plain Linux machine. */
int gs_mount_under_wsl(void);

/* Which distribution this is, for the record kept against a drive. */
int gs_mount_distro(char *out, size_t cap);

/* True for exactly one capital letter A to Z. Every command this module
 * runs is built from a letter that passed this, and from nothing else. */
int gs_mount_letter_ok(char letter);

/* Writes "/mnt/d" for 'D'. The point is worked out from the letter every
 * time rather than read back from the database, so a row somebody edited
 * cannot name a path of its own choosing. */
int gs_mount_point_for(char letter, char *out, size_t cap);

/* True for a path this module refuses to touch whatever the database
 * says. The system drive carries the Windows programs the file box and
 * the window sweep both run, and /mnt/wslg carries the sockets the
 * screen is drawn through, so unmounting either breaks the program that
 * is doing the unmounting. */
int gs_mount_point_refused(const char *point);

/* How this machine reaches the administrator with nothing asked. */
typedef enum {
    GS_MOUNT_ROAD_NONE = 0,  /* no road, every command would want a password */
    GS_MOUNT_ROAD_SELF,      /* already running as the administrator */
    GS_MOUNT_ROAD_SUDO,      /* sudo lets this user through without asking */
    GS_MOUNT_ROAD_WSL        /* wsl.exe starts commands as root in this Linux */
} gs_mount_road_t;

/* Finds the road once and remembers it. helper_out receives the program
 * that carries the command down that road, NULL when the road needs
 * none. Never prompts for anything. */
gs_mount_road_t gs_mount_root_road(const char **helper_out);

/* Non zero when a road exists, so a drive can be put back with nothing
 * typed. */
int gs_mount_root_free(void);

/* Non zero once the road is known one way or the other. A probe that
 * timed out decides nothing and is tried again on the next call, and
 * until it answers nothing should act on there being no road. */
int gs_mount_root_settled(void);

/* The command a person would type to do this by hand, built from the
 * letter alone so the text carries nothing that was stored. */
int gs_mount_hand_command(char letter, char *out, size_t cap);

/* Every Windows drive the kernel mount table holds right now. Returns
 * how many were written, or a negative error. */
int gs_mount_read(gs_mount_drive_t *out, int max);

/* Where one letter stands. found receives the reading when it is not
 * NULL, which saves the caller a second walk of the table. */
gs_mount_state_t gs_mount_state(char letter, gs_mount_drive_t *found);

/* Writes a drive down, or updates the row already there. Only a drive
 * proven to answer should ever reach this. */
int gs_mount_remember(const gs_mount_drive_t *drive);

/* Every drive written down, oldest miss count first. */
int gs_mount_recall(gs_mount_drive_t *out, int max);

/* Remembers that the person said no to setting this drive up, so the
 * box is not put in front of them again at every start. */
int gs_mount_decline(char letter);
int gs_mount_declined(char letter);

/* Drops one letter from the list. */
int gs_mount_forget(char letter);

/* Records every drive that is mounted and answering right now, and
 * counts a miss against every remembered drive that is not. A drive
 * missing for GS_MOUNT_MISS_LIMIT runs in a row is dropped. Returns how
 * many drives were written down. */
int gs_mount_keep_reading(void);

/* Puts one drive back. why receives a sentence for the person, safe to
 * print, built only from values this module worked out itself. */
gs_mount_outcome_t gs_mount_restore(char letter, char *why, size_t cap);

/* The first drive Windows has that this machine cannot mount without a
 * password, or nought when there is none. That is the one drive worth
 * asking about, and asking is the only way past it. */
char gs_mount_wants_setup(void);

/* Puts every remembered drive back. note receives a short line for the
 * status bar, empty when there is nothing worth saying. Returns how many
 * drives were put back. */
int gs_mount_restore_all(char *note, size_t cap);

const char *gs_mount_state_name(gs_mount_state_t state);
const char *gs_mount_outcome_name(gs_mount_outcome_t outcome);

/* True for an outcome the person should be told about. Everything that
 * worked is silent. */
int gs_mount_outcome_is_warning(gs_mount_outcome_t outcome);

extern const gs_module gs_mount_module;

#endif /* GS_MOUNT_H */
