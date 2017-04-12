//  Armen Stepanians 260586139
//  a2.c
//  Mini_PA2
//
//  Created by Armen Stepanians on 2017-03-10.
//  Copyright © 2017 Armen Stepanians. All rights reserved.
//

#include "a2.h"
#include "config.h"

// if character is not alphanumeric returns -1
int  check_key_validity(char first_char) {
    if (first_char < 48 || (first_char > 57 && first_char < 65) ||
        (first_char > 90 && first_char < 97) || first_char > 122)
        return -1;
    else return 0;
}

// returns a normalized value between 0 and 61 used for hashing
int storage_normalized_value(char first_char) {
    if (first_char >= 48 && first_char <= 57) return first_char % 48;
    else if (first_char >= 65 && first_char <= 90) return (first_char % 65) + 10;
    else return (first_char % 97) + 26;
}

// find the proper storage pod with respect to normalized hash value
int find_proper_pod(int normalized_hash_value) {
    int segment = 61 / NUMBER_OF_PODS;
    
    if (segment == 0) {
        return normalized_hash_value;
    } else {
        if ((normalized_hash_value / segment) == NUMBER_OF_PODS) {
            return NUMBER_OF_PODS - 1;
        } else return normalized_hash_value / segment;
    }
}

int  kv_store_create(const char *name) {
    
    a2_database.db_name = malloc(strlen(name));
    
    strcpy(a2_database.db_name, name);
    // create the shared memory obj and get its file descriptor
    int shm_fd = shm_open(a2_database.db_name, O_CREAT | O_RDWR, S_IRWXU);
    
    if (shm_fd < 0) printf("Error %d happened upon creating shared memory object", errno);
    
    // adjust file size to a fixed one, get pointer to start
    a2_database.storage_size = DATA_SIZE + CONTROL_SIZE;
    ftruncate(shm_fd, a2_database.storage_size);
    
    a2_database.db_addr = (char *)mmap(NULL,
                                       a2_database.storage_size,
                                       PROT_READ | PROT_WRITE,
                                       MAP_SHARED, shm_fd, 0);
    if (a2_database.db_addr == MAP_FAILED) {
        return -1;
    }
    
    // initialize all values to 0
    memset(a2_database.db_addr, '\0', a2_database.storage_size);
    
    // set all pod reads and writes indeces to 0
    memset(a2_database.db_addr + DATA_SIZE, 0, CONTROL_SIZE);
    
    close(shm_fd);
    
    
    printf("database created at address %s\n", a2_database.db_addr);
    return 0;
    
}

int kv_store_write(const char *key, const char *value) {
    
    if (strlen(key) > KEY_MAX_LENGTH || strlen(value) > VALUE_MAX_LENGTH) exit(1);
    
    // create the shared memory obj and get its file descriptor
    char *buf = malloc(1);
//    int shm_fd = shm_open(a2_database.db_name, O_RDWR, 0);
    int storage_pod, wr_pointer;
    
//    if (shm_fd < 0) printf("Error %d happened upon openning shared memory object", errno);
    
    // get pod number and start writing
    if (check_key_validity(key[0]) == 0) {
        storage_pod = find_proper_pod(storage_normalized_value(key[0]));
        wr_pointer = atoi(&a2_database.db_addr[DATA_SIZE + 4 * storage_pod]);
    }
    else exit(1);
    
    // insert key
    memcpy(&a2_database.db_addr[wr_pointer * POD_LENGTH
                                + storage_pod * POD_LENGTH * POD_SIZE],
           key,
           strlen(key));
    
    // insert value
    memcpy(&a2_database.db_addr[(wr_pointer * POD_LENGTH) + KEY_MAX_LENGTH
                                + storage_pod * POD_LENGTH * POD_SIZE],
           value,
           strlen(value));
    
    // update writer pointer value
    wr_pointer++;
    wr_pointer %= POD_SIZE;
    sprintf(buf, "%d", wr_pointer);
    memcpy(&a2_database.db_addr[DATA_SIZE + 4 * storage_pod],
           buf,
           strlen(buf));
    
    free(buf);
//    close(shm_fd);
    
    return 0;
}

char *kv_store_read(const char *key) {

    char *buf = malloc(1);
//    int shm_fd = shm_open(a2_database.db_name, O_RDWR, 0);
    int cycle_complete_tracker = 0, storage_pod = 0, rd_pointer;
    
//    if (shm_fd < 0) printf("Error %d happened upon openning shared memory object", errno);
    
    // define variables
    // get address and start reading from the address plus offset
    char *value = "\0";
    
    // get pod number and start writing
    if (check_key_validity(key[0]) == 0) {
        storage_pod = find_proper_pod(storage_normalized_value(key[0]));
        rd_pointer = atoi(&a2_database.db_addr[DATA_SIZE + 4 * storage_pod + 2]);
    }
    
    while (rd_pointer < POD_SIZE) {
        if (strcmp(&a2_database.db_addr[POD_LENGTH * rd_pointer +
                                        storage_pod * POD_LENGTH * POD_SIZE], key) == 0) {
            value = a2_database.db_addr +
                    POD_LENGTH * rd_pointer + KEY_MAX_LENGTH +
                    storage_pod * POD_LENGTH * POD_SIZE;
        }
        rd_pointer++;
        rd_pointer %= POD_SIZE;
        
        // if value changed means it's been found, so return it
        if (strcmp(value, "\0") != 0) {
//            close(shm_fd);
            // update reader pointer value
            sprintf(buf, "%d", rd_pointer);
            memcpy(&a2_database.db_addr[DATA_SIZE + 4 * storage_pod + 2],
                   buf,
                   strlen(buf));
            
            free(buf);
            return value;
        }
        
        if (cycle_complete_tracker++ == POD_SIZE) {
            // means we've gone through the entire pod without finding the specified key
//            close(shm_fd);
            break;
        }
    }
    return NULL;
}

char **kv_store_read_all(const char *key) {
    
    char **values, *value = "\0", *tmp = NULL;
    values = malloc(POD_SIZE);
    int counter = 0;
    
    // get the first if there are any
    tmp = kv_store_read(key);
    
    if (tmp != NULL) {
        values[0] = tmp;
        printf("%d item collected\n", counter + 1);
        // the following strcmp makes sure we are not collecting the
        // same date all over again
        while (strcmp(values[0], value = kv_store_read(key)) != 0) {
            values[++counter] = value;
            printf("%d items collected\n", counter + 1);
        }
        char **buffer = values;
        free(values);
        return buffer;
    }
    else return NULL;
}

int kv_delete_db() {
    
    a2_database.storage_size = DATA_SIZE + CONTROL_SIZE;
    
    if (munmap(a2_database.db_addr, a2_database.storage_size) == 0) {
        shm_unlink(a2_database.db_name);
        return 0;}
    else return errno;
}
