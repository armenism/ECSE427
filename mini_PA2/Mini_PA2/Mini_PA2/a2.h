//
//  a2.h
//  Mini_PA2
//
//  Created by Armen Stepanians on 2017-03-10.
//  Copyright © 2017 Armen Stepanians. All rights reserved.
//

#ifndef a2_h
#define a2_h

#include <errno.h>
#include <stdio.h> 
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/mman.h>
#include <sys/stat.h>        /* For mode constants */
#include <fcntl.h>           /* For O_* constants */

#define NUMBER_OF_PODS 16
#define POD_SIZE 256
#define POD_LENGTH (KEY_MAX_LENGTH + VALUE_MAX_LENGTH)
#define CONTROL_SIZE (4 * NUMBER_OF_PODS - 1)
#define DATA_SIZE (POD_LENGTH * POD_SIZE * NUMBER_OF_PODS)

struct database {
    void *db_addr;
    char *db_name;
    size_t storage_size;
};

struct database a2_database;

int  kv_store_create(const char *name);
int  kv_store_write(const char *key, const char *value);
char *kv_store_read(const char *key);
char **kv_store_read_all(const char *key);
int  kv_delete_db();

//void generate_string(char buf[], int length){
//    int type;
//    for(int i = 0; i < length; i++){
//        type = rand() % 3;
//        if(type == 2)
//            buf[i] = rand() % 26 + 'a';
//        else if(type == 1)
//            buf[i] = rand() % 10 + '0';
//        else
//            buf[i] = rand() % 26 + 'A';
//    }
//    buf[length - 1] = '\0';
//}

#endif /* a2_h */
