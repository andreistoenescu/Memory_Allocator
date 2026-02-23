// SPDX-License-Identifier: BSD-3-Clause

#include "osmem.h"
#include <sys/mman.h>
#include <unistd.h>
#include <string.h>
#include "block_meta.h"
#include "printf.h"

#define ALIGNMENT 8
#define ALIGN(size) (((size) + (ALIGNMENT - 1)) & ~(ALIGNMENT - 1))
#define BLOCK_META_SIZE sizeof(struct block_meta)
#define MMAP_THRESHOLD (128 * 1024)

static struct block_meta *heap_start;
static size_t mmap_threshold = MMAP_THRESHOLD;
static int initial_status = STATUS_FREE;
static int prealoc_size = MMAP_THRESHOLD;

void set_block(struct block_meta *block, size_t size, int status, struct block_meta *prev, struct block_meta *next)
{
	block->size = size;
	block->status = status;
	block->prev = prev;
	block->next = next;
}

void *init_heap(size_t size)
{
	void *mem = sbrk(size);

	heap_start = (struct block_meta *)mem;
	set_block(heap_start, size - BLOCK_META_SIZE, STATUS_ALLOC, NULL, NULL);
	return (char *)heap_start + BLOCK_META_SIZE;
}

struct block_meta *find_fit(struct block_meta **block, size_t size)
{
	struct block_meta *current = heap_start;

	while (current) {
		if (!(current->status == STATUS_FREE && current->size >= size)) {
			*block = current;
			current = current->next;
		} else {
			break;
		}
	}
	return current;
}

void split_block(struct block_meta *block, size_t size)
{
	size_t aligned_blocks = ALIGN(size) + BLOCK_META_SIZE;

	if (block->size >= size + sizeof(struct block_meta) + ALIGNMENT) {
		struct block_meta *new_block = (struct block_meta *)((char *)block + aligned_blocks);

		set_block(new_block, block->size - aligned_blocks, STATUS_FREE, block, block->next);
		block->status = STATUS_ALLOC;
		block->next = new_block;
		block->size = ALIGN(size);
	} else {
		block->status = STATUS_ALLOC;
	}
}

void merge_fblocks(struct block_meta *block)
{
	if (block->next && block->next->status == STATUS_FREE) {
		block->size += block->next->size + BLOCK_META_SIZE;
		block->next = block->next->next;
		if (block->next)
			block->next->prev = block;
	}

	if (block->prev && block->prev->status == STATUS_FREE) {
		block->prev->size += block->size + BLOCK_META_SIZE;
		block->prev->next = block->next;
		if (block->next)
			block->next->prev = block->prev;
	}
}

void *increase_block(struct block_meta *block, size_t size)
{
	size_t new_size = ALIGN(size);
	size_t size_difference = new_size - block->size;
	void *mem = sbrk(size_difference);

	block->size = new_size;
	block->status = STATUS_ALLOC;

	return mem;
}

void *os_malloc(size_t size)
{
	if (size == 0)
		return NULL;

	size_t block_size = ALIGN(size + sizeof(struct block_meta));

	if (initial_status == STATUS_FREE && block_size <= mmap_threshold) {
		void *mem = init_heap(prealoc_size);

		if (mem == NULL)
			return NULL;
		initial_status = STATUS_ALLOC;
		return mem;
	}

	if (block_size > mmap_threshold) {
		void *mem = mmap(NULL, block_size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);

		struct block_meta *block = (struct block_meta *)mem;

		set_block(block, block_size - BLOCK_META_SIZE, STATUS_MAPPED, NULL, NULL);
		return (char *)block + BLOCK_META_SIZE;
	}

	struct block_meta *last = heap_start;

	struct block_meta *block = find_fit(&last, ALIGN(size));

	if (block) {
		split_block(block, size);
		return (char *)block + BLOCK_META_SIZE;
	}
		if (last->status == STATUS_FREE) {
			void *mem = increase_block(last, size);

			if (mem == NULL)
				return NULL;
			return (char *)last + BLOCK_META_SIZE;
		}
		void *mem = sbrk(block_size);

		block = (struct block_meta *)mem;
		set_block(block, ALIGN(size), STATUS_ALLOC, last, NULL);
		last->next = block;
		return (char *)block + BLOCK_META_SIZE;
	}


void os_free(void *ptr)
{
	if (ptr == NULL)
		return;
	struct block_meta *block = (struct block_meta *)((char *)ptr - BLOCK_META_SIZE);

	if (block->status == STATUS_ALLOC) {
		block->status = STATUS_FREE;
		merge_fblocks(block);
	} else if (block->status == STATUS_MAPPED) {
		munmap(block, block->size + BLOCK_META_SIZE);
	}
}

void *os_calloc(size_t nmemb, size_t size)
{
	if (nmemb <= 0 || size <= 0)
		return NULL;

	size_t total_size = nmemb * size;

	mmap_threshold = 4 * 1024;
	void *ptr = os_malloc(total_size);

	mmap_threshold = MMAP_THRESHOLD;
	memset(ptr, 0, total_size);

	return ptr;
}

void *os_realloc(void *ptr, size_t size)
{
	if (ptr == NULL)
		return os_malloc(size);

	if (size == 0) {
		os_free(ptr);
		return NULL;
	}

	struct block_meta *block = (struct block_meta *)ptr - 1;
	size_t alligned_size = ALIGN(size);

	if (!block)
		return NULL;

	if (block->next && block->next->status == STATUS_FREE) {
		if (block->size + block->next->size >= alligned_size) {
			block->size += block->next->size + BLOCK_META_SIZE;
			block->next = block->next->next;
		if (block->next)
			block->next->prev = block;
		}
	}

	if (block->status == STATUS_FREE)
		return NULL;

	if (block->status == STATUS_MAPPED) {
		void *new_mem = os_malloc(size);

		if (block->size < alligned_size)
			memcpy(new_mem, ptr, block->size);
		else
			memcpy(new_mem, ptr, alligned_size);

		os_free(ptr);
		return new_mem;
	}

	if (block->size >= alligned_size) {
		split_block(block, size);
		return ptr;
	}

	if (!block->next) {
		increase_block(block, size);
		return ptr;
	}

	if (block->next->status == STATUS_FREE) {
		if (block->size + block->next->size + BLOCK_META_SIZE >= alligned_size) {
			merge_fblocks(block);
			split_block(block, size);
			return ptr;
		}
	}

	if (block->size < alligned_size) {
		void *new_mem = os_malloc(size);

		memcpy(new_mem, ptr, block->size);
		os_free(ptr);
		return new_mem;
	}

	return NULL;
}
