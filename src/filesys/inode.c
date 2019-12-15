#include "filesys/inode.h"
#include <list.h>
#include <debug.h>
#include <stdio.h>
#include <round.h>
#include <string.h>
#include "filesys/filesys.h"
#include "filesys/free-map.h"
#include "threads/malloc.h"
#include "filesys/cache.h"
#include "threads/synch.h"

/* Identifies an inode. */
#define INODE_MAGIC 0x494e4f44

#define NUM_DIRECT 10
#define NUM_INDIRECT 1
#define NUM_DOUBLE_INDIRECT 1
#define NUM_SECTORS 16522

/* On-disk inode.
   Must be exactly BLOCK_SECTOR_SIZE bytes long. */
struct inode_disk
  {
    volatile off_t length;                       /* File size in bytes. */
    block_sector_t direct[NUM_DIRECT];
    block_sector_t indirect[NUM_INDIRECT];
    block_sector_t double_indirect[NUM_DOUBLE_INDIRECT];

    bool isdir;                        /* Is directory? */
    int entry_cnt;                      /* Number of entries in directory */

    unsigned magic;                     /* Magic number. */
    uint32_t unused[112];               /* Not used. */
  };

struct indirect_block
  {
    block_sector_t indirect_blocks[128];
  };

/* Returns the number of sectors to allocate for an inode SIZE
   bytes long. */
static inline size_t
bytes_to_sectors (off_t size)
{
  return DIV_ROUND_UP (size, BLOCK_SECTOR_SIZE);
}

/* In-memory inode. */
struct inode
  {
    struct list_elem elem;              /* Element in inode list. */
    block_sector_t sector;              /* Sector number of disk location. */
    int open_cnt;                       /* Number of openers. */
    bool removed;                       /* True if deleted, false otherwise. */
    int deny_write_cnt;                 /* 0: writes ok, >0: deny writes. */
    struct lock extension_lock;
    //struct inode_disk data;             /* Inode content. */
  };


// Must free inode_disk after this function has been called
static struct inode_disk*
get_disk_inode (const struct inode *inode)
{
  struct inode_disk *disk_inode = NULL;

  disk_inode = calloc (1, sizeof *disk_inode);

  if (disk_inode != NULL)
  {
    cache_read_at (inode->sector, disk_inode, 0, BLOCK_SECTOR_SIZE);
    return disk_inode;
  }

  return NULL;
}

static block_sector_t
inode_block_to_sector (struct inode_disk *disk_inode, size_t block_idx, struct indirect_block *indirect_block,
    struct indirect_block *double_indirect_block, bool create)
{
  // printf ("block idx : %d\n", block_idx);

  block_sector_t sector;
  if (block_idx < NUM_DIRECT)
  {
    sector = disk_inode->direct[block_idx];
    if (sector == 0 && create)
    {
      if (!free_map_allocate (&disk_inode->direct[block_idx]))
        return 0;
      // printf ("direct sector created : %d\n", disk_inode->direct[block_idx]);

      return disk_inode->direct[block_idx];
    }
    // printf ("direct : %d\n", sector);
    return sector;
  }
  //indirect
  else if (NUM_DIRECT <= block_idx && block_idx < NUM_DIRECT + 128)
  {
    if (create && disk_inode->indirect[0] == 0)
    {
      if(!free_map_allocate (&disk_inode->indirect[0]))
        return 0;
      cache_write_at (disk_inode->indirect[0], indirect_block->indirect_blocks, 0, BLOCK_SECTOR_SIZE);
    }
    else if (disk_inode->indirect[0] == 0)
      return 0;
    cache_read_at (disk_inode->indirect[0], indirect_block->indirect_blocks, 0, BLOCK_SECTOR_SIZE);
    sector = indirect_block->indirect_blocks[block_idx - NUM_DIRECT];
    if (sector == 0 && create)
    {
      if (!free_map_allocate (&indirect_block->indirect_blocks[block_idx - NUM_DIRECT]))
        return 0;
      cache_write_at (disk_inode->indirect[0], indirect_block->indirect_blocks, 0, BLOCK_SECTOR_SIZE);

      // printf ("indirect sector created : %d\n", indirect_block->indirect_blocks[block_idx - NUM_DIRECT]);
      return indirect_block->indirect_blocks[block_idx - NUM_DIRECT];
    }
    // printf ("existing indirect : %d\n", sector);
    return sector;
  }
  //double indirect
  else if (NUM_DIRECT + 128 <= block_idx && block_idx < NUM_SECTORS)
  {
    if (create && disk_inode->double_indirect[0] == 0)
    {
      if (!free_map_allocate (&disk_inode->double_indirect[0]))
        return 0;
      cache_write_at (disk_inode->double_indirect[0], indirect_block->indirect_blocks, 0, BLOCK_SECTOR_SIZE);
    }
    else if (disk_inode->double_indirect[0] == 0)
      return 0;

    int index = block_idx - (NUM_DIRECT + 128);

    cache_read_at (disk_inode->double_indirect[0], indirect_block->indirect_blocks, 0, BLOCK_SECTOR_SIZE);

    if (create && indirect_block->indirect_blocks[index / BLOCK_SECTOR_SIZE] == 0)
    {
      if (!free_map_allocate (&indirect_block->indirect_blocks[index / BLOCK_SECTOR_SIZE]))
        return 0;
      cache_write_at (indirect_block->indirect_blocks[index / BLOCK_SECTOR_SIZE], double_indirect_block->indirect_blocks, 0, BLOCK_SECTOR_SIZE);
      cache_write_at (disk_inode->double_indirect[0], indirect_block->indirect_blocks, 0, BLOCK_SECTOR_SIZE);
    }
    else if (indirect_block->indirect_blocks[index / BLOCK_SECTOR_SIZE] == 0)
      return 0;

    cache_read_at (indirect_block->indirect_blocks[index / BLOCK_SECTOR_SIZE], double_indirect_block->indirect_blocks, 0, BLOCK_SECTOR_SIZE);
    sector = double_indirect_block->indirect_blocks[index % BLOCK_SECTOR_SIZE];

    if (sector == 0 && create)
    {
      if (!free_map_allocate (&double_indirect_block->indirect_blocks[index % BLOCK_SECTOR_SIZE]))
        return 0;
      cache_write_at (indirect_block->indirect_blocks[index / BLOCK_SECTOR_SIZE],
                      double_indirect_block->indirect_blocks, 0, BLOCK_SECTOR_SIZE);
      cache_write_at (disk_inode->double_indirect[0], indirect_block->indirect_blocks, 0, BLOCK_SECTOR_SIZE);

      // printf ("double indirect sector created : %d\n", double_indirect_block->indirect_blocks[index % BLOCK_SECTOR_SIZE]);
      return double_indirect_block->indirect_blocks[index % BLOCK_SECTOR_SIZE];
    }
    // printf ("existing double indirect : %d\n", sector);
    return sector;
  }
  ASSERT (0);
}

/* List of open inodes, so that opening a single inode twice
   returns the same `struct inode'. */
static struct list open_inodes;

/* Initializes the inode module. */
void
inode_init (void)
{
  list_init (&open_inodes);
}

/* Initializes an inode with LENGTH bytes of data and
   writes the new inode to sector SECTOR on the file system
   device.
   Returns true if successful.
   Returns false if memory or disk allocation fails. */
bool
inode_create (block_sector_t sector, off_t length, bool isdir)
{
  struct inode_disk *disk_inode = NULL;
  bool success = false;

  ASSERT (length >= 0);

  /* If this assertion fails, the inode structure is not exactly
     one sector in size, and you should fix that. */
  ASSERT (sizeof *disk_inode == BLOCK_SECTOR_SIZE);

  disk_inode = calloc (1, sizeof *disk_inode);
  if (disk_inode != NULL)
    {
      size_t sectors = bytes_to_sectors (length);
      //printf ("\nlength %d, sectors %d\n", length, sectors);
      disk_inode->length = length;
      disk_inode->magic = INODE_MAGIC;
      disk_inode->isdir = isdir;
      disk_inode->entry_cnt = 0;

      struct indirect_block *indirect_block = NULL;
      struct indirect_block *double_indirect_block = NULL;

      if (sectors > NUM_DIRECT)
        indirect_block = calloc (1, sizeof (struct indirect_block));
      if (sectors > NUM_DIRECT + 128)
        double_indirect_block = calloc (1, sizeof (struct indirect_block));

      for (size_t i = 0; i < sectors; i++)
      {
        if (i >= NUM_DIRECT)
          memset (indirect_block, 0, sizeof (struct indirect_block));
        if (i >= NUM_DIRECT + 128)
          memset (double_indirect_block, 0, sizeof (struct indirect_block));
        if (inode_block_to_sector (disk_inode, i, indirect_block, double_indirect_block, true) == 0)
          goto done;
        // struct inode_disk *empty = calloc (1, sizeof (disk_inode));
        // cache_write_at (sector_, empty, 0, BLOCK_SECTOR_SIZE);
        // free (empty);
        //printf ("allocated sector %d\n", sector_);

      }

      cache_write_at (sector, disk_inode, 0, BLOCK_SECTOR_SIZE);

      free (indirect_block);
      free (double_indirect_block);

      success = true;
    }

  done:
    //printf ("create done\n");
    free (disk_inode);
    return success;
}

/* Reads an inode from SECTOR
   and returns a `struct inode' that contains it.
   Returns a null pointer if memory allocation fails. */
struct inode *
inode_open (block_sector_t sector)
{
  struct list_elem *e;
  struct inode *inode;

  /* Check whether this inode is already open. */
  //lock_acquire ()
  for (e = list_begin (&open_inodes); e != list_end (&open_inodes);
       e = list_next (e))
    {
      inode = list_entry (e, struct inode, elem);
      if (inode->sector == sector)
        {
          inode_reopen (inode);
          return inode;
        }
    }

  /* Allocate memory. */
  inode = malloc (sizeof *inode);
  if (inode == NULL)
    return NULL;

  /* Initialize. */
  list_push_front (&open_inodes, &inode->elem);
  inode->sector = sector;
  inode->open_cnt = 1;
  inode->deny_write_cnt = 0;
  inode->removed = false;
  lock_init (&inode->extension_lock);
  // block_read (fs_device, inode->sector, &inode->data);
  return inode;
}

/* Reopens and returns INODE. */
struct inode *
inode_reopen (struct inode *inode)
{
  if (inode != NULL)
    inode->open_cnt++;
  return inode;
}

/* Returns INODE's inode number. */
block_sector_t
inode_get_inumber (const struct inode *inode)
{
  return inode->sector;
}

int
inode_get_open_cnt (const struct inode *inode)
{
  return inode->open_cnt;
}

/* Closes INODE and writes it to disk.
   If this was the last reference to INODE, frees its memory.
   If INODE was also a removed inode, frees its blocks. */
void
inode_close (struct inode *inode)
{
  /* Ignore null pointer. */
  if (inode == NULL)
    return;

  /* Release resources if this was the last opener. */
  if (--inode->open_cnt == 0)
    {
      /* Remove from inode list and release lock. */
      list_remove (&inode->elem);

      /* Deallocate blocks if removed. */
      if (inode->removed)
        {
          struct inode_disk *disk_inode = get_disk_inode (inode);
          ASSERT (disk_inode != NULL);

          free_map_release (inode->sector);

          struct indirect_block *indirect_block = NULL;
          struct indirect_block *double_indirect_block = NULL;

          size_t sectors = bytes_to_sectors (disk_inode->length);

          if (sectors > NUM_DIRECT)
            indirect_block = calloc (1, sizeof(struct indirect_block));
          if (sectors > NUM_DIRECT + 128)
            double_indirect_block = calloc (1, sizeof(struct indirect_block));

          for (size_t i = 0; i < sectors; i++)
          {
            if (i >= NUM_DIRECT)
              memset (indirect_block, 0, sizeof (struct indirect_block));
            if (i >= NUM_DIRECT + 128)
              memset (double_indirect_block, 0, sizeof (struct indirect_block));
            block_sector_t sector = inode_block_to_sector (disk_inode, i, indirect_block,
                        double_indirect_block, false);
            if (sector != 0)
              free_map_release (sector);
          }

          free (indirect_block);
          free (double_indirect_block);

          free (disk_inode);
        }

      free (inode);
    }
}

/* Marks INODE to be deleted when it is closed by the last caller who
   has it open. */
void
inode_remove (struct inode *inode)
{
  ASSERT (inode != NULL);
  inode->removed = true;
}

/* Reads SIZE bytes from INODE into BUFFER, starting at position OFFSET.
   Returns the number of bytes actually read, which may be less
   than SIZE if an error occurs or end of file is reached. */
off_t
inode_read_at (struct inode *inode, void *buffer_, off_t size, off_t offset)
{
  uint8_t *buffer = buffer_;
  off_t bytes_read = 0;

  struct indirect_block *indirect_block = NULL;
  struct indirect_block *double_indirect_block = NULL;

  size_t sectors = bytes_to_sectors (size + offset);

  if (sectors > NUM_DIRECT)
    indirect_block = calloc (1, sizeof (struct indirect_block));

  if (sectors > NUM_DIRECT + 128)
    double_indirect_block = calloc (1, sizeof (struct indirect_block));

  struct inode_disk *disk_inode = get_disk_inode (inode);

  while (size > 0)
    {
      /* Disk sector to read, starting byte offset within sector. */
      int block_idx = offset/BLOCK_SECTOR_SIZE;

      if (block_idx >= NUM_DIRECT)
        memset (indirect_block, 0, sizeof (struct indirect_block));
      if (block_idx >= NUM_DIRECT + 128)
        memset (double_indirect_block, 0, sizeof (struct indirect_block));

      block_sector_t sector_idx = inode_block_to_sector (disk_inode, block_idx,
                indirect_block, double_indirect_block, false);

      //printf ("reading from sector_idx: %d, offset: %d, size %d\n", sector_idx, offset, size);

      int sector_ofs = offset % BLOCK_SECTOR_SIZE;

      /* Bytes left in inode, bytes left in sector, lesser of the two. */
      off_t inode_left = inode_length (inode) - offset;
      int sector_left = BLOCK_SECTOR_SIZE - sector_ofs;
      int min_left = inode_left < sector_left ? inode_left : sector_left;

      /* Number of bytes to actually copy out of this sector. */
      int chunk_size = size < min_left ? size : min_left;
      if (chunk_size <= 0)
        break;

      if (sector_idx == 0 && inode_left > 0)
      {
        memset (buffer + bytes_read, 0, chunk_size);
        size -= chunk_size;
        offset += chunk_size;
        bytes_read += chunk_size;
        continue;
      }
      else if (sector_idx == 0)
        goto done;

      cache_read_at (sector_idx, buffer + bytes_read, sector_ofs, chunk_size);

      /* Advance. */
      size -= chunk_size;
      offset += chunk_size;
      bytes_read += chunk_size;
    }

  done:
    free (disk_inode);
    free (indirect_block);
    free (double_indirect_block);
    return bytes_read;
}

/* Writes SIZE bytes from BUFFER into INODE, starting at OFFSET.
   Returns the number of bytes actually written, which may be
   less than SIZE if end of file is reached or an error occurs.
   (Normally a write at end of file would extend the inode, but
   growth is not yet implemented.) */
off_t
inode_write_at (struct inode *inode, const void *buffer_, off_t size,
                off_t offset)
{
  const uint8_t *buffer = buffer_;
  off_t bytes_written = 0;

  if (inode->deny_write_cnt)
    return 0;

  struct indirect_block *indirect_block = NULL;
  struct indirect_block *double_indirect_block = NULL;

  size_t sectors = bytes_to_sectors (size + offset);

  if (sectors > NUM_DIRECT)
    indirect_block = calloc (1, sizeof (struct indirect_block));

  if (sectors > NUM_DIRECT + 128)
    double_indirect_block = calloc (1, sizeof (struct indirect_block));

  struct inode_disk *disk_inode = get_disk_inode (inode);

  while (size > 0)
    {
      /* Disk sector to read, starting byte offset within sector. */
      int block_idx = offset/BLOCK_SECTOR_SIZE;

      int sector_ofs = offset % BLOCK_SECTOR_SIZE;

      /* Bytes left in inode, bytes left in sector, lesser of the two. */
      //off_t inode_left = inode_length (inode) - offset;
      int sector_left = BLOCK_SECTOR_SIZE - sector_ofs;
      //int min_left = inode_left < sector_left ? inode_left : sector_left;

      /* Number of bytes to actually copy out of this sector. */
      // int chunk_size = size < min_left ? size : min_left;
      int chunk_size = size < sector_left ? size : sector_left;
      if (chunk_size <= 0)
        break;

      if (block_idx >= NUM_DIRECT)
        memset (indirect_block, 0, sizeof (struct indirect_block));
      if (block_idx >= NUM_DIRECT + 128)
        memset (double_indirect_block, 0, sizeof (struct indirect_block));

      //printf ("offset = %d, chunk_size = %d, disk_inode->length: %d\n", offset, chunk_size, disk_inode->length);

      if (offset + chunk_size > disk_inode->length)
      {
        //lock_acquire (&inode->extension_lock);
        if (offset + chunk_size > disk_inode->length)
        {
          block_sector_t sector_idx = inode_block_to_sector (disk_inode, block_idx,
                    indirect_block, double_indirect_block, true);

          if (sector_idx == 0)
            goto done;

          disk_inode->length = offset + chunk_size;
          //lock_release (&inode->extension_lock);
          cache_write_at (inode->sector, disk_inode, 0, BLOCK_SECTOR_SIZE);

          //printf ("(extension) writing to sector_idx: %d, offset: %d, size %d\n", sector_idx, offset, size);
          cache_write_at (sector_idx, buffer + bytes_written, sector_ofs, chunk_size);
        }
        else
        {
          //lock_release (&inode->extension_lock);
        }
      }
      else
      {
        block_sector_t sector_idx = inode_block_to_sector (disk_inode, block_idx,
                  indirect_block, double_indirect_block, true);

        if (sector_idx == 0)
          goto done;

        //printf ("(non-extension) writing to sector_idx: %d, offset: %d, size %d\n", sector_idx, offset, size);
        cache_write_at (sector_idx, buffer + bytes_written, sector_ofs, chunk_size);
      }

      /* Advance. */
      size -= chunk_size;
      offset += chunk_size;
      bytes_written += chunk_size;
    }

  done:
    free (disk_inode);

    free (indirect_block);
    free (double_indirect_block);
    //printf ("bytes_written: %d\n", bytes_written);

    return bytes_written;
}

/* Disables writes to INODE.
   May be called at most once per inode opener. */
void
inode_deny_write (struct inode *inode)
{
  inode->deny_write_cnt++;
  ASSERT (inode->deny_write_cnt <= inode->open_cnt);
}

/* Re-enables writes to INODE.
   Must be called once by each inode opener who has called
   inode_deny_write() on the inode, before closing the inode. */
void
inode_allow_write (struct inode *inode)
{
  ASSERT (inode->deny_write_cnt > 0);
  ASSERT (inode->deny_write_cnt <= inode->open_cnt);
  inode->deny_write_cnt--;
}

/* Returns the length, in bytes, of INODE's data. */
off_t
inode_length (const struct inode *inode)
{
  struct inode_disk *disk_inode = get_disk_inode (inode);
  ASSERT (disk_inode != NULL);
  off_t ret = disk_inode->length;
  free (disk_inode);
  return ret;
}

bool
inode_isdir (const struct inode *inode)
{
  struct inode_disk *disk_inode = get_disk_inode (inode);
  ASSERT (disk_inode != NULL);
  bool ret = disk_inode->isdir;
  free (disk_inode);
  return ret;
}

void
inode_entrycnt_inc (const struct inode *inode)
{
  struct inode_disk *disk_inode = get_disk_inode (inode);
  ASSERT (disk_inode != NULL);
  disk_inode->entry_cnt++;
  cache_write_at (inode->sector, disk_inode, 0, BLOCK_SECTOR_SIZE);
  free (disk_inode);
}

void
inode_entrycnt_dec (const struct inode *inode)
{
  struct inode_disk *disk_inode = get_disk_inode (inode);
  ASSERT (disk_inode != NULL);
  disk_inode->entry_cnt--;
  ASSERT (disk_inode->entry_cnt >= 0);
  cache_write_at (inode->sector, disk_inode, 0, BLOCK_SECTOR_SIZE);
  free (disk_inode);
}

bool
inode_emptydir (const struct inode *inode)
{
  struct inode_disk *disk_inode = get_disk_inode (inode);
  ASSERT (disk_inode != NULL);
  bool ret = disk_inode->entry_cnt == 0;
  free (disk_inode);
  return ret;
}
