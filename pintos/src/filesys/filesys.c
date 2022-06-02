#include "filesys/filesys.h"
#include <debug.h>
#include <stdio.h>
#include <string.h>
#include "filesys/file.h"
#include "filesys/free-map.h"
#include "filesys/inode.h"
#include "filesys/directory.h"
#include "filesys/buffer_cache.h"
#include "threads/thread.h"

/* Partition that contains the file system. */
struct block *fs_device;

static void do_format (void);

/* Initializes the file system module.
   If FORMAT is true, reformats the file system. */
void
filesys_init (bool format) 
{
  fs_device = block_get_role (BLOCK_FILESYS);
  if (fs_device == NULL)
    PANIC ("No file system device found, can't initialize file system.");
  
  // Buffer cache initialization
  bc_init ();

  inode_init ();
  free_map_init ();

  if (format) 
    do_format ();
  free_map_open ();

  // Set root directory
  thread_current()->cur_dir = dir_open_root();
}

/* Shuts down the file system module, writing any unwritten data
   to disk. */
void
filesys_done (void) 
{
  // Terminate Buffer Cache
  bc_term ();
  free_map_close ();
}

/* Creates a file named NAME with the given INITIAL_SIZE.
   Returns true if successful, false otherwise.
   Fails if a file named NAME already exists,
   or if internal memory allocation fails. */
bool
filesys_create (const char *name, off_t initial_size) 
{
  block_sector_t inode_sector = 0;
  char file_name[256+1];
  struct dir *dir = parse_path(name, file_name);
  bool success = (dir != NULL
                  && free_map_allocate (1, &inode_sector)
                  && inode_create (inode_sector, initial_size, 0)
                  && dir_add (dir, file_name, inode_sector));
  if (!success && inode_sector != 0) 
    free_map_release (inode_sector, 1);
  dir_close (dir);

  return success;
}

/* Opens the file with the given NAME.
   Returns the new file if successful or a null pointer
   otherwise.
   Fails if no file named NAME exists,
   or if an internal memory allocation fails. */
struct file *
filesys_open (const char *name)
{
  char file_name[256+1];
  struct dir *dir = parse_path(name, file_name);
  if(dir == NULL) {
    return NULL;
  }
  struct inode *inode = NULL;

  if (dir != NULL)
    dir_lookup (dir, file_name, &inode);
  dir_close (dir);

  return file_open (inode);
}

/* Deletes the file named NAME.
   Returns true if successful, false on failure.
   Fails if no file named NAME exists,
   or if an internal memory allocation fails. */
bool
filesys_remove (const char *name) 
{
  char file_name[256+1];
  struct dir *dir = parse_path(name, file_name);

  // Get inode
  struct inode *inode;
  dir_lookup(dir, name, &inode);

  // Temp variables
  struct dir *cur_dir = NULL;
  char temp[256+1];

  bool success = false;
  // If inode is file, remove it. If inode is directory remove it if no files. 
  if((inode_is_dir(inode) == false) || (cur_dir = dir_open(inode) && !dir_readdir(cur_dir, temp))) {
    success = dir != NULL && dir_remove (dir, name);
  }
  dir_close (dir);

  if(cur_dir != NULL) {
    dir_close(cur_dir);
  }

  return success;
}

/* Formats the file system. */
static void
do_format (void)
{
  printf ("Formatting file system...");
  free_map_create ();
  if (!dir_create (ROOT_DIR_SECTOR, 16))
    PANIC ("root directory creation failed");

  struct dir *root = dir_open_root();
  dir_add(root, ".", ROOT_DIR_SECTOR);
  dir_add(root, "..", ROOT_DIR_SECTOR);
  dir_close(root);

  free_map_close ();
  printf ("done.\n");
}

struct dir *parse_path (char *path_name, char *file_name) {
  struct dir *dir = NULL;
  struct thread *cur = thread_current();
  char path[256 + 1];
  if (path_name == NULL || file_name == NULL) {
    return NULL;
  }
  if (strlen(path_name) == 0) {
    return NULL;
  }

  // Copy path name
  strlcpy (path, path_name, 256);

  // Root path
  if(path[0] == '/') {
    dir = dir_open_root();
  }
  else {
    dir = dir_reopen(cur->cur_dir);
  }

  // Check inode
  if(inode_is_dir(dir_get_inode(dir)) == false) {
    return NULL;
  }

  char *token, *nextToken, *savePtr;
  token = strtok_r(path, "/", &savePtr);
  nextToken = strtok_r(NULL, "/", &savePtr);

  // if token points nothing
  if (token == NULL) {
    strlcpy(file_name, ".", 256);
    return dir;
  }

  while (token != NULL && nextToken != NULL)
  {
    // Look up the directory & store info in the inode
    struct inode *inode;
    if (dir_lookup(dir, token, &inode) == false) {
      dir_close(dir);
      return NULL;
    }

    // If it is file return NULL
    if (inode_is_dir(inode) == false) {
      dir_close(dir);
      return NULL;
    }

    // Close dir & store inode info to dir
    dir_close(dir);
    dir = dir_open(inode);

    token = nextToken;
    nextToken = strtok_r(NULL, "/", &savePtr);
  }
  // Store token to the file_name
  strlcpy (file_name, token, 256);
  //printf("%s: file_name",file_name);
  return dir;  
}

bool filesys_create_dir (const char *name) {
  block_sector_t sector_idx = 0;
  char file_name[256+1];
  struct dir *dir = parse_path (name, file_name);

  bool success = (dir != NULL
                && free_map_allocate (1, &sector_idx)
                && dir_create (sector_idx, 16)
                && dir_add (dir, file_name, sector_idx));
  // If failed, free free_map
  if(success == false && sector_idx != 0) {
    free_map_release (sector_idx, 1);
  }

  // Add ".",".." Directory
  if(success == true) {
    struct dir *new_dir = dir_open(inode_open(sector_idx));
    // . points current directory sector
    dir_add (new_dir, ".", sector_idx);
    // .. points parent directory sector
    dir_add (new_dir, "..", inode_get_inumber(dir_get_inode(dir)));
    dir_close(new_dir);
  }
  dir_close(dir);
  return success;
}