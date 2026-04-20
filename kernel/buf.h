struct buf {
  int valid;   // has data been read from disk?
  int disk;    // does disk "own" buf?
  uint dev;
  uint blockno;
  struct sleeplock lock;
  uint refcnt;
  struct buf *prev; // LRU cache list
  struct buf *next;
  uchar data[BSIZE];

  struct buf *qnext; // disk scheduling queue
  struct proc *p;// pointer to the process that requested this disk I/O
  int is_write;//1 if write else 0 if read
};

