#include <stdint.h>
#include "types.h"
#include "mmu.h"
#include "string.h"
#include "memlayout.h"
#include "console.h"

#include "vm.h"
#include "kalloc.h"

/* 
 * Given 'pgdir', a pointer to a page directory, pgdir_walk returns
 * a pointer to the page table entry (PTE) for virtual address 'va'.
 * This requires walking the four-level page table structure.
 *
 * The relevant page table page might not exist yet.
 * If this is true, and alloc == false, then pgdir_walk returns NULL.
 * Otherwise, pgdir_walk allocates a new page table page with kalloc.
 *   - If the allocation fails, pgdir_walk returns NULL.
 *   - Otherwise, the new page is cleared, and pgdir_walk returns
 *     a pointer into the new page table page.
 */

static uint64_t *
pgdir_walk(uint64_t *pgdir, const void *va, int64_t alloc)
{
    // Read mmu.h
    // pte point to next level page table
    uint64_t *nextlv_page_table, *currlv_page_table=pgdir;
    uint64_t *pte_pointer, *new_page_addr;
    uint64_t pte;

    // walking the four-level page table structure
    for (int level = 0; level < 4; level++) {
        // 四次迭代结束之后 curr 和 next 都指向4KB页，然后pte是第四级页表的pte。
        pte_pointer = &currlv_page_table[PTX(level, va)];   // 指向entry
        if (*pte_pointer & PTE_P) {
            // enter next level page table
            nextlv_page_table = *pte_pointer & 0x0000fffffffff000;
        } else { // Invalid
        // TODO: 到了最后一级的时候不用kalloc 因为是直接对应到物理内存的。
            // don't wanna alloc OR kalloc failed
            if (!alloc || (nextlv_page_table = (uint64_t *)kalloc()) == 0)
                return NULL;
            // 对于新分配的下一级页表的地址 我们还要做一些初始化工作
            // 1. memset
            // 2. 末尾的标志位
            // 3. 开头的标志位
            // 4. 放到当前的页表的页表项里面
            memset(nextlv_page_table, 0, PGSIZE); 
            // 这里我们自己创建了pte
            *pte_pointer = (uint64_t)nextlv_page_table | PTE_P;
        }
        currlv_page_table = nextlv_page_table;
    }
    // 最后指向了线性地址
    return pte;
}

/*
 * Create PTEs for virtual addresses starting at va that refer to
 * physical addresses starting at pa. va and size might **NOT**
 * be page-aligned.
 * Use permission bits perm|PTE_P|PTE_TABLE|PTE_AF for the entries.
 *
 * Hint: call pgdir_walk to get the corresponding page table entry
 */

static int
map_region(uint64_t *pgdir, void *va, uint64_t size, uint64_t pa, int64_t perm)
{
    // va -> pa
    uint64_t *pte;
    uint64_t *start, *end;
    // 对齐
    start = ROUNDDOWN(va, PGSIZE);
    end = ROUNDDOWN(va + size - 1, PGSIZE);

    for (;;) {
        if ((pte = pgdir_walk(pgdir, start, 1)) == 0) 
            return -1;
        if (*pte & PTE_P)   // check pte
            panic("map_region: page returned by pgdir_walk invalid");
        // 拿到第四级页表的pte指针之后 需要构造这个页表项
        *pte = (uint64_t)(pa << PGSHIFT);
        *pte |= (perm | PTE_P | PTE_TABLE | PTE_AF);
        if (start == end)
            break;

        start += PGSIZE;
        pa += PGSIZE;
    }
    return 0;
}

/* 
 * Free a page table.
 *
 * Hint: You need to free all existing PTEs for this pgdir.
 */

void
vm_free(uint64_t *pgdir, int level)
{
    uint64_t *pte = NULL;
    for (int i = 0; i < 512; i++) {
        *pte = pgdir[i];
            // This contains child page
            vm_free((uint64_t *)(*pte >> PGSHIFT), level + 1);
            pgdir[i] = 0;
        } else if(*pte & PTE_P) {
            // pte 是指向 pgdir[i] 的指针
            // 上一步已置 0
            panic("vm_free: leaf");
        }
    }
    // 一次释放一页
    kfree((char *)pgdir);
}
