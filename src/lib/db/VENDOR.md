# Vendored SQLite

`sqlite3.c` and `sqlite3.h` are the official amalgamation, copied here so the
project builds with nothing installed on the machine.

```
version   3.45.3
source    https://sqlite.org/2024/sqlite-amalgamation-3450300.zip
fetched   2026-09-01
```

Checksums, to verify the copy in this directory:

```
sha256  ea170e73e447703e8359308ca2e4366a3ae0c4304a8665896f068c736781c651  the zip
sha256  9ca336fbcbff9f1d78b4f45b6a19583fcc097192310dd2f5f6cd43b9a33d7d69  sqlite3.c
sha256  882ad3c0448d0324fb3a6b1a85333a9173d539ac669c9972ae1f03722ff86282  sqlite3.h
```

Nothing outside this directory includes `sqlite3.h`. Everything goes through
`db.h`, so replacing the storage engine touches this directory alone.

The amalgamation is compiled with its own warning flags in the `Makefile`,
because it does not pass the settings the rest of the project uses.

Measured on this machine, gcc 14.2.0 at `-O2`:

```
sqlite3.c on disk          9,027,389 bytes
compile time                     ~25 seconds, once
machine code produced        963 KB
added to the stripped binary 1,029 KB
```

`-lpthread` and `-lm` sit on the link line. Neither is needed on this
glibc, where both were folded into the C library, and both are kept for
systems where they were not.
