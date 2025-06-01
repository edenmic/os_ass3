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

  int children[4]; // Array to store child PIDs
  int child_count = 0;

  // Create 4 children
  for(int i = 0; i < 4; i++) {
    int child_pid = fork();
    
    if(child_pid < 0) {
      printf("fork failed at child %d\n", i);
      exit(1);
    }
    
    if(child_pid == 0) {
      // Child process (with identifier i)
      printf("Child %d (pid %d): started\n", i, getpid());
      print_size("Child before mapping", getpid());
      
      // Map shared memory from parent
      uint64 shared_addr_child = (uint64)map_shared_pages(parent_pid, buf_parent, 4096);
      
      printf("Child %d: map_shared_pages returned: 0x%x\n", i, shared_addr_child);

      if(shared_addr_child == 0) {
        printf("Child %d: map_shared_pages failed\n", i);
        exit(1);
      }
      
      print_size("Child after mapping", getpid());
      
      char *shared_buf_child = (char*)shared_addr_child;
      printf("Child %d: reading from shared memory at 0x%x\n", i, shared_buf_child);
      printf("Child %d: shared memory contains: '%s'\n", i, shared_buf_child);
      
      // Each child writes a different message
      char message[32];
      strcpy(message, "Hello from child ");
      message[16] = '0' + i;  // Convert i to a character
      message[17] = '\0';     // Null-terminate the string
      strcpy(shared_buf_child, message);
      printf("Child %d: wrote '%s' to shared memory\n", i, message);
      
      // Have each child sleep for a different time to demonstrate they're running in parallel
      sleep(i * 10);
      
      if(!disable_unmap) {
        printf("Child %d: unmapping shared memory\n", i);
        if(unmap_shared_pages((void*)shared_addr_child, 4096) != 0) {
          printf("Child %d: unmap_shared_pages failed\n", i);
          exit(1);
        }
        
        print_size("Child after unmapping", getpid());
      } else {
        printf("Child %d: skipping unmap as requested\n", i);
      }
      
      exit(i); // Exit with child number as status
    } else {
      // Parent process - remember child pid
      children[child_count++] = child_pid;
      printf("Parent: created child %d (pid %d)\n", i, child_pid);
    }
  }

  // Parent waits for all children
  if(child_count > 0) {
    printf("Parent: waiting for %d children to finish...\n", child_count);
    
    // Use explicit array indexes when waiting for children
    for(int i = 0; i < child_count; i++) {
      int status;
      int child_pid = wait(&status);
      printf("Parent: child with pid %d (original child[%d]=%d) finished with status %d\n", 
             child_pid, i, children[i], status);
    }
    
    printf("Parent: all children finished. Accessing shared memory...\n");
    printf("Parent: shared memory final content: '%s'\n", buf_parent);
    
    free(buf_parent);
    print_size("Parent after all children exit and free", getpid());
  }

  return 0;
}