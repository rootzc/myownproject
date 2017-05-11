#ifndef _VR_OBJECT_H_
#define _VR_OBJECT_H_

#define OBJ_SHARED_INTEGERS 10000
#define OBJ_SHARED_BULKHDR_LEN 32

/* Object types */
//对象类型
#define OBJ_STRING 0
#define OBJ_LIST 1
#define OBJ_SET 2
#define OBJ_ZSET 3
#define OBJ_HASH 4

/* Objects encoding. Some kind of objects like Strings and Hashes can be
 * internally represented in multiple ways. The 'encoding' field of the object
 * is set to one of this fields for this object. */
//底层编码
//原生字符串sds
#define OBJ_ENCODING_RAW 0     /* Raw representation */
//int
#define OBJ_ENCODING_INT 1     /* Encoded as integer */
//hash table
#define OBJ_ENCODING_HT 2      /* Encoded as hash table */
//zipmap
#define OBJ_ENCODING_ZIPMAP 3  /* Encoded as zipmap */
//链表
#define OBJ_ENCODING_LINKEDLIST 4 /* Encoded as regular linked list */
//压缩链表
#define OBJ_ENCODING_ZIPLIST 5 /* Encoded as ziplist */
//整型集合
#define OBJ_ENCODING_INTSET 6  /* Encoded as intset */
//跳跃表
#define OBJ_ENCODING_SKIPLIST 7  /* Encoded as skiplist */
//综合sds
#define OBJ_ENCODING_EMBSTR 8  /* Embedded sds string encoding */
//快速链表
#define OBJ_ENCODING_QUICKLIST 9 /* Encoded as linked list of ziplists */

//hash key
#define OBJ_HASH_KEY 1
//hash value
#define OBJ_HASH_VALUE 2

//判断对象的编码
#define sdsEncodedObject(objptr) (objptr->encoding == OBJ_ENCODING_RAW || objptr->encoding == OBJ_ENCODING_EMBSTR)

/* The actual Redis Object */

#define LRU_BITS 24
#define LRU_CLOCK_MAX ((1<<LRU_BITS)-1) /* Max value of obj->lru */
#define LRU_CLOCK_RESOLUTION 1000 /* LRU clock resolution in ms */

//多态对象
typedef struct vr_object {
    //对象类型
    unsigned type:4;
    //编码
    unsigned encoding:4;
    //有效时间
    unsigned lru:LRU_BITS; /* lru time (relative to server.lruclock) */
    //常量？
    unsigned constant:1;
    //引用计数
    int refcount;
    //底层数据
    void *ptr;
} robj;


//---------------------------------对象api--------------------------------------------//

void decrRefCount(robj *o);                                 //减少对象计数
void decrRefCountVoid(void *o);                             //
void incrRefCount(robj *o);
robj *resetRefCount(robj *obj);
void freeObject(robj *o);
void freeObjectVoid(void *o);
void freeStringObject(robj *o);
void freeListObject(robj *o);
void freeSetObject(robj *o);
void freeZsetObject(robj *o);
void freeHashObject(robj *o);
robj *createObject(int type, void *ptr);
robj *createStringObject(const char *ptr, size_t len);
robj *createRawStringObject(const char *ptr, size_t len);
robj *createEmbeddedStringObject(const char *ptr, size_t len);
robj *dupStringObject(robj *o);
robj *dupStringObjectUnconstant(robj *o);
int isObjectRepresentableAsLongLong(robj *o, long long *llongval);
robj *tryObjectEncoding(robj *o);
robj *getDecodedObject(robj *o);
size_t stringObjectLen(robj *o);
robj *createStringObjectFromLongLong(long long value);
robj *createStringObjectFromLongDouble(long double value, int humanfriendly);
robj *createQuicklistObject(void);
robj *createZiplistObject(void);
robj *createSetObject(void);
robj *createIntsetObject(void);
robj *createHashObject(void);
robj *createZsetObject(void);
robj *createZsetZiplistObject(void);
int getLongFromObjectOrReply(struct client *c, robj *o, long *target, const char *msg);
int checkType(struct client *c, robj *o, int type);
int getLongLongFromObjectOrReply(struct client *c, robj *o, long long *target, const char *msg);
int getDoubleFromObjectOrReply(struct client *c, robj *o, double *target, const char *msg);
int getLongLongFromObject(robj *o, long long *target);
int getLongDoubleFromObject(robj *o, long double *target);
int getLongDoubleFromObjectOrReply(struct client *c, robj *o, long double *target, const char *msg);
char *strEncoding(int encoding);
int compareStringObjects(robj *a, robj *b);
int collateStringObjects(robj *a, robj *b);
int equalStringObjects(robj *a, robj *b);
unsigned long long estimateObjectIdleTime(robj *o);

size_t getStringObjectSdsUsedMemory(robj *o);

robj *objectCommandLookup(struct client *c, robj *key);
robj *objectCommandLookupOrReply(struct client *c, robj *key, robj *reply);
void objectCommand(struct client *c);

#endif
