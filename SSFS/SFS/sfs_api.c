// Armen Stepanians 260586139

#include "sfs_api.h"

#define MAXFILENAME 20
// Magic number for file system
#define NUMERO_MAGIQUE 0x777DEF
// The maximum open files is also the size of the file descriptor table
#define MAX_OPEN_FILES 50
// Blocks assigned to:
// 20 inode blocks
// 1 data bitmap
// 1 inode bitmap
// 1 superblock
#define FS_BLOCKS 32
// Size per block
#define BLOCK_SIZE 1024
// Amount of data blocks to use
// Also the amount of files we can have in the file system
#define BLOCKS_FOR_DATA 200
// Number for the ROOT inode
#define INODE_AT_ROOT 0
// Flags used to mark blocks in bitmaps
#define IS_USED 0
#define IS_FREE 1
// Total blocks allcated for disk
#define TOTAL_BLOCKS BLOCKS_FOR_DATA + FS_BLOCKS
// Value for default instantiation
#define UNDEF_TEMP_VAL 0
// Direct pointers
#define DIR_PTR_COUNT 12
// Error value
#define INIT_VAL -1
// Diskname string
#define FSDISKNAME "SFSDISK"
#define OFFSET 1
#define ERROR -1

//---------------------------------------------------------------------------------------
//DATA STRUCTURE DEFINITIONS

// Super block def
typedef struct Sup_Block_def
{
    int magic_number;                   // magic number
    int block_size;                     // size of each block ( in bytes )
    int block_count;                    // total blocks used for data and meta info
    int data_block_count;               // total blocks for data
    int inode_table_size;               // blocks used for the inode table
    int inode_table_location;           // defines in blocks where the inode table is situated on the disk
    int root_inode_number;              // the number of the inode allocated for root
    
} Sup_Blk;

// INode entry def
typedef struct INode_def
{
    int direct_pointers[DIR_PTR_COUNT]; // array of direct pointers (12)
    int indirect_pointer;               // indirect pointer
    int inode_count;                    // number of inodes for inode table
    int inode_size;                     // size of inode in bytes
    bool is_directory;                  // defines if the inode is the directory inode or not
} INode ;

// File Descriptor entry def
typedef struct File_Descriptor_def
{
    int state;                          // FREE OR USED
    int inode_number;                   // inode number (i)
    INode* inode;                       // inode belonging to the file
    int read_pointer;                   // accessable read pointer
    int write_pointer;                  // accessable write pointer
//    int read_write_pointer;              // accessable read/write pointer
    
} File_Descriptor;

// Directory mapping def
typedef struct Dir_Map_def
{
    int inode_num;                      // inode number (i)
    char file_name[MAXFILENAME+1];      // name of file in chars
    
} Dir_Map;

File_Descriptor file_descr_table[MAX_OPEN_FILES]; // initialize the file descriptor table
INode inode_tbl[BLOCKS_FOR_DATA];       // initlaize the inode table
Dir_Map dir_map_tbl[BLOCKS_FOR_DATA];   // initialize the directory map table


int directory_loc =0;                   // set directory location to 0
int inode_bmap[BLOCKS_FOR_DATA];        // initialize the inode bitmap
int data_bmap[BLOCKS_FOR_DATA];         // initialize the data bitmap

// setting the needed quantities to temporary undefined value. Will change if used.
int inode_bmap_nb_of_blocks = UNDEF_TEMP_VAL;	//  #blocks for inode bitmap
int inode_bmap_on_disk = UNDEF_TEMP_VAL;		//  location of the inode bitmap (blk number), on disk
int dir_table_nb_of_blocks = UNDEF_TEMP_VAL; 	//  #blocks for directory mapping
int data_block_first  = UNDEF_TEMP_VAL;         //  data section's first block location
int inode_tbl_nb_of_blocks = UNDEF_TEMP_VAL; 	//  #blocks for inode table
int inode_tbl_on_disk = UNDEF_TEMP_VAL;         //  location of the inode table (blk number), on disk
int data_bmap_nb_of_blocks = UNDEF_TEMP_VAL;	//  #blocks for data bitmap
int data_bmap_on_disk = UNDEF_TEMP_VAL;         //  location of the data bitmap (blk number), on disk
char *inode_tbl_pointer;                        //  pointer to inode table

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
//HELPER FUNCTIONS

// Gets a free data block based on the data blocks statuses in the data bitmap.
// Specifically, returns the number of the free block.
int get_free_data_block()
{
    int i;
    
    for (i=0;i < BLOCKS_FOR_DATA; i++)
    {
        if (data_bmap[i]==IS_FREE)
        {
            data_bmap[i] = IS_USED;
            write_blocks(inode_bmap_nb_of_blocks + OFFSET, data_bmap_nb_of_blocks , data_bmap);
            return i;
        }
    }
    
    return -1;
}

// Setting the inderect pointer
int set_indirect_pointer()
{
    int freeblk = get_free_data_block();
    
    if (freeblk < 0)
    {
        return ERROR;
    }
    
    int i;
    
    // allocating memory for buffer
    int *indirect_pointer_buffer = malloc(BLOCK_SIZE);
    
    for (i = 0; i < (BLOCK_SIZE/sizeof(int)); i++)
    {
        indirect_pointer_buffer[i] = UNDEF_TEMP_VAL;
    }
    write_blocks(freeblk + data_block_first, 1, indirect_pointer_buffer);
    
    // freeing the buffer
    free(indirect_pointer_buffer);
    
    // a free block is found. Return it's number
    return freeblk;
}

// Freeing used data block at specified number due to deletion.
int set_data_block_free(int block)
{
    data_bmap[block] = IS_FREE;
    
    write_blocks(inode_bmap_nb_of_blocks + OFFSET, data_bmap_nb_of_blocks , data_bmap);
    
    return 0;
}

// Defines sizes to different data structures such as the inode table, inode bitmap, data bitmap and directory table map.
// Rounding up***OFFSET
void set_sizes_in_blocks()
{
    inode_tbl_nb_of_blocks =  OFFSET + (sizeof(inode_tbl) / BLOCK_SIZE);
    inode_bmap_nb_of_blocks = OFFSET + (sizeof(inode_bmap) / BLOCK_SIZE);
    data_bmap_nb_of_blocks = OFFSET + (sizeof(data_bmap) / BLOCK_SIZE);
    dir_table_nb_of_blocks = OFFSET + (sizeof(dir_map_tbl) / BLOCK_SIZE);
    data_block_first = inode_tbl_nb_of_blocks + inode_bmap_nb_of_blocks + data_bmap_nb_of_blocks + 10;
}

// Initializes the super block attributes for the file system.
void setup_super_block(Sup_Blk super_blk)
{
    super_blk.magic_number = NUMERO_MAGIQUE;
    super_blk.block_count = TOTAL_BLOCKS;
    super_blk.data_block_count = BLOCKS_FOR_DATA;
    super_blk.block_size = BLOCK_SIZE;
    super_blk.inode_table_location = inode_bmap_nb_of_blocks + data_bmap_nb_of_blocks + OFFSET;
    super_blk.inode_table_size = inode_tbl_nb_of_blocks;
    super_blk.root_inode_number = INODE_AT_ROOT;
}

// Sets up data and inode bitmaps
void setup_bitmaps()
{
    int i;
    for (i = 0; i < BLOCKS_FOR_DATA; i++ )
    {
        data_bmap[i] = IS_FREE;
        inode_bmap[i] = IS_FREE;
        dir_map_tbl[i].inode_num = UNDEF_TEMP_VAL;
    }
}

// Sets up the inode table
void setup_inode_table()
{
    int j;
    
    for (j=0; j< BLOCKS_FOR_DATA ; j++)
    {
        inode_tbl[j].is_directory = INIT_VAL;
        inode_tbl[j].inode_size = 0;
        inode_tbl[j].inode_count = j;
        
        int k;
        
        for (k = 0; k < DIR_PTR_COUNT; k++)
        {
            inode_tbl[j].direct_pointers[k] = UNDEF_TEMP_VAL;
        }
        inode_tbl[j].indirect_pointer = UNDEF_TEMP_VAL;
    }
}

// Sets up the file descriptor table
void setup_file_descriptor_table()
{
    file_descr_table[0].state = IS_USED;
    file_descr_table[0].inode_number = INODE_AT_ROOT;
    file_descr_table[0].read_pointer = 0;
    file_descr_table[0].write_pointer = 0;
    file_descr_table[0].inode = & inode_tbl[0];
    file_descr_table[0].inode -> is_directory = true;
    file_descr_table[0].inode -> inode_size = sizeof(dir_map_tbl);
}


//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~API FUNCTIONS

//API: initializes the file system
void mkssfs(int fresh)
{
    
    // define sizes for each table and bitmap
    set_sizes_in_blocks();
    
    int j;
    
    // free file descriptor table and reset all inode numbers
    for (j=0; j < MAX_OPEN_FILES; j++)
    {
        file_descr_table[j].state = IS_FREE;
        
        file_descr_table[j].inode_number = UNDEF_TEMP_VAL;
    }
    
    Sup_Blk su_block_pointer;
    
    
    //FRESH START
    if (fresh)
    {
        init_fresh_disk(FSDISKNAME, BLOCK_SIZE, TOTAL_BLOCKS);
        printf("DEB: Initialized fresh disk: SFSDISK...\n");
        
        // SETTING UP ALL DATA STRUCTURES
        setup_super_block(su_block_pointer);
        printf("DEB: Initialized the Super Block...\n");
        
        setup_bitmaps();
        printf("DEB: Initialized inode and data bitmaps...\n");
        
        // set for root
        inode_bmap[0] = IS_USED;
        
        setup_inode_table();
        printf("DEB: Initialized inode table...\n");
        
        setup_file_descriptor_table();
        printf("DEB: Initialized file descriptor table...\n");
        
        // allocating first blocks to directory and setting them as used
        if (dir_table_nb_of_blocks <= DIR_PTR_COUNT)
        {
            int l;
            
            for(l=0; l < dir_table_nb_of_blocks; l++)
            {
                file_descr_table[0].inode -> direct_pointers[l]=data_block_first + l;
                data_bmap[l] = IS_USED;
            }
        }
        else
        {
            set_indirect_pointer();
        }
        
        // WRITE TO DISK!
        // need to typecast to void the buffer
        write_blocks(0, 1,  (void*) &su_block_pointer);
        write_blocks(1, inode_bmap_nb_of_blocks, inode_bmap);
        write_blocks(OFFSET + inode_bmap_nb_of_blocks , data_bmap_nb_of_blocks , data_bmap);
        int inodeTableLoc = OFFSET + inode_bmap_nb_of_blocks + data_bmap_nb_of_blocks;
        write_blocks(inodeTableLoc, inode_tbl_nb_of_blocks , inode_tbl);
        write_blocks(data_block_first, dir_table_nb_of_blocks, dir_map_tbl);
    }
    else
    {
        // IF NOT FRESH:
        // Initialized disk with same name and same size
        init_disk(FSDISKNAME, BLOCK_SIZE, TOTAL_BLOCKS);
        setup_super_block(su_block_pointer);
        printf("DEB: NON-FRESH SFS REQUESTED: Initialized the Super Block...\n");
        
        // ELSE if not fresh
        // Setup Disk
        init_disk(FSDISKNAME, BLOCK_SIZE, TOTAL_BLOCKS);
        
        // proceed reading blocks from the disk
        read_blocks(OFFSET, inode_bmap_nb_of_blocks, inode_bmap);
        read_blocks(OFFSET + inode_bmap_nb_of_blocks , data_bmap_nb_of_blocks , data_bmap);
        read_blocks(OFFSET + inode_bmap_nb_of_blocks + data_bmap_nb_of_blocks, inode_tbl_nb_of_blocks , inode_tbl);
        read_blocks(data_block_first, dir_table_nb_of_blocks, dir_map_tbl);
    }
}

// API: opens a file given the specific filename
int ssfs_fopen(char *filename)
{
    
    if(strlen(filename) > MAXFILENAME)
    {
        return ERROR;
    }
    
    int i;
    
    // searching the file in the directory mapping
    for(i=0; i < BLOCKS_FOR_DATA; i++)
    {
        if (strncmp(dir_map_tbl[i].file_name , filename , MAXFILENAME) == 0)
        {
            // file was found
            int inode_numr = dir_map_tbl[i].inode_num;
            
            // check if file was previously open
            int j;
            
            for (j= 0; j < MAX_OPEN_FILES; j++)
            {
                if(file_descr_table[j].inode_number == inode_numr)
                {
                    return j;
                }
            }
            
            // case if file wasn't open
            int k;
            
            for(k=0; k < MAX_OPEN_FILES; k++)
            {
                // search for free entry then assign attributes
                if (file_descr_table[k].state == IS_FREE)
                {
                    file_descr_table[k].inode_number = inode_numr;
                    file_descr_table[k].inode = &inode_tbl[inode_numr];
                    file_descr_table[k].state = IS_USED;
                    file_descr_table[k].read_pointer = inode_tbl[inode_numr].inode_size;
                    
                    return k;
                }
            }
            return ERROR;
        }
    }
    
    // If search above failed, create a file.
    int m;
    
    int inode_numr = UNDEF_TEMP_VAL;
    
    // Search for free block
    for(m=0; m < BLOCKS_FOR_DATA; m++)
    {
        if(inode_bmap[m] == IS_FREE)
        {
            inode_numr = m;
            inode_tbl[m].is_directory = false;
            inode_tbl[m].inode_size = 0;
            inode_bmap[m] =IS_USED;
            inode_numr = m;
            break;
        }
    }
    // no free block was found
    if (inode_numr == UNDEF_TEMP_VAL)
    {
        return ERROR;
    }
    
    // record the file in the directory mapping
    int n;
    for(n=0; n < BLOCKS_FOR_DATA; n++)
    {
        if(dir_map_tbl[n].inode_num == UNDEF_TEMP_VAL)
        {
            dir_map_tbl[n].inode_num =inode_numr;
            
            strncpy(dir_map_tbl[n].file_name, filename, MAXFILENAME + 1);
            
            break;
        }
    }
    
    // filling the entry in the file descriptor table
    int p;
    for(p=0; p < MAX_OPEN_FILES; p++)
    {
        if (file_descr_table[p].state == IS_FREE)
        {
            file_descr_table[p].inode_number = inode_numr;
            file_descr_table[p].inode = &inode_tbl[inode_numr];
            file_descr_table[p].state = IS_USED;
            file_descr_table[p].read_pointer = inode_tbl[inode_numr].inode_size;
            file_descr_table[p].write_pointer = inode_tbl[inode_numr].inode_size;
            
            // write to disk
            write_blocks(1, inode_bmap_nb_of_blocks, inode_bmap);
            int inodeTableLoc = OFFSET + inode_bmap_nb_of_blocks + data_bmap_nb_of_blocks;
            write_blocks(inodeTableLoc, inode_tbl_nb_of_blocks , inode_tbl);
            write_blocks(data_block_first, dir_table_nb_of_blocks, dir_map_tbl);
            
            return p;
        }
    }
    
    return ERROR;
}

//API: Read a file from sfs
int ssfs_fread(int fileID, char *buf, int length)
{
    // Trying to read a freed file --> bad
    if (fileID < 0 || file_descr_table[fileID].state == IS_FREE || length < 0)
    {
        return ERROR;
    }
    
    if(file_descr_table[fileID].read_pointer + length > file_descr_table[fileID].inode->inode_size)
    {
        // update the length of the file since bigger than the allowed size
        length = file_descr_table[fileID].inode->inode_size - file_descr_table[fileID].read_pointer;
    }
    
    if (length < 0) return ERROR;
    
    // boundaries for block operations
    int amount_of_blocks_to_read;
    int first_byte_position;
    int last_byte_position;
    
    // using read write pointers to get the source and destination blocks from the F.D.T.
    int source_block = file_descr_table[fileID].read_pointer/BLOCK_SIZE;
    int target_block = (file_descr_table[fileID].read_pointer + length)/BLOCK_SIZE;
    
    // using read write pointers to get the source and destination blocks from the F.D.T. (mod)
    int source_byte = file_descr_table[fileID].read_pointer%BLOCK_SIZE;
    int target_byte = (file_descr_table[fileID].read_pointer + length)%BLOCK_SIZE;
    
    int destination; // to return
    
    // allcoating memory for the buffer
    char *buff = malloc(length);
    
    // getting the pointer to inode from the F.D.T.
    INode *in_ptr = file_descr_table[fileID].inode;
    
    destination=0;
    
    int n;
    // range read from source to target blocks
    for (n=source_block; n <= target_block; n++)
    {
        // allocating memory for one block
        char *single_block_mem = malloc(BLOCK_SIZE);
        
        // getting the number of the blocks to read
        if(n < DIR_PTR_COUNT)
        {
            amount_of_blocks_to_read = in_ptr -> direct_pointers[n];
        }
        else
        {
            // allocating memory for the indirect pointers buffer
            int *indirect_pt_buff = malloc(BLOCK_SIZE);
            
            // read starting from data block into the buffer
            read_blocks(data_block_first + in_ptr->indirect_pointer, 1, indirect_pt_buff);
            
            //DONT FORGET TO OFFSET the 12 INDIRECT POINTERS!!!
            amount_of_blocks_to_read = indirect_pt_buff[n - DIR_PTR_COUNT];
            
            // clean
            free(indirect_pt_buff);
        }
        
        // read block
        read_blocks(data_block_first + amount_of_blocks_to_read, 1, single_block_mem);
        
        // get bytes from the single defined block that was just read from buffer
        // block reading boundaries:
        first_byte_position = 0;
        last_byte_position=BLOCK_SIZE;
        
        // if n is the source block, then first byte position to get is the source byte
        if(n==source_block)
        {
            first_byte_position=source_byte;
        }
        // idem
        if(n==target_block)
        {
            last_byte_position=target_byte;
        }
        
        int byte_range = last_byte_position-first_byte_position;
        
        //memory copy from all buffers
        memcpy(&buff[destination], &single_block_mem[first_byte_position], byte_range);
        
        // update the destination and return it
        destination = destination + byte_range;
        
        // clean
        free(single_block_mem);
    }
    
    // get all indo buf
    memcpy(buf,buff,length);
    
    //clean
    free(buff);
    
    // IMPORTANT: UPDATE READ WRITE POINTER (to end)
    file_descr_table[fileID].write_pointer = file_descr_table[fileID].write_pointer + length;
    
    return destination;
}

//API: Write a file in sfs (similar principle to read)
int ssfs_fwrite(int fileID, char *buf, int length){
    
    // as before, cannot read a freed file
    if (fileID < 0 || file_descr_table[fileID].state == IS_FREE || length < 0)
    {
        return ERROR;
    }
    
    // case if the file provided is a new file --> need free block
    if (file_descr_table[fileID].inode -> inode_size == 0)
    {
        file_descr_table[fileID].inode -> direct_pointers[0] = get_free_data_block();
    }
    
    // same principle as before:
    // using read write pointers to get the source and destination blocks from the F.D.T.
    int source_block = file_descr_table[fileID].read_pointer / BLOCK_SIZE;
    int target_block = (file_descr_table[fileID].read_pointer + length) / BLOCK_SIZE;
    
    // using read write pointers to get the source and destination bytes from the F.D.T. (mod)
    int source_byte = file_descr_table[fileID].read_pointer % BLOCK_SIZE;
    int target_byte = (file_descr_table[fileID].read_pointer + length) % BLOCK_SIZE;
    
    // define writing boundaries (just like reading)
    int amount_of_blocks_to_write;
    int first_byte_in_block;
    int last_byte_in_block;
    int destination;
    
    // add blocks
    INode *in_ptr = file_descr_table[fileID].inode;
    
    // again, range is between source and target blocks
    int i;
    for (i=source_block; i <= target_block; i++)
    {
        // case if direct pointers
        if (i < DIR_PTR_COUNT)
        {
            if(in_ptr -> direct_pointers[i] == UNDEF_TEMP_VAL)
            {
                // get free blocks from inode dir. ptrs
                in_ptr -> direct_pointers[i] = get_free_data_block();
                
                if (in_ptr -> direct_pointers[i] <0)
                {
                    return ERROR;
                }
            }
        }
        
        // case if indirect pointer
        else
        {
            if(in_ptr -> indirect_pointer == UNDEF_TEMP_VAL)
            {
                // get indirect ptr
                in_ptr -> indirect_pointer = set_indirect_pointer();
                
                if ( in_ptr -> indirect_pointer < 0)
                {
                    return ERROR;
                }
            }
            
            // case if block is non empty and contains values
            if(in_ptr -> indirect_pointer != UNDEF_TEMP_VAL)
            {
                // same principle as before
                int *indirect_pt_buff = malloc(BLOCK_SIZE);
                
                // reading blocks @ indir ptr onto the buffer
                read_blocks(data_block_first + in_ptr->indirect_pointer, 1, indirect_pt_buff);
                
                // if blocks are still not used (undefined) --> define blocks
                if (indirect_pt_buff[i - DIR_PTR_COUNT] == UNDEF_TEMP_VAL)
                {
                    indirect_pt_buff[i - DIR_PTR_COUNT] = get_free_data_block();
                    
                    if (indirect_pt_buff[i - DIR_PTR_COUNT] < 0)
                    {
                        return ERROR;
                    }
                    write_blocks(data_block_first + in_ptr->indirect_pointer, 1, indirect_pt_buff);
                }
                // free buff
                free(indirect_pt_buff);
            }
        }
    }
    
    destination=0;  //ptr to return
    
    // write blocks.
    for (i=source_block;i<=target_block;i++)
    {
        
        if(i < DIR_PTR_COUNT)
        {
            amount_of_blocks_to_write = in_ptr -> direct_pointers[i]; // got number of blocks to be written
        }
        else
        {
            // same analogy as before
            int *buff = malloc(BLOCK_SIZE);
            
            read_blocks(data_block_first + in_ptr->indirect_pointer, 1, buff);
            
            amount_of_blocks_to_write = buff[i - DIR_PTR_COUNT];
            
            // clean
            free(buff);
            
            if (amount_of_blocks_to_write == UNDEF_TEMP_VAL)
            {
                return ERROR;
            }
        }
        
        char *single_block_mem = malloc(BLOCK_SIZE);
        
        // block read from ptr
        read_blocks(data_block_first + amount_of_blocks_to_write, 1 ,single_block_mem);
        
        // mod whats in the memory since the block now is in it
        // as previously, find writing boundaries
        first_byte_in_block = 0;
        
        last_byte_in_block = BLOCK_SIZE;
        
        if(i == target_block)
        {
            last_byte_in_block=target_byte;
        }
        
        if(i == source_block)
        {
            first_byte_in_block = source_byte;
        }
        
        int byte_range = last_byte_in_block - first_byte_in_block;
        
        // final mod on buffer + write to disk (copy buff --> write to disk)
        memcpy(&single_block_mem[first_byte_in_block], &buf[destination], byte_range);
        
        write_blocks(data_block_first + amount_of_blocks_to_write, 1, single_block_mem);
        
        destination = destination + byte_range;
        
        // clean
        free(single_block_mem);
        
    }
    
    // UPDATE RW POINTER!!!
    in_ptr -> inode_size = (in_ptr->inode_size > file_descr_table[fileID].write_pointer+length) ? in_ptr->inode_size : file_descr_table[fileID].write_pointer + length;
    file_descr_table[fileID].write_pointer = file_descr_table[fileID].write_pointer +length;
    
    
    //inode table modified
    write_blocks(inode_bmap_nb_of_blocks + data_bmap_nb_of_blocks + OFFSET, inode_tbl_nb_of_blocks , inode_tbl);
    return destination;
}

// API: closes a file given a file name
int ssfs_fclose(int fileID)
{
    
    // can't close a freed file.
    if (fileID >= 0 && file_descr_table[fileID].state != IS_FREE)
    {
        file_descr_table[fileID].read_pointer=0;
        file_descr_table[fileID].write_pointer=0;
        file_descr_table[fileID].state = IS_FREE;
        file_descr_table[fileID].inode_number = UNDEF_TEMP_VAL;
        file_descr_table[fileID].inode = NULL;
        
        return 0;
    }
    else
    {
        return ERROR;
    }
}

// API: seek for a file
int sfs_fseek(int fileID, int loc)
{
    
    if (file_descr_table[fileID].state == IS_FREE)
    {
        return ERROR;
    }
    if (file_descr_table[fileID].inode -> inode_size < loc)
    {
        //file_descr_table[fileID].read_write_pointer = file_descr_table[fileID].inode -> inode_size ;
        
        return 0;
    }
    //file_descr_table[fileID].read_write_pointer = loc;
    
    return 0;
}

int ssfs_frseek(int fileID, int loc) {
    
    if (fileID < 0 || file_descr_table[fileID].state == IS_FREE || loc < 0 ||
        loc > file_descr_table[fileID].inode -> inode_size)
        return ERROR;
    
    file_descr_table[fileID].read_pointer = loc;
    return 0;
}

int ssfs_fwseek(int fileID, int loc) {
    
    if (fileID < 0 || file_descr_table[fileID].state == IS_FREE || loc < 0 ||
        loc > file_descr_table[fileID].inode -> inode_size)
        return ERROR;
    
    file_descr_table[fileID].write_pointer = loc;
    return 0;
}
// API: removing a file
int ssfs_remove(char *file)
{
    int r;
    int fd;
    
    for(r=0; r < BLOCKS_FOR_DATA; r++)
    {
        if(strncmp(dir_map_tbl[r].file_name, file, MAXFILENAME) == 0 && dir_map_tbl[r].inode_num != UNDEF_TEMP_VAL)
        {
            fd = ssfs_fopen(file);
            ssfs_fclose(fd);
            
            // clearing inode
            INode *inodeToClear = &inode_tbl[dir_map_tbl[r].inode_num];
            
            int s;
            for (s = 0; s < DIR_PTR_COUNT; s++)
            {
                if (inodeToClear->direct_pointers[s] != UNDEF_TEMP_VAL)
                {
                    set_data_block_free(inodeToClear -> direct_pointers[s]);
                    inodeToClear->direct_pointers[s] = UNDEF_TEMP_VAL;
                }
            }
            
            // if inode is not undefined
            if (inodeToClear->indirect_pointer != UNDEF_TEMP_VAL)
            {
                set_data_block_free(inodeToClear -> indirect_pointer);
                
                inodeToClear->indirect_pointer = UNDEF_TEMP_VAL;
            }
            
            // deal with size, directory and number
            inodeToClear -> is_directory = INIT_VAL;
            inodeToClear -> inode_size = 0;
            inode_bmap[dir_map_tbl[r].inode_num] = IS_FREE;
            
            // update directory
            strncpy(dir_map_tbl[r].file_name, "\0", MAXFILENAME + 1); //copy filename over and increment
            dir_map_tbl[r].inode_num = UNDEF_TEMP_VAL;
            
            // Write all to disk
            write_blocks(1, inode_bmap_nb_of_blocks, inode_bmap);
            int inodeTableLoc = OFFSET + inode_bmap_nb_of_blocks + data_bmap_nb_of_blocks;
            write_blocks(inodeTableLoc, inode_tbl_nb_of_blocks , inode_tbl);
            write_blocks(data_block_first, dir_table_nb_of_blocks, dir_map_tbl);
            
        }
    }
    
    return 0;
}



