#include <stdlib.h>

#include <dmalloc.h>

#include <darray.h>

//创建的成员数目，成员长度
darray *
darray_create(unsigned long long n, size_t size)
{
    darray *a;
//
    a = dalloc(sizeof(*a));
    if (a == NULL) {
        return NULL;
    }

    a->elem = dalloc(n * size);
    if (a->elem == NULL) {
        dfree(a);
        return NULL;
    }

    a->nelem = 0;
    a->size = size;
    a->nalloc = n;

    return a;
}

void
darray_destroy(darray *a)
{
    darray_deinit(a);
    dfree(a);
}

//初始化
int
darray_init(darray *a, unsigned long long n, size_t size)
{
    a->elem = dalloc(n * size);
    if (a->elem == NULL) {
        return -1;
    }

    a->nelem = 0;
    a->size = size;
    a->nalloc = n;

    return 0;
}

//释放空间
void
darray_deinit(darray *a)
{
    if (a->elem != NULL) {
        dfree(a->elem);
    }
}

//返回给定成员的下标
unsigned long long
darray_idx(darray *a, void *elem)
{
    char *p, *q;
    unsigned long long off, idx;

    p = a->elem;
    q = elem;
    //得到相差的字节数
    off = (unsigned long long)(q - p);
    //总字节数/单个元素的字节数
    idx = off / (unsigned long long)a->size;

    return idx;
}
//压入数据，就是将对应的指针返回：会暴漏指针，有可能会造成内存覆盖
void *
darray_push(darray *a)
{
    void *elem, *new;
    size_t size;

    if (a->nelem == a->nalloc) {

        /* the array is full; allocate new array */
        size = a->size * a->nalloc;
        new = drealloc(a->elem, 2 * size);
        if (new == NULL) {
            return NULL;
        }

        a->elem = new;
        a->nalloc *= 2;
    }
//这句精华：elem：数组起始位置，size：每个成员字节数，nelem为当前已有的数据个数
    elem = (char *)a->elem + a->size * a->nelem;
    a->nelem++;

    return elem;
}

void *
darray_pop(darray *a)
{
    void *elem;

    a->nelem--;
    elem = (char *)a->elem + a->size * a->nelem;

    return elem;
}

//得到对应下表的数据
void *
darray_get(darray *a, unsigned long long idx)
{
    void *elem;

    elem = (char *)a->elem + (a->size * idx);

    return elem;
}

void *
darray_top(darray *a)
{
    return darray_get(a, a->nelem - 1);
}

void
darray_swap(darray *a, darray *b)
{
    darray tmp;

    tmp = *a;
    *a = *b;
    *b = tmp;
}

/*
 * Sort nelem elements of the array in ascending order based on the
 * compare comparator.
 */
void
darray_sort(darray *a, darray_compare_t compare)
{
    qsort(a->elem, a->nelem, a->size, compare);
}

/*
 * Calls the func once for each element in the array as long as func returns
 * success. On failure short-circuits and returns the error status.
 */
//for_each
int
darray_each(darray *a, darray_each_t func, void *data)
{
    unsigned long long i, nelem;

    for (i = 0, nelem = darray_n(a); i < nelem; i++) {
        void *elem = darray_get(a, i);
        int ret;

        ret = func(elem, data);
        if (ret != 0) {
            return -1;
        }
    }

    return 0;
}
