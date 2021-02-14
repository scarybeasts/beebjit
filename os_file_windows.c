#include "os_file.h"
#include <windows.h>


char*
os_file_get_executable_path(void) {
  char* executable_path = util_malloc(MAX_PATH);
  if (GetModuleFileNameA(NULL, executable_path, MAX_PATH) < MAX_PATH) {
    char* path_leaf = strrchr(executable_path, '\\');
    if (path_leaf) {
      *path_leaf = 0;
    } else {
      *executable_path = 0;
	}
  } else {
    util_bail("Could not get Win32 executable path");
  }

  return executable_path;
}

char
os_file_get_separator_char(void) {
	return '\\';
}
