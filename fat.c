#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdlib.h>
#include <ctype.h>
#include <sys/types.h>

#include "fat_structures.h"

void handleEntryFile(FATData *fatData, DIR_ENT *dir, char *name);

void die(const char *msg) {
    fprintf(stderr, "%s\n", msg);
    exit(1);
}

void fs_read(int fd, size_t offset, void *buffer, size_t len) {
    // TODO
    lseek(fd,offset,SEEK_SET);
    if(read(fd, buffer, len)==-1){
        exit(EXIT_FAILURE);
    }
}

void printLabel(int fd) {
    char *format_string = "The Label of the given FAT16 file system is: \"%.11s\"\n";
    char name [12];
    fs_read(fd, 43 , &name, 11);
    printf(format_string, name);
}


//dont forget to free the fat
FAT_ENTRY *readFAT(int fd, struct boot_sector *boot) {
    FAT_ENTRY *erg = (FAT_ENTRY *) malloc(boot->fat_length*GET_UNALIGNED_W(boot->sector_size));
    fs_read(fd, boot->reserved * GET_UNALIGNED_W(boot->sector_size),erg, boot->fat_length*GET_UNALIGNED_W(boot->sector_size));
    return erg;
}

void initFatData(int fd, struct boot_sector *boot, FATData *fatData) {
    fatData->fd=fd;
    fatData->boot=boot;
    fatData->fat=readFAT(fd, boot);
    fatData->sector_size=GET_UNALIGNED_W(boot->sector_size);
    fatData->entry_size=boot->cluster_size * fatData->sector_size; //DAS HIER UNBEDINGT UEBERNEHMEN ???
    fatData->root_entries=GET_UNALIGNED_W(boot->dir_entries);
    fatData->rootdir_offset=boot->reserved * fatData->sector_size +  boot->fat_length*boot->fats*(fatData->sector_size); // + (fatData->sector_size)*
    fatData->data_offset=fatData->rootdir_offset + fatData->root_entries*sizeof(DIR_ENT); //alternativ vielleicht 224 * 32
}

void readFile(FATData *fatData, DIR_ENT *dir) {
    if (CHECKFLAGS(dir->attr, ATTR_DIR)) return;
    puts("Reading file:");
    
    uint16_t cur = dir->start;
    uint16_t to_go = dir->size;
    while(cur != 0xffff && to_go > 0){
        char buf [fatData->entry_size+1];
        buf[fatData->entry_size] = 0;
        fs_read(fatData->fd, fatData->data_offset + (cur-2) * fatData->entry_size, &buf, fatData->entry_size);
        for(uint16_t i = 0;to_go > 0 && i<fatData->entry_size; i++, to_go--){
            printf("%c", buf[i]);
        }
        cur = fatData->fat[cur].value;
    }
    printf("\n");
}

void iterateDirectoryFile(FATData *fatData, DIR_ENT *dir, char *name) {
    char cur_name [12] = {0};
    strncpy(cur_name, (char *) dir->name, 11);
    if (!CHECKFLAGS(dir->attr, ATTR_DIR) || strcmp(cur_name, MSDOS_DOT) == 0 || strcmp(cur_name, MSDOS_DOTDOT) == 0) return;
        uint16_t cur = dir->start;
        while(cur != 0xffff){
            for(uint16_t i = 0; i<fatData->sector_size/sizeof(DIR_ENT); i++){
                DIR_ENT dir_new;
                fs_read(fatData->fd, (fatData->data_offset +  (cur-2)*fatData->entry_size + i * sizeof(DIR_ENT)), &dir_new, sizeof(DIR_ENT));
                handleEntryFile(fatData, &dir_new , name);
            }
            cur = fatData->fat[cur].value;
        }
}

void handleEntryFile(FATData *fatData, DIR_ENT *dir, char *name){
    if(CHECKFLAGS(dir->attr, ATTR_DIR)){
        iterateDirectoryFile(fatData, dir, name);
    }else if(strcmp(sanitizeName((char *) dir->name), name) == 0){
        printf("File: %s\n",sanitizeName((char *) dir->name));
        readFile(fatData, dir);
    }
    
}

void iterateRootFile(FATData *fatData, char *name) {
    for(uint16_t i = 0; i < fatData->root_entries; i++){
        DIR_ENT dir;
        fs_read(fatData->fd, (fatData->rootdir_offset + i * sizeof(DIR_ENT)), &dir, sizeof(DIR_ENT));
        handleEntryFile(fatData, &dir , name);
    }
}

void iterateDirectory(FATData *fatData, DIR_ENT *dir, int level) {
    char name [12] = {0};
    strncpy(name, (char *) dir->name, 11);
    if (!CHECKFLAGS(dir->attr, ATTR_DIR) || strcmp(name, MSDOS_DOT) == 0 || strcmp(name, MSDOS_DOTDOT) == 0) return;
        //for(int i=0;i<fatData.)
        uint16_t cur = dir->start;
        while(cur != 0xffff){
            for(uint16_t i = 0; i<fatData->sector_size/sizeof(DIR_ENT); i++){
                DIR_ENT dir_new;
                fs_read(fatData->fd, (fatData->data_offset +  (cur-2)*fatData->entry_size + i * sizeof(DIR_ENT)), &dir_new, sizeof(DIR_ENT));
                handleEntry(fatData, &dir_new , level+1);
            }
            cur = fatData->fat[cur].value;
        }
}

void handleEntry(FATData *fatData, DIR_ENT *dir, int level){
    if(CHECKFLAGS(dir->attr, ATTR_VOLUME) || dir->name[0] == 0){
        return;
    }
    for(int i = 0; i<level; i++){
        printf("\t");
    }
    if(CHECKFLAGS(dir->attr, ATTR_DIR)){
        printf("Dir : %s\n",sanitizeName((char*) dir->name));
        iterateDirectory(fatData, dir, level);
    }else{
        printf("File: %s\n",sanitizeName((char*) dir->name));
    }
    
}

void iterateRoot(FATData *fatData) {
    for(uint16_t i = 0; i < fatData->root_entries; i++){
        DIR_ENT dir;
        fs_read(fatData->fd, (fatData->rootdir_offset + i * sizeof(DIR_ENT)), &dir, sizeof(DIR_ENT));
        handleEntry(fatData, &dir , 0);
    }
}

void print_help(char *prog_name) {
    printf("Usage: %s FAT16_IMAGE_FILE\n", prog_name);
    fflush(stdout);
}

void print_menu() {
    puts("Type your command, then [enter]");
    puts("Available commands:");
    puts("l      Print label of FAT16_IMAGE_FILE");
    puts("i      Iterate file system and print the name of all directories/files");
    puts("f FILE Print content of FILE");
    puts("q      quit the program.");
    fflush(stdout);
}

int main(int argc, char *argv[]) {
    if(argc == 1 || argv[1]==NULL){
        print_help(*argv);
        exit(EXIT_FAILURE);
    }
    char command = ' ';
    do {
        print_menu();

        do {
            command = (char) fgetc(stdin);
        } while (isspace(command) || command == '\n');
        switch (command) {
            case 'l': {
                int fd = open(argv[1],O_RDONLY);
                printLabel(fd);
                close(fd);
                break;
            }
            case 'i': {
                int fd = open(argv[1],O_RDONLY);
                FATData data;
                struct boot_sector sector;
                fs_read(fd, 0, &sector, sizeof(struct boot_sector));
                initFatData(fd, &sector, &data);
                iterateRoot(&data);
                free(data.fat);
                close(fd);
                break;
            }
            case 'f':{
                int fd = open(argv[1],O_RDONLY);
                FATData data;
                struct boot_sector sector;
                fs_read(fd, 0, &sector, sizeof(struct boot_sector));
                initFatData(fd, &sector, &data);
                char space = getc(stdin);
                space = space;
                char name [13] = {0};
                fgets(name, 13, stdin);
                for(int i = 0; i < 12; i++){
                    if(name[i] == '\n'){
                        name[i] = 0;
                    }
                }
                iterateRootFile(&data, name);
                free(data.fat);
                close(fd);
                break;
            }
            case 'q': exit(EXIT_SUCCESS);
            default: ;
        }
        printf("\n");
    } while (command != 'q');
    
    exit(EXIT_SUCCESS);
}
