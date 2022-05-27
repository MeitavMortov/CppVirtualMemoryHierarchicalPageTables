//
// Created by meitav_mortov and Zimrat Kaniel on 16/05/2022.
//

#include "VirtualMemory.h"
#include "PhysicalMemory.h"

// size of one part of the virtual address (in bits)
#define ADDRESS_PART_SIZE CEIL(((VIRTUAL_ADDRESS_WIDTH - OFFSET_WIDTH) / double(TABLES_DEPTH)))

/**
 * represents the result from the function that searches for a zeros-only frame
 */
typedef struct {
    int is_found;
    uint64_t parent;
}ZerosFrame;

/**
 * represents the result from the function that searches for a page with maximum distance to the wanted page
 */
typedef struct {
    uint64_t page_num;
    uint64_t frame;
    uint64_t parent;
    uint64_t distance;
}MaxDistanceFrame;

/**
 * Returns one part from the virtual address
 * @param index the index of the wanted part
 * @param virtual_address the virtual address of the requested page
 * @return the bits of the wanted part of the address
 */
uint64_t get_address_part(int index, uint64_t virtual_address){
    int shifts = OFFSET_WIDTH + (ADDRESS_PART_SIZE * (TABLES_DEPTH - index - 1));
    uint64_t mask = (1<<ADDRESS_PART_SIZE)-1;
    return (virtual_address>>shifts) & mask;
}

/**
 * Returns the offset part of the given address
 * @param virtual_address the virtual address of the requested page
 * @return the offset in the page
 */
uint64_t get_offset(uint64_t virtual_address){
    uint64_t mask = (1<<OFFSET_WIDTH)-1;
    return virtual_address & mask;
}

/**
 * Verifies that the only-zeros frame is not one of the tables that we have already used in order to find the page
 * @param virtual_address the requested address
 * @param frame the number of the found frame
 * @return 1 if we can safely use this frame, else 0
 */
int verify(uint64_t virtual_address, uint64_t frame){
    if(frame == 0){
        return 0;
    }
    uint64_t last_address = 0;
    for(int i=0; i<TABLES_DEPTH; i++){
        uint64_t current_address_part = get_address_part(i,virtual_address);
        word_t addr;
        PMread(last_address*PAGE_SIZE+current_address_part, &addr);
        if((uint64_t )addr==frame){
            return 0;
        }
        if(addr == 0){
            return 1;
        }
        last_address = addr;
    }
    return 1;
}


/**
 * Fills a frame with zeros
 * @param address the address of the frame to fill
 */
void fill_zeroes(uint64_t address) {
    uint64_t physical_address = address * PAGE_SIZE;
    for(int i=0; i<PAGE_SIZE; i++){
        PMwrite(physical_address+i,0);
    }
}

/**
 * Calculates the distance between the given page and the page we are looking for, according to the formula given in
 * this exercise instructions
 * @param original_virtual_address the requested virtual address
 * @param p the page to calculate the distance to
 * @return the distance between the given page and the page we are looking for
 */
uint64_t distance_to_page_swapped_in(uint64_t original_virtual_address, uint64_t p){
    uint64_t page_swapped_in = original_virtual_address >> OFFSET_WIDTH;
    uint64_t abs_val = page_swapped_in > p ? page_swapped_in - p : p-page_swapped_in;
    uint64_t a = NUM_PAGES - abs_val;
    uint64_t b = abs_val;
    return  a > b ? b : a;
}

/**
 * Chooses available frame according to the third priority:
 * if there is a frame containing an empty table – where all rows are 0.
 *  @param virtualAddress a virtual address.
 *  @param current_vertex current vertex.
 *  @param parent parent of the current vertex.
 *  @param current_level the current level.
 */
ZerosFrame choose_frame_1(uint64_t virtual_address, uint64_t current_vertex, uint64_t parent, int current_level){
    uint64_t zeroes_frame = 1;
    for (int i=0; i<PAGE_SIZE; i++){
        uint64_t current_address = current_vertex * PAGE_SIZE + i;
        word_t next_address=0;
        PMread(current_address, &next_address);
        if(next_address != 0){
            zeroes_frame = 0;
            if(current_level < TABLES_DEPTH -1){
                ZerosFrame returned_frame = choose_frame_1(virtual_address, next_address,
                                                           current_address, current_level+1);

                if(returned_frame.is_found){
                    return returned_frame;
                }
            }
        }
    }
    if(zeroes_frame){
        ZerosFrame result = {
                .is_found=1, .parent=parent
        };
        if(verify(virtual_address, current_vertex)){
            return  result;
        }
    }
    ZerosFrame false_result = {
            .is_found=0, .parent=0
    };
    return false_result;

}


/**
 * Chooses available frame according to the third priority:
 * An unused frame – returns variable with the maximal frame
 * index referenced from any table we visit.
 *  @param current_vertex current vertex.
 *  @param current_level the current level.
 */
uint64_t choose_frame_2(uint64_t current_vertex, int current_level){
    uint64_t max_frame = current_vertex;
    for (int i=0; i<PAGE_SIZE; i++){
        uint64_t current_address = current_vertex * PAGE_SIZE + i;
        word_t next_address;
        PMread(current_address, &next_address);
        if(next_address != 0 && current_level < TABLES_DEPTH){
            uint64_t returned_frame = choose_frame_2(next_address, current_level+1);
            if(returned_frame > max_frame){
                max_frame = returned_frame;
            }
        }
    }
    return  max_frame;
}

/**
 * Chooses available frame according to the third priority:
 * If all frames are already used, then a page must be swapped out from some frame in
 * order to replace it with the relevant page.
 *  @param virtualAddress a virtual address.
 *  @param current_vertex current vertex.
 *  @param parent parent of the current vertex.
 *  @param p the current page.
 *  @param current_level the current level.
 */
MaxDistanceFrame choose_frame_3(uint64_t virtual_address,
                                uint64_t current_vertex,
                                uint64_t parent,
                                uint64_t p,
                                int current_level){
    if(current_level == TABLES_DEPTH){
        uint64_t distance = distance_to_page_swapped_in(virtual_address,p); //returned_frame.page_num)
        MaxDistanceFrame result = {.page_num=p, .frame=current_vertex,.parent=parent, .distance=distance};
        return result;
    }
    uint64_t max_distance = 0;
    MaxDistanceFrame max_frame = {0,0,0, 0};
    for (int i=0; i<PAGE_SIZE; i++){
        uint64_t current_address = current_vertex * PAGE_SIZE + i;
        word_t next_address;
        PMread(current_address, &next_address);
        if(next_address != 0 && current_level < TABLES_DEPTH){
            uint64_t new_p = (p<<ADDRESS_PART_SIZE)+i;
            MaxDistanceFrame returned_frame = choose_frame_3(virtual_address,next_address,
                                                             current_address,new_p, current_level+1);
            if( returned_frame.distance > max_distance){
                max_distance = returned_frame.distance;
                max_frame = returned_frame;
            }
        }
    }
    return  max_frame;
}

/**
 * Finds  available frame according to the priorities which defined exercise.
 *  @param virtualAddress a virtual address.
 */
uint64_t find_frame(uint64_t virtual_address){
    ZerosFrame returned_zeros_frame = choose_frame_1(virtual_address,0,0,0);
    if(returned_zeros_frame.is_found){
        word_t result;
        PMread(returned_zeros_frame.parent, &result);
        PMwrite(returned_zeros_frame.parent,0);
        return result;
    }
    uint64_t max_frame = choose_frame_2(0,0);
    if(NUM_FRAMES > max_frame + 1){
        return max_frame + 1;
    }
    MaxDistanceFrame returned_max_distance = choose_frame_3(virtual_address,0,0,0,0);
    word_t val;
    PMread(returned_max_distance.parent, &val);
    PMwrite(returned_max_distance.parent, 0);
    PMevict(returned_max_distance.frame, returned_max_distance.page_num);
    return  returned_max_distance.frame;
}

/**
 * Returns physical address which  is matching to the given virtual address.
 *  @param virtualAddress a virtual address.
 */
uint64_t get_physical_address(uint64_t virtual_address){
    uint64_t last_address = 0;
    for(int i=0; i<TABLES_DEPTH; i++){
        uint64_t current_address_part = get_address_part(i,virtual_address);
        word_t addr;
        PMread(last_address*PAGE_SIZE+current_address_part, &addr);
        if(addr == 0){
            uint64_t next_address = find_frame(virtual_address);
            PMwrite(last_address*PAGE_SIZE+current_address_part, next_address);
            if(i==TABLES_DEPTH-1){ // actual page.
                PMrestore(next_address, virtual_address>>OFFSET_WIDTH);
            }
            else{ //a table
                fill_zeroes(next_address);
            }
            last_address = next_address;
        }
        else{ //addr != 0
            last_address = addr;
        }


    }
    uint64_t offset = get_offset(virtual_address);
    return  last_address * PAGE_SIZE + offset;
}

/**
 * Checks if the given virtual address is valid.
 *  @param virtualAddress a virtual address.
 */
int validate_virtual_address(uint64_t virtual_address){
    uint64_t virtual_address_space_size = 1<<VIRTUAL_ADDRESS_WIDTH;
    if(virtual_address < virtual_address_space_size){
        return 1;
    }
    return 0;
}

/**
 * Initialize the virtual memory.
 */
void VMinitialize(){
    fill_zeroes(0);
}

/**
 * Reads a word from the given virtual address
 * and puts its content in *value.
 * returns 1 on success.
 * returns 0 on failure (if the address cannot be mapped to a physical
 * address for any reason)
 * @param virtualAddress a virtual address.
 * @param value value contains the value which is writen on the address.
 */
int VMread(uint64_t virtualAddress, word_t* value){
    if(!validate_virtual_address(virtualAddress)){
        return 0;
    }
    uint64_t physical_address = get_physical_address(virtualAddress);
    PMread(physical_address, value);
    return 1;
}

/**
 * Writes a word to the given virtual addruint64_tess.
 * returns 1 on success.
 * returns 0 on failure (if the address cannot be mapped to a physical
 * address for any reason)
 * @param virtualAddress a virtual address.
 * @param value value to write on the address.
 */
int VMwrite(uint64_t virtualAddress, word_t value){
    if(!validate_virtual_address(virtualAddress)){
        return 0;
    }
    uint64_t physical_address = get_physical_address(virtualAddress);
    PMwrite(physical_address, value);
    return 1;
}

