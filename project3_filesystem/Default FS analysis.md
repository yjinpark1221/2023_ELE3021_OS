# buf.h

```
struct buf {
  int flags;
  uint dev;
  uint blockno;
  struct sleeplock lock;
  uint refcnt;
  struct buf *prev; // LRU cache list
  struct buf *next;
  struct buf *qnext; // disk queue
  uchar data[BSIZE];
};
```

# bio.c

```
struct {
  struct spinlock lock;
  struct buf buf[NBUF];

  // Linked list of all buffers, through prev/next.
  // head.next is most recently used.
  struct buf head;
} bcache;
```
bcache : 배열 + 링크드리스트로 이뤄진 버퍼 

## void binit(void)

bcache lock init
버퍼 배열 돌면서 연결해주기

## static struct buf* bget(uint dev, uint blockno)

버퍼 리스트 돌면서 b->dev == dev, b->blockno == blockno인 b를 찾기
- 있으면 b->refcnt 증가, b lock acquire, b return
- 없으면 b->refcnt == 0 && not dirty인 b를 찾아
  - b->refcnt 증가, b lock acquire, b return

## struct buf* bread(uint dev, uint blockno)

bget한 후 valid한 값이 아니면, 디스크에 쓰고 return


## void bwrite(struct buf *b)
- install_trans

디스크에 쓰기

## void brelse(struct buf *b)

b lock release하고, bcache MRU list에 연결

# log.c

```
struct logheader {
  int n;
  int block[LOGSIZE];
};

struct log {
  struct spinlock lock;
  int start;
  int size;
  int outstanding; // how many FS sys calls are executing.
  int committing;  // in commit(), please wait.
  int dev;
  struct logheader lh;
};
struct log log;
```

## void initlog(int dev)

log 값 초기화

## static void install_trans(void)

disk에 적힌 log deㅍ

## static void read_head(void)

bread로 disk에서 log header 위치 찾아서 log.lh.block에 내용 복사

