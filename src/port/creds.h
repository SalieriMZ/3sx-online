#ifndef PORT_CREDS_H
#define PORT_CREDS_H

#include <stdbool.h>
#include <stddef.h>

// Persist a plaintext password encrypted with the OS-native user-scoped
// secret store (Windows DPAPI). Returns false if encryption / write fails or
// the OS doesn't provide a backend (Linux/macOS = NYI). Encrypted blob lives
// at Paths_GetPrefPath()/cred; only readable by the same Windows user on the
// same machine.
bool Creds_SavePassword(const char* pw);

// Decrypt + copy into buf (NUL-terminated). Returns false if no saved cred,
// decryption fails, or buf too small. On any failure, buf[0] is set to '\0'.
bool Creds_LoadPassword(char* buf, size_t buf_size);

void Creds_ClearPassword(void);

#endif
