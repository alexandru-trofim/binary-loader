/*
 * Loader Implementation
 *
 * 2022, Operating Systems
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <signal.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <unistd.h>
#include "exec_parser.h"

#define PAGE_SIZE 4096

//the file descriptor of the bin file
static int fd;           
static int bin_size;
static void* mapped_file;
//sigaction to call default handler
static struct sigaction call_default_handler;   
static so_exec_t *exec;

typedef struct mapped_addresses{
	uintptr_t* addr_array;
	int size;
	int capacity;
} mapped_addresses;

int size_of_file() {
	int size = lseek(fd, 0, SEEK_END);
	return size;
}

void add_element(uintptr_t elem, void* data) {
	mapped_addresses* array = data;

	if (array->size + 1 == array->capacity) {
		array->capacity *= 2;
		array->addr_array = realloc(array->addr_array, array->capacity * sizeof(uintptr_t));
	}

	array->addr_array[array->size] = elem;
	array->size++;
}

int check_mapped_addr(uintptr_t addr_to_check, int curr_segment,ssize_t curr_page, void* data) {

	mapped_addresses* casted_data = data;
	//going through all the addresses where we've got a page fault 
	for(int i = 0; i < casted_data->size; ++i) {
		// calculating the page where they belong
		ssize_t allocated_page = (casted_data->addr_array[i] - exec->segments[curr_segment].vaddr) / PAGE_SIZE;
		
		if (allocated_page == curr_page) {
			return 1;
		}
	}
	return 0;
}


static void segv_handler(int signum, siginfo_t *info, void *context)
{

	for (int i = 0; i < exec->segments_no; ++i) {

		//check if it's within the range of segment[i]
		if ((uintptr_t)info->si_addr >= exec->segments[i].vaddr &&
			(uintptr_t)info->si_addr <= exec->segments[i].vaddr + exec->segments[i].mem_size) {

			//the number of page where this address should be mapped
			ssize_t page_no = ((uintptr_t)info->si_addr - exec->segments[i].vaddr) / PAGE_SIZE;

			//check if this page number was already mapped
			if(!check_mapped_addr((uintptr_t)info->si_addr, i, page_no, exec->segments[i].data)) {

				int map_size;
				if (exec->segments[i].vaddr + (page_no+1)*PAGE_SIZE > exec->segments[i].vaddr + exec->segments[i].mem_size) {
					map_size = exec->segments[i].mem_size % PAGE_SIZE;
				} else {
					map_size = PAGE_SIZE;
				}
				//the page was not allocated yet, so we use mmap to allocate it
				void * mapped_mem = mmap((void*)exec->segments[i].vaddr + page_no * PAGE_SIZE,
										map_size,
										PROT_READ | PROT_WRITE,
										MAP_ANONYMOUS | MAP_PRIVATE | MAP_FIXED, -1, 0);
				
				if (mapped_mem == MAP_FAILED) {
					printf("ERROR!! Can't map page");
				}

				//add address to data
				add_element((uintptr_t)info->si_addr, exec->segments[i].data);

				int copy_size;
				
				//if the page is in segment but it is further than file_size

				if (exec->segments[i].vaddr + (page_no+1)*PAGE_SIZE > exec->segments[i].vaddr + exec->segments[i].file_size) {
					copy_size = exec->segments[i].file_size % PAGE_SIZE;
				} else {
					copy_size = PAGE_SIZE;
				}
					
			
				memcpy(mapped_mem, mapped_file + exec->segments[i].offset + page_no*PAGE_SIZE, copy_size);

				int check_protect = mprotect(mapped_mem, copy_size, exec->segments[i].perm);

				if (check_protect == -1) {
					printf("mprotect failed");
				}
				return;
			} else {
				call_default_handler.sa_sigaction(signum, info, context);
			}
		}
	}
	call_default_handler.sa_sigaction(signum, info, context);
    return;
}

int so_init_loader(void)
{
	int rc;
	struct sigaction sa;

	memset(&sa, 0, sizeof(sa));
	sa.sa_sigaction = segv_handler;
	sa.sa_flags = SA_SIGINFO;
	rc = sigaction(SIGSEGV, &sa, &call_default_handler);
	if (rc < 0) {
		perror("sigaction");
		return -1;
	}
	return 0;
}

int so_execute(char *path, char *argv[])
{
	exec = so_parse_exec(path);
	if (!exec)
		return -1;

	fd = open(path, O_APPEND);
	bin_size = size_of_file();
	lseek(fd, 0, SEEK_CUR);
	mapped_file = mmap(0, bin_size, PROT_READ, MAP_PRIVATE, fd, 0);

	//create the data section for each segment
	for (int i = 0; i < exec->segments_no; ++i) {
		exec->segments[i].data = malloc(sizeof(mapped_addresses));
		mapped_addresses* data = exec->segments[i].data;

		data->addr_array = malloc(sizeof(uintptr_t));
		data->capacity = 1;
		data->size = 0;

	}

	//open the bin file from "path"
 	
	so_start_exec(exec, argv);

	close(fd);

	return -1;
}