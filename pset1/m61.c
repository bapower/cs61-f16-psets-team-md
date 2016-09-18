#define M61_DISABLE 1
#include "m61.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <inttypes.h>
#include <assert.h>
#include <stdbool.h>

// push on 9-17 to bry

/* variables from m61.h for getstats */
static unsigned long long nactive, active_size, ntotal, total_size, nfail, fail_size;
char* heap_min;
char* heap_max;

/* metadata structure to get active_size */
struct m61_metadata {
    unsigned long long block_size;            
    int active;     
    char* address;                     
    const char* file;                   
    int line;                           
    struct m61_metadata* prev_ptr;          
    struct m61_metadata* next_ptr;                                
};

struct m61_metadata* metadata_link = NULL;

typedef struct m61_buffers {
    unsigned long long buffer1;      
    unsigned long long buffer2;     
} m61_buffers;

void* m61_malloc(size_t sz, const char* file, int line) {
    (void) file, (void) line;   // avoid uninitialized variable warnings
    // Your code here.
    if (sz > SIZE_MAX - sizeof(struct m61_statistics) - sizeof(m61_buffers)) {
        nfail++;
        fail_size += sz;
        return NULL;
    }
    m61_buffers buffer;
	buffer.buffer1 = 1234;
	buffer.buffer2 = 4321;

    struct m61_metadata metadata;
	metadata.block_size = sz;
	metadata.active = 0;
	metadata.address = NULL;
	metadata.file = file;
	metadata.line = line;
	metadata.prev_ptr = NULL;
	metadata.next_ptr = NULL;

    struct m61_metadata* ptr = NULL;
    ptr = malloc(sizeof(struct m61_metadata)+sz+sizeof(m61_buffers));

    if (!ptr) {
        nfail++;
        fail_size += sz;
        return ptr;
    }

    ntotal++;
    nactive++;
    total_size += sz;
    active_size += sz;

    char* heap_min_t = (char*) ptr;
    char* heap_max_t = (char*) ptr + sz + sizeof(struct m61_metadata) + sizeof(m61_buffers);
    if (!heap_min || heap_min >= heap_min_t) {
        heap_min = heap_min_t;
    }

    if (!heap_max || heap_max <= heap_max_t) {
        heap_max = heap_max_t;
    }

    metadata.address = (char*) (ptr + 1);
    *ptr = metadata;
    if (metadata_link) {
        ptr->next_ptr = metadata_link;
        metadata_link->prev_ptr = ptr;
    }
    metadata_link = ptr;

    m61_buffers* buffer_ptr = (m61_buffers*) ((char*) (ptr + 1) + sz);
    *buffer_ptr = buffer;

    return ptr + 1;
}

void m61_free(void *ptr, const char *file, int line) {
    (void) file, (void) line;   // avoid uninitialized variable warnings
    // Your code here.
    if (ptr) {
        struct m61_metadata* new_ptr = (struct m61_metadata*) ptr - 1;
        if ((char*) new_ptr >= heap_min && (char*) new_ptr <= heap_max) {
            if (new_ptr->active != 1) {
                if (new_ptr->address != (char*) ptr) {
                    printf("MEMORY BUG: %s:%d: invalid free of pointer %p, not allocated\n", file, line, ptr);
                    for (struct m61_metadata* metadata = metadata_link; metadata != NULL; metadata = metadata->next_ptr) {
                        if ((char*) ptr >= (char*) metadata && (char*) ptr <= (char*) (metadata + 1) + metadata->block_size + sizeof(m61_buffers))
                            printf("  %s:%d: %p is %zu bytes inside a %llu byte region allocated here\n", 
                                    metadata->file, metadata->line, ptr, (char*) ptr - metadata->address, metadata->block_size);
                    }
                    abort();
                }
                if (new_ptr->next_ptr) {
                    if (new_ptr->next_ptr->prev_ptr != new_ptr) {
                        printf("MEMORY BUG: %s%d: invalid free of pointer %p\n", file, line, ptr);
                        abort();
                    }
                }
                else if (new_ptr->prev_ptr) {
                    if (new_ptr->prev_ptr->next_ptr != new_ptr) {
                        printf("MEMEORY BUG: %s%d: invalid free of pointer %p\n", file, line, ptr);
                        abort();
                    }
                }
                else if ((new_ptr->prev_ptr && new_ptr->next_ptr) &&
                        (new_ptr->prev_ptr->next_ptr != new_ptr || new_ptr->next_ptr->prev_ptr != new_ptr)) {
                    printf("MEMORY BUG: %s%d: invalid free of pointer %p\n", file, line, ptr);
                    abort();
                }

                m61_buffers* buffer_ptr = (m61_buffers*) ((char*) ptr + new_ptr->block_size);
                if (buffer_ptr->buffer1 != 1234 || buffer_ptr->buffer2 != 4321) {
                    printf("MEMORY BUG: %s:%d: detected wild write during free of pointer %p\n", file, line, ptr);
                    abort();
                }

                if (new_ptr->prev_ptr)
                   new_ptr->prev_ptr->next_ptr = new_ptr->next_ptr;
                else
                    metadata_link = new_ptr->next_ptr;
                if (new_ptr->next_ptr)
                    new_ptr->next_ptr->prev_ptr = new_ptr->prev_ptr;

                new_ptr->next_ptr = NULL;
                new_ptr->prev_ptr = NULL;

                // statistics
                nactive--;
                active_size -= new_ptr->block_size;

                new_ptr->active = 1;
                free(new_ptr);
            }
            else {
                printf("MEMORY BUG: %s:%d: invalid free of pointer %p\n", file, line, ptr);
                abort();
            }
        }
        else {
            printf("MEMORY BUG: %s:%d: invalid free of pointer %p, not in heap\n", file, line, ptr);
            abort();
        }
    }
}
/// m61_realloc(ptr, sz, file, line)
///    Reallocate the dynamic memory pointed to by `ptr` to hold at least
///    `sz` bytes, returning a pointer to the new block. If `ptr` is NULL,
///    behaves like `m61_malloc(sz, file, line)`. If `sz` is 0, behaves
///    like `m61_free(ptr, file, line)`. The allocation request was at
///    location `file`:`line`.

void* m61_realloc(void* ptr, size_t sz, const char* file, int line) {
    void* new_ptr = NULL;
    if (sz)
        new_ptr = m61_malloc(sz, file, line);
    if (ptr && new_ptr) {
        // Copy the data from `ptr` into `new_ptr`.
        // To do that, we must figure out the size of allocation `ptr`.
        // Your code here (to fix test012).
        struct m61_metadata* metadata = (struct m61_metadata*) ptr - 1;
        size_t old_sz = metadata->block_size;
        if (old_sz <= sz)
            memcpy(new_ptr, ptr, old_sz);
        else
            memcpy(new_ptr, ptr, sz);
    }
    m61_free(ptr, file, line);
    return new_ptr;
}

void* m61_calloc(size_t nmemb, size_t sz, const char* file, int line) {
    // Your code here (to fix test014).
    if (nmemb * sz < sz || nmemb * sz  < nmemb) {
        nfail++;
        return NULL;
    }
    void* ptr = m61_malloc(nmemb * sz, file, line);
    if (ptr)
        memset(ptr, 0, nmemb * sz);
    return ptr;
}



/// m61_getstatistics(stats)
///    Store the current memory statistics in `*stats`.

void m61_getstatistics(struct m61_statistics* stats) {
    // Stub: set all statistics to enormous numbers
    memset(stats, 255, sizeof(struct m61_statistics));
	stats->nactive=nactive;
	stats->active_size=active_size;
	stats->ntotal=ntotal;
	stats->total_size=total_size;
	stats->nfail=nfail;
	stats->fail_size=fail_size;
	stats->heap_min=heap_min;
	stats->heap_max=heap_max;
}

void m61_printstatistics(void) {
    struct m61_statistics stats;
    m61_getstatistics(&stats);

    printf("malloc count: active %10llu   total %10llu   fail %10llu\n",
           stats.nactive, stats.ntotal, stats.nfail);
    printf("malloc size:  active %10llu   total %10llu   fail %10llu\n",
           stats.active_size, stats.total_size, stats.fail_size);
}


/// m61_printleakreport()
///    Print a report of all currently-active allocated blocks of dynamic
///    memory.

void m61_printleakreport(void) {
    // Your code here.
    
    struct m61_metadata* item = metadata_link;
    
	while(item!=NULL){
	printf("LEAK CHECK: %s:%d: allocated object %p with size %zd\n",item->file, item->line, item, (size_t)item->block_size);
	item=item->next_ptr;
	}
   	
}


