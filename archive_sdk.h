#pragma once

#if __has_include(<archive.h>) && __has_include(<archive_entry.h>)
#include <archive.h>
#include <archive_entry.h>
#else
// Recent macOS SDKs ship the system libarchive without its public headers.
// Only the public ZIP/file-descriptor API used by AuraPet is declared here.
#include <cstddef>
#include <cstdint>
#include <sys/types.h>

inline constexpr int ARCHIVE_OK = 0;
inline constexpr int ARCHIVE_EOF = 1;
inline constexpr unsigned int AE_IFREG = 0100000;
extern "C" {
struct archive;
struct archive_entry;
archive* archive_read_new();
int archive_read_support_format_zip_seekable(archive*);
int archive_read_open_fd(archive*, int, size_t);
int archive_read_next_header(archive*, archive_entry**);
ssize_t archive_read_data(archive*, void*, size_t);
int archive_read_free(archive*);
archive* archive_write_new();
int archive_write_set_format_zip(archive*);
int archive_write_open_fd(archive*, int);
int archive_write_header(archive*, archive_entry*);
ssize_t archive_write_data(archive*, const void*, size_t);
int archive_write_close(archive*);
int archive_write_free(archive*);
const char* archive_error_string(archive*);
archive_entry* archive_entry_new();
void archive_entry_free(archive_entry*);
const char* archive_entry_pathname(archive_entry*);
const char* archive_entry_hardlink(archive_entry*);
mode_t archive_entry_filetype(archive_entry*);
int archive_entry_size_is_set(archive_entry*);
int64_t archive_entry_size(archive_entry*);
void archive_entry_set_pathname(archive_entry*, const char*);
void archive_entry_set_filetype(archive_entry*, unsigned int);
void archive_entry_set_perm(archive_entry*, mode_t);
void archive_entry_set_size(archive_entry*, int64_t);
}
#endif
