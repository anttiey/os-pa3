/**********************************************************************
 * Copyright (c) 2020-2021
 *  Sang-Hoon Kim <sanghoonkim@ajou.ac.kr>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTIABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 **********************************************************************/

#include <stdio.h>
#include <stdlib.h>
#include <strings.h>

#include "types.h"
#include "list_head.h"
#include "vm.h"

/**
 * Ready queue of the system
 */
extern struct list_head processes;

/**
 * Currently running process
 * 
 * 
 * 
 */
extern struct process *current;

/**
 * Page Table Base Register that MMU will walk through for address translation
 */
extern struct pagetable *ptbr;

/**
 * TLB of the system.
 */
extern struct tlb_entry tlb[1UL << (PTES_PER_PAGE_SHIFT * 2)];


/**
 * The number of mappings for each page frame. Can be used to determine how
 * many processes are using the page frames.
 */
extern unsigned int mapcounts[];


/**
 * lookup_tlb(@vpn, @pfn)
 *
 * DESCRIPTION
 *   Translate @vpn of the current process through TLB. DO NOT make your own
 *   data structure for TLB, but use the defined @tlb data structure
 *   to translate. If the requested VPN exists in the TLB, return true
 *   with @pfn is set to its PFN. Otherwise, return false.
 *   The framework calls this function when needed, so do not call
 *   this function manually.
 *
 * RETURN
 *   Return true if the translation is cached in the TLB.
 *   Return false otherwise
 */
bool lookup_tlb(unsigned int vpn, unsigned int *pfn)
{
	
	for(int i=0; i < NR_TLB_ENTRIES; i++) {
		struct tlb_entry *t = &tlb[i];

		if (!t->valid) continue;

		if (t->vpn == vpn) {
			*pfn = t->pfn;
			return true;
		}
	}
	
	return false;

}


/**
 * insert_tlb(@vpn, @pfn)
 *
 * DESCRIPTION
 *   Insert the mapping from @vpn to @pfn into the TLB. The framework will call
 *   this function when required, so no need to call this function manually.
 *
 */
void insert_tlb(unsigned int vpn, unsigned int pfn)
{
	for(int i=0; i < NR_TLB_ENTRIES; i++) {

		struct tlb_entry *t = &tlb[i];

		if (!t->valid) {
			t->valid = true;
			t->vpn = vpn;
			t->pfn = pfn;
			return;
		}

	}
}

void free_tlb(unsigned int vpn) {

	for(int i=0; i < NR_TLB_ENTRIES; i++) {

		struct tlb_entry *t = &tlb[i];

		if (t->valid) {
			if (t->vpn == vpn) {
				t->valid = false;
		 	}	
		}

	}

}

void flush_tlb() {

	for(int i=0; i < NR_TLB_ENTRIES; i++) {

		struct tlb_entry *t = &tlb[i];

		if (t->valid) {
			t->valid = false;
		}

	}

}


/**
 * alloc_page(@vpn, @rw)
 *
 * DESCRIPTION
 *   Allocate a page frame that is not allocated to any process, and map it
 *   to @vpn. When the system has multiple free pages, this function should
 *   allocate the page frame with the **smallest pfn**.
 *   You may construct the page table of the @current process. When the page
 *   is allocated with RW_WRITE flag, the page may be later accessed for writes.
 *   However, the pages populated with RW_READ only should not be accessed with
 *   RW_WRITE accesses.
 *
 * RETURN
 *   Return allocated page frame number.
 *   Return -1 if all page frames are allocated.
 */
unsigned int alloc_page(unsigned int vpn, unsigned int rw)
{
	int outIndex = vpn / NR_PTES_PER_PAGE;
	int inIndex = vpn % NR_PTES_PER_PAGE;

	if(current->pagetable.outer_ptes[outIndex] == NULL) {
		current->pagetable.outer_ptes[outIndex] = (struct pte_directory *)malloc(sizeof(struct pte_directory));
	}

	int pfn;

	for(int i = 0; i < NR_PAGEFRAMES; i++) {
		pfn = i;
		if(mapcounts[i] == 0) {
			break;
		}
	}

	if(mapcounts[pfn] == 16) {
		return -1;
	}

	current->pagetable.outer_ptes[outIndex]->ptes[inIndex].valid = true;
	current->pagetable.outer_ptes[outIndex]->ptes[inIndex].writable = false;
	current->pagetable.outer_ptes[outIndex]->ptes[inIndex].pfn = pfn;

	if(rw == 1) {
		current->pagetable.outer_ptes[outIndex]->ptes[inIndex].private = 1;
	} else {
		current->pagetable.outer_ptes[outIndex]->ptes[inIndex].writable = true;
		current->pagetable.outer_ptes[outIndex]->ptes[inIndex].private = 3;
	}

	mapcounts[pfn]++;

	return pfn;

}


/**
 * free_page(@vpn)
 *
 * DESCRIPTION
 *   Deallocate the page from the current processor. Make sure that the fields
 *   for the corresponding PTE (valid, writable, pfn) is set @false or 0.
 *   Also, consider carefully for the case when a page is shared by two processes,
 *   and one process is to free the page.
 */
void free_page(unsigned int vpn)
{

	int outIndex = vpn / NR_PTES_PER_PAGE;
	int inIndex = vpn % NR_PTES_PER_PAGE;

	int pfn = current->pagetable.outer_ptes[outIndex]->ptes[inIndex].pfn;

	mapcounts[pfn]--;

	current->pagetable.outer_ptes[outIndex]->ptes[inIndex].valid = false;
	current->pagetable.outer_ptes[outIndex]->ptes[inIndex].writable = false;
	current->pagetable.outer_ptes[outIndex]->ptes[inIndex].pfn = 0;
	current->pagetable.outer_ptes[outIndex]->ptes[inIndex].private = 0;

	free_tlb(vpn);

}


/**
 * handle_page_fault()
 *
 * DESCRIPTION
 *   Handle the page fault for accessing @vpn for @rw. This function is called
 *   by the framework when the __translate() for @vpn fails. This implies;
 *   0. page directory is invalid
 *   1. pte is invalid
 *   2. pte is not writable but @rw is for write
 *   This function should identify the situation, and do the copy-on-write if
 *   necessary.
 *
 * RETURN
 *   @true on successful fault handling
 *   @false otherwise
 */
bool handle_page_fault(unsigned int vpn, unsigned int rw)
{

	int outIndex = vpn / NR_PTES_PER_PAGE;
	int inIndex = vpn % NR_PTES_PER_PAGE;
	
	// page directory is invalid
	if(current->pagetable.outer_ptes[outIndex] == NULL) {
		return false;
	}

	int pfn = current->pagetable.outer_ptes[outIndex]->ptes[inIndex].pfn;

	// pte is invalid 
	if(current->pagetable.outer_ptes[outIndex]->ptes[inIndex].valid == false) {

		if(mapcounts[pfn] == 1) {

			current->pagetable.outer_ptes[outIndex]->ptes[inIndex].valid = true;

		} else {

			current->pagetable.outer_ptes[outIndex]->ptes[inIndex].valid = true;
			mapcounts[pfn]--;
			alloc_page(vpn, rw);

		}

		return true;
	}

	// pte is not writable but @rw is for write

	if((current->pagetable.outer_ptes[outIndex]->ptes[inIndex].writable == false) && 
		(current->pagetable.outer_ptes[outIndex]->ptes[inIndex].private == 3)) {

		if(mapcounts[pfn] == 1) {

			current->pagetable.outer_ptes[outIndex]->ptes[inIndex].writable = true;
			
		} else {

			current->pagetable.outer_ptes[outIndex]->ptes[inIndex].writable = true;
			mapcounts[pfn]--;
			alloc_page(vpn, rw);

		}

		return true;

	}

	return false;

}


/**
 * switch_process()
 *
 * DESCRIPTION
 *   If there is a process with @pid in @processes, switch to the process.
 *   The @current process at the moment should be put into the @processes
 *   list, and @current should be replaced to the requested process.
 *   Make sure that the next process is unlinked from the @processes, and
 *   @ptbr is set properly.
 *
 *   If there is no process with @pid in the @processes list, fork a process
 *   from the @current. This implies the forked child process should have
 *   the identical page table entry 'values' to its parent's (i.e., @current)
 *   page table. 
 *   To implement the copy-on-write feature, you should manipulate the writable
 *   bit in PTE and mapcounts for shared pages. You may use pte->private for 
 *   storing some useful information :-)
 */
void switch_process(unsigned int pid)
{

	struct process *temp = NULL;
	struct process *child = NULL;

	bool isExist = false;

	flush_tlb();

	list_for_each_entry(temp, &processes, list) {
		if(temp->pid == pid) {
			isExist = true;
			break;
		}
	}

	// Switch the process

	/* 	1.	The @current process at the moment should be put into the @processes list
		2.	@current should be replaced to the requested process.
		3.	the next process is unlinked from the @processes
		4.	@ptbr is set properly.
	*/

	if(isExist == true) {

		list_add_tail(&current->list, &processes);
		current = temp;

		list_del_init(&current->list);
		ptbr = &current->pagetable;

	} else {
	
	// Fork a process

	/* 	1. 	This implies the forked child process should have the 
			identical page table entry 'values' to its parent's (i.e., @current) page table. 
		2.	To implement the copy-on-write feature, you should manipulate the writable
 			bit in PTE and mapcounts for shared pages.
		3.	To implement the copy-on-write feature, you should manipulate the writable
 			bit in PTE and mapcounts for shared pages.
	*/

		child = (struct process *)malloc(sizeof(struct process));

		for(int i = 0; i < NR_PTES_PER_PAGE; i++) {

			if(current->pagetable.outer_ptes[i] == NULL) continue;

			child->pagetable.outer_ptes[i] = (struct pte_directory *)malloc(sizeof(struct pte_directory));

			for(int j = 0; j < NR_PTES_PER_PAGE; j++) {

				if(current->pagetable.outer_ptes[i]->ptes[j].valid == false) continue;

				child->pagetable.outer_ptes[i]->ptes[j].valid = current->pagetable.outer_ptes[i]->ptes[j].valid;
				child->pagetable.outer_ptes[i]->ptes[j].writable = current->pagetable.outer_ptes[i]->ptes[j].writable;
				child->pagetable.outer_ptes[i]->ptes[j].pfn = current->pagetable.outer_ptes[i]->ptes[j].pfn;
				child->pagetable.outer_ptes[i]->ptes[j].private = current->pagetable.outer_ptes[i]->ptes[j].private;

				if(current->pagetable.outer_ptes[i]->ptes[j].private == 3) {
					current->pagetable.outer_ptes[i]->ptes[j].writable = false;
					child->pagetable.outer_ptes[i]->ptes[j].writable = false;
				}

				mapcounts[child->pagetable.outer_ptes[i]->ptes[j].pfn]++;

			}

		}

		list_add_tail(&current->list, &processes);
		child->pid = pid;

		current = child;
		ptbr = &current->pagetable;

	}
}

