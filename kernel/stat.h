#define T_DIR     1   // Directory
#define T_FILE    2   // File
#define T_DEVICE  3   // Device

struct stat {
  int dev;     // File system's disk device
  uint ino;    // Inode number
  short type;  // Type of file
  short nlink; // Number of links to file
  uint64 size; // Size of file in bytes
};

struct mlfqinfo{
    int level;//current queue level
    int ticks[4];//total ticks consumed at each level
    int times_scheduled;//number of times process has been scheduled
    int total_syscalls;//total system calls made
    
};