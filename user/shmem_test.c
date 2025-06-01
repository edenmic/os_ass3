#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

void print_size(char* label, int pid) {
  int size = (int)sbrk(0);
  printf("%s (pid %d): size = %d\n", label, pid, size);
}

int main(int argc, char *argv[])
{
  int disable_unmap = 0;
  if(argc > 1 && strcmp(argv[1], "-d") == 0)
    disable_unmap = 1;
  
  // Allocate memory to share
  char *buf = malloc(4096);
  strcpy(buf, "Initial value");
  
  print_size("Parent before fork", getpid());
  
  int pid = fork();
  if(pid < 0) {
    printf("fork failed\n");
    exit(1);
  }
  
  if(pid == 0) {
    // Child process
    print_size("Child before mapping", getpid());
    
    // Map shared memory from parent
    uint64 shared_addr = map_shared_pages(getppid(), buf, 4096);
    if(shared_addr == 0) {
      printf("map_shared_pages failed\n");
      exit(1);
    }
    
    print_size("Child after mapping", getpid());
    
    // Access the shared memory
    char *shared_buf = (char*)shared_addr;
    printf("Child: shared memory contains: %s\n", shared_buf);
    
    // Modify the shared memory
    strcpy(shared_buf, "Hello daddy");
    printf("Child: wrote to shared memory\n");
    
    if(!disable_unmap) {
      // Unmap the shared memory
      if(unmap_shared_pages(shared_buf, 4096) != 0) {
        printf("unmap_shared_pages failed\n");
        exit(1);
      }
      print_size("Child after unmapping", getpid());
      
      // Allocate memory with malloc to show it works
      char *new_buf = malloc(2048);
      strcpy(new_buf, "New buffer works");
      print_size("Child after malloc", getpid());
      printf("Child: wrote to new buffer: %s\n", new_buf);
      free(new_buf);
    } else {
      printf("Child: skipping unmap as requested\n");
    }
    
    exit(0);
  } else {
    // Parent process
    sleep(10);  // Wait for child to complete its operations
    
    // Access the shared memory after child modified it
    printf("Parent: shared memory contains: %s\n", buf);
    
    wait(0);
    free(buf);
    print_size("Parent after free", getpid());
    
    exit(0);
  }
}