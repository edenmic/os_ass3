#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

void print_size(char* label, int pid) {
  // uint64 size = (uint64)sbrk(0); // sbrk returns current break, which is effectively the size
  uint64 current_break = (uint64)sbrk(0); // sbrk(0) returns current program break
  printf("%s (pid %d): current break (size) = %d (0x%x)\n", label, pid, current_break, current_break);
}

int main(int argc, char *argv[])
{
  int disable_unmap = 0;
  if(argc > 1 && strcmp(argv[1], "-d") == 0)
    disable_unmap = 1;
  
  // Allocate memory to share in parent
  char *buf_parent = malloc(4096);
  if(buf_parent == 0){
    printf("Parent: malloc failed\n");
    exit(1);
  }
  strcpy(buf_parent, "Initial value");
  
  int parent_pid = getpid();
  print_size("Parent before fork", parent_pid);
  
  int child_pid = fork();
  if(child_pid < 0) {
    printf("fork failed\n");
    exit(1);
  }
  
  if(child_pid == 0) {
    // Child process
    sleep(2); // Give parent time to print first
    print_size("Child before mapping", getpid());
    
    // Map shared memory from parent
    // We pass parent_pid, the virtual address in parent (buf_parent), and size
    uint64 shared_addr_child = (uint64)map_shared_pages(parent_pid, buf_parent, 4096);
    
    // // Print the address returned by map_shared_pages for debugging
    // printf("Child: map_shared_pages returned: 0x%x\n", shared_addr_child);

    if(shared_addr_child == 0) { // Assuming 0 is error/failure
      printf("Child: map_shared_pages failed\n");
      exit(1);
    }
    
    print_size("Child after mapping", getpid());
    
    char *shared_buf_child = (char*)shared_addr_child;
    printf("Child: attempting to read from shared memory at 0x%x\n", shared_buf_child);
    printf("Child: shared memory initially contains: '%s'\n", shared_buf_child); // First access (read)
    
    strcpy(shared_buf_child, "Hello daddy"); // Second access (write)
    printf("Child: wrote 'Hello daddy' to shared memory\n");

    //for debug
    // printf("Child: shared memory address = 0x%x\n", shared_addr_child);
    // for(int i = 0; i < 10; i++) {
    //   printf("Child: byte at offset %d = %d\n", i, shared_buf_child[i]);
    // }
    
    if(!disable_unmap) {
      printf("Child: attempting to unmap shared memory at 0x%x\n", shared_buf_child);
      if(unmap_shared_pages((void*)shared_addr_child, 4096) != 0) {
        printf("Child: unmap_shared_pages failed\n");
        exit(1);
      }
      print_size("Child after unmapping", getpid());
      
      char *new_buf_child = malloc(2048);
      if(new_buf_child == 0){
        printf("Child: malloc after unmap failed\n");
        exit(1);
      }
      strcpy(new_buf_child, "New child buffer works");
      print_size("Child after malloc", getpid());
      printf("Child: wrote to new buffer: '%s'\n", new_buf_child);
      free(new_buf_child);
    } else {
      printf("Child: skipping unmap as requested\n");
    }
    
    exit(0);
  } else {
    // Parent process
    // Wait for child to complete its operations BEFORE accessing shared memory
    // The sleep(10) was a workaround; wait() is more robust.
    printf("Parent: waiting for child (pid %d) to finish...\n", child_pid);
    wait(0); 
    
    printf("Parent: child finished. Accessing shared memory...\n");
    printf("Parent: shared memory now contains: '%s'\n", buf_parent); // buf_parent is in parent's address space
    
    free(buf_parent);
    print_size("Parent after child exit and free", getpid());
    
    exit(0);
  }
}