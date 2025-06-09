#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

#define BUFFER_SIZE 4096  // One page size
#define NUM_CHILDREN 16    // Number of child processes
#define MAX_MSG_LEN 100   // Maximum message length

// Define log message header structure
// Header is 32-bit: 16 bits for child index, 16 bits for message length
typedef struct {
  uint16 child_index;
  uint16 msg_length;
} __attribute__((packed)) LogHeader;

// Helper function to advance to next aligned header position
uint64 next_aligned_addr(uint64 addr) {
  return (addr + 3) & ~3;  // Align to 4-byte boundary
}

// Function to print buffer size
void print_size(char* label, int pid) {
  uint64 current_break = (uint64)sbrk(0);
  printf("%s (pid %d): current break (size) = %d (0x%x)\n", 
         label, pid, current_break, current_break);
}

int main(int argc, char *argv[]) {
  // Allocate shared memory buffer in parent
  char *buf_parent = malloc(BUFFER_SIZE);
  if(buf_parent == 0) {
    printf("Parent: malloc failed\n");
    exit(1);
  }
  
  // Initialize buffer with zeros
  memset(buf_parent, 0, BUFFER_SIZE);
  
  int parent_pid = getpid();
  print_size("Parent before fork", parent_pid);
  
  int child_pids[NUM_CHILDREN];
  
  // Create child processes
  for(int i = 0; i < NUM_CHILDREN; i++) {
    int pid = fork();
    
    if(pid < 0) {
      printf("Fork failed\n");
      exit(1);
    }
    
    if(pid == 0) {
      // Child process
      // print_size("Child before mapping", getpid());  // Comment out
      
      // Map shared memory from parent
      uint64 shared_addr_child = (uint64)map_shared_pages(parent_pid, buf_parent, BUFFER_SIZE);
      
      // Print the address returned by map_shared_pages
      // printf("Child %d: map_shared_pages returned: 0x%x\n", i, shared_addr_child);  // Comment out
      
      if(shared_addr_child == 0) {
        // printf("Child %d: map_shared_pages failed\n", i);  // Comment out
        exit(1);
      }
      
      // print_size("Child after mapping", getpid());  // Comment out
      
      // Generate log messages - more for the last child to ensure buffer overflow testing
      int num_messages = (i == NUM_CHILDREN-1) ? 1000 : 10;
      
      // Access shared memory
      char *shared_buf = (char*)shared_addr_child;
      uint64 write_pos = 0;
      
      // Write messages to the shared buffer
      for(int msg = 0; msg < num_messages; msg++) {
        // Skip if we're near the end of buffer
        if(write_pos + sizeof(LogHeader) + MAX_MSG_LEN >= BUFFER_SIZE) {
          // printf("Child %d: buffer full, stopping\n", i);  // Comment out
          break;
        }
        
        // Prepare message
        char message[MAX_MSG_LEN];
        int len = 0;
        char num_buf[16];

        // Add "Message "
        const char *prefix = "Message ";
        for (int j = 0; prefix[j]; j++) {
            message[len++] = prefix[j];
        }

        // Add message number
        int tmp = msg;
        int num_len = 0;
        if (tmp == 0) {
            num_buf[num_len++] = '0';
        } else {
            while (tmp > 0) {
                num_buf[num_len++] = '0' + (tmp % 10);
                tmp /= 10;
            }
        }
        // Reverse digits
        for (int j = num_len - 1; j >= 0; j--) {
            message[len++] = num_buf[j];
        }

        // Add " from child "
        const char *middle = " from child ";
        for (int j = 0; middle[j]; j++) {
            message[len++] = middle[j];
        }

        // Add child number
        tmp = i;
        num_len = 0;
        if (tmp == 0) {
            num_buf[num_len++] = '0';
        } else {
            while (tmp > 0) {
                num_buf[num_len++] = '0' + (tmp % 10);
                tmp /= 10;
            }
        }
        // Reverse digits
        for (int j = num_len - 1; j >= 0; j--) {
            message[len++] = num_buf[j];
        }

        message[len] = '\0';
        
        // Create header with child index and message length
        uint32 header = (i & 0xFFFF) | ((len & 0xFFFF) << 16); 
        
        // Find a free slot for the message
        int attempts = 0;
        int success = 0;
        
        while(!success && attempts < 100) {
          // Check if position is aligned
          if(write_pos % 4 != 0) {
            write_pos = next_aligned_addr(write_pos);
          }
          
          // Skip if we're near the end of buffer
          if(write_pos + sizeof(LogHeader) + len >= BUFFER_SIZE) {
            // printf("Child %d: reached end of buffer\n", i);  // Comment out
            break;
          }
          
          // Try to atomically write header
          uint32 *header_ptr = (uint32*)(shared_buf + write_pos);
          uint32 old_val = __sync_val_compare_and_swap(header_ptr, 0, header);
          
          if(old_val == 0) {
            // Successfully claimed this slot
            // Write message after header
            memcpy(shared_buf + write_pos + sizeof(LogHeader), message, len);
            
            // Move to next potential position
            write_pos += sizeof(LogHeader) + len;
            write_pos = next_aligned_addr(write_pos);
            success = 1;
          } else {
            // Slot already taken, skip this header and message
            uint16 msg_len = (old_val >> 16) & 0xFFFF;
            write_pos += sizeof(LogHeader) + msg_len;
            write_pos = next_aligned_addr(write_pos);
          }
          
          attempts++;
        }
        
        // Remove the sleep to speed up execution
        // sleep(1);  // Comment out
      }
      
      // printf("Child %d: finished writing messages\n", i);  // Comment out
      exit(0);
    } else {
      // Parent process - store child PID
      child_pids[i] = pid;
      printf("Parent: created child %d with pid %d\n", i, pid);
    }
  }
  
  // Parent waits for all children to finish
  for(int i = 0; i < NUM_CHILDREN; i++) {
    printf("Parent: waiting for child %d (pid %d) to finish...\n", i, child_pids[i]);
    wait(0);
  }
  
  printf("Parent: all children finished. Reading messages from buffer...\n");
  
  // Read messages from the buffer
  uint64 read_pos = 0;
  int msg_count = 0;
  
  while(read_pos + sizeof(LogHeader) <= BUFFER_SIZE) {
    // Ensure read position is aligned
    if(read_pos % 4 != 0) {
      read_pos = next_aligned_addr(read_pos);
    }
    
    uint32 *header_ptr = (uint32*)(buf_parent + read_pos);
    uint32 header = *header_ptr;
    
    if(header == 0) {
      // Empty slot, move to next header
      read_pos += sizeof(LogHeader);
      continue;
    }
    
    uint16 child_index = header & 0xFFFF;
    uint16 msg_length = (header >> 16) & 0xFFFF;
    
    // Ensure we don't read past buffer
    if(read_pos + sizeof(LogHeader) + msg_length > BUFFER_SIZE) {
      printf("Parent: message would exceed buffer boundary, stopping\n");
      break;
    }
    
    // Temporary buffer to hold the message plus null terminator
    char temp_msg[MAX_MSG_LEN + 1];
    if(msg_length > MAX_MSG_LEN) {
      printf("Parent: message length %d exceeds max %d, skipping\n", msg_length, MAX_MSG_LEN);
      read_pos += sizeof(LogHeader) + msg_length;
      read_pos = next_aligned_addr(read_pos);
      continue;
    }
    
    // Copy message to temp buffer and add null terminator
    memcpy(temp_msg, buf_parent + read_pos + sizeof(LogHeader), msg_length);
    temp_msg[msg_length] = '\0';
    
    // Print the message
    printf("Parent: Message %d - From child %d: %s\n", msg_count++, child_index, temp_msg);
    
    // Move to next header
    read_pos += sizeof(LogHeader) + msg_length;
    read_pos = next_aligned_addr(read_pos);
  }
  
  printf("Parent: finished reading messages, total count: %d\n", msg_count);
  
  // Free the buffer
  free(buf_parent);
  print_size("Parent after reading and free", getpid());
  
  exit(0);
}